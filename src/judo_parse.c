/*
 *  Judo - Embeddable JSON and JSON5 parser.
 *  Copyright (c) 2025 Railgun Labs, LLC
 *
 *  This software is dual-licensed: you can redistribute it and/or modify
 *  it under the terms of the GNU General exportedlic License version 3 as
 *  exportedlished by the Free Software Foundation. For the terms of this
 *  license, see <https://www.gnu.org/licenses/>.
 *
 *  Alternatively, you can license this software under a proprietary
 *  license, as set out in <https://railgunlabs.com/judo/license/>.
 */

// The Judo parser does _not_ use a union type to represent JSON values like most
// other JSON parsers do. The reasons are (1) MISRA C:2012 Rule 19.2 advises against
// union types and Judo strives for strong conformance and (2) JSON values are more
// compactly represented in in-memory using smaller structures, where possible, and
// reserving larger structures exclusively for JSON types that require it.

#include "judo.h"

#if defined(JUDO_PARSER)
#include <string.h>
#include <assert.h>

struct judo_value
{
    judo_value *next;
    struct judo_span where;
    uint8_t type;
};

struct judo_member
{
    judo_member *next;
    judo_value *value;
    struct judo_span name;
};

struct boolean
{
    judo_value descriptor; // Must be the first field in-memory for casting.
    uint8_t value;
};

struct array
{
    judo_value descriptor; // Must be the first field in-memory for casting.
    judo_value *next;
    int32_t length;
};

struct object
{
    judo_value descriptor; // Must be the first field in-memory for casting.
    judo_member *members;
    int32_t size;
};

struct parse_stack
{
    judo_value *collection; // The current array or object being parsed (or null if neither are being parsed).
    judo_value *elements_tail; // Last array element of the current array being parsed.
    judo_member *members_tail; // Last object member of the current object being parsed.
};

struct context
{
    void *udata; // User pointer passed through to the users custom memory allocator function.
    judo_memfunc memfunc; // User custom memory allocation function.
    const char *string;
    judo_value *root; // Root value of the JSON structure.
    int32_t stack_depth;
    struct parse_stack stack[JUDO_MAXDEPTH]; // Arrays and objects.
};

static void *judo_alloc(struct context *ctx, size_t size)
{
    void *ptr = ctx->memfunc(ctx->udata, NULL, size);
    if (ptr != NULL)
    {
        (void)memset(ptr, 0, size);
    }
    return ptr;
}

static struct object *to_object(void *value)
{
    struct object *object = (struct object *)value; // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
    // LCOV_EXCL_START
    assert(object != NULL);
    assert(object->descriptor.type == (uint8_t)JUDO_TYPE_OBJECT);
    // LCOV_EXCL_STOP
    return object;
}

static struct array *to_array(void *value)
{
    struct array *array = (struct array *)value; // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
    // LCOV_EXCL_START
    assert(array != NULL);
    assert(array->descriptor.type == (uint8_t)JUDO_TYPE_ARRAY);
    // LCOV_EXCL_STOP
    return array;
}

static struct boolean *to_boolean(void *value)
{
    struct boolean *boolean = (struct boolean *)value; // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
    // LCOV_EXCL_START
    assert(boolean != NULL);
    assert(boolean->descriptor.type == (uint8_t)JUDO_TYPE_BOOL);
    // LCOV_EXCL_STOP
    return boolean;
}

static void link(struct context *ctx, judo_value *value)
{
    // LCOV_EXCL_START
    assert(ctx != NULL);
    assert(value != NULL);
    // LCOV_EXCL_STOP

    // Assume the first JSON value encountered is the root value.
    if (ctx->root == NULL)
    {
        ctx->root = value;
    }

    // Check if processing an token of an array or a member of an object.
    // If so, then link the JSON value with the array/object.
    if (ctx->stack_depth > 0)
    {
        struct parse_stack *top = &ctx->stack[ctx->stack_depth - 1];
        if (top->collection->type == (uint8_t)JUDO_TYPE_ARRAY)
        {
            struct array *array = to_array(top->collection);
            if (top->elements_tail == NULL)
            {
                assert(array->next == NULL); // LCOV_EXCL_BR_LINE: The head should be null if the tail is null.
                top->elements_tail = value;
                array->next = value;
            }
            else
            {
                assert(top->elements_tail != value); // LCOV_EXCL_BR_LINE
                top->elements_tail->next = value;
                top->elements_tail = value;
            }
            array->length += 1;
        }
        else
        {
            // LCOV_EXCL_START
            assert(top->collection->type == (uint8_t)JUDO_TYPE_OBJECT);
            assert(top->members_tail != NULL);
            // LCOV_EXCL_STOP

            struct object *object = to_object(top->collection);
            top->members_tail->value = value;
            object->size += 1;
        }
    }
}

static enum judo_result process_value(struct context *ctx, const struct judo_stream *js)
{
    enum judo_result result = JUDO_RESULT_SUCCESS;
    if (js->token == JUDO_TOKEN_ARRAY_BEGIN)
    {
        assert (ctx->stack_depth < JUDO_MAXDEPTH); // LCOV_EXCL_BR_LINE
        struct array *array = judo_alloc(ctx, sizeof(array[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (array == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            array->descriptor.type = JUDO_TYPE_ARRAY;
            array->descriptor.where = js->where;
            link(ctx, &array->descriptor);

            ctx->stack[ctx->stack_depth].collection = &array->descriptor;
            ctx->stack_depth += 1;
        }
    }
    else if (js->token == JUDO_TOKEN_OBJECT_BEGIN)
    {
        assert (ctx->stack_depth < JUDO_MAXDEPTH); // LCOV_EXCL_BR_LINE
        struct object *object = judo_alloc(ctx, sizeof(object[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (object == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            object->descriptor.type = JUDO_TYPE_OBJECT;
            object->descriptor.where = js->where;
            link(ctx, &object->descriptor);

            ctx->stack[ctx->stack_depth].collection = &object->descriptor;
            ctx->stack_depth += 1;
        }
    }
    else if ((js->token == JUDO_TOKEN_ARRAY_END) ||
             (js->token == JUDO_TOKEN_OBJECT_END))
    {
        assert(ctx->stack_depth > 0); // LCOV_EXCL_BR_LINE
        struct parse_stack *top = &ctx->stack[ctx->stack_depth - 1];
        top->collection->where.length = (js->where.offset + js->where.length) - top->collection->where.offset;
        top->collection = NULL;
        top->elements_tail = NULL;
        top->members_tail = NULL;
        ctx->stack_depth -= 1;
    }
    else if (js->token == JUDO_TOKEN_NULL)
    {
        judo_value *value = judo_alloc(ctx, sizeof(value[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_NULL;
            value->where = js->where;
            link(ctx, value);
        }
    }
    else if ((js->token == JUDO_TOKEN_TRUE) ||
             (js->token == JUDO_TOKEN_FALSE))
    {
        struct boolean *boolean = judo_alloc(ctx, sizeof(boolean[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (boolean == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            boolean->descriptor.type = JUDO_TYPE_BOOL;
            boolean->descriptor.where = js->where;
            boolean->value = (js->token == JUDO_TOKEN_TRUE) ? (uint8_t)1 : (uint8_t)0;
            link(ctx, &boolean->descriptor);
        }
    }
    else if (js->token == JUDO_TOKEN_NUMBER)
    {
        judo_value *value = judo_alloc(ctx, sizeof(value[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_NUMBER;
            value->where = js->where;
            link(ctx, value);
        }
    }
    else if (js->token == JUDO_TOKEN_STRING)
    {
        judo_value *value = judo_alloc(ctx, sizeof(value[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_STRING;
            value->where = js->where;
            link(ctx, value);
        }
    }
    else if (js->token == JUDO_TOKEN_OBJECT_NAME)
    {
        assert(ctx->stack_depth > 0); // LCOV_EXCL_BR_LINE

        // There must be an object being parsed to have received this value.
        struct parse_stack *top = &ctx->stack[ctx->stack_depth - 1];
        assert(top->collection->type == (uint8_t)JUDO_TYPE_OBJECT); // LCOV_EXCL_BR_LINE

        // Allocate a structure to represent the object member.
        judo_member *member = judo_alloc(ctx, sizeof(member[0])); // cppcheck-suppress misra-c2012-11.5 ; Cast from void pointer to object pointer.
        if (member == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            // Save the lexeme location.
            member->name = js->where;

            // Link the object name into the linked list.
            struct object *object = to_object(top->collection);
            if (object->members == NULL)
            {
                assert(top->members_tail == NULL); // LCOV_EXCL_BR_LINE
                object->members = member;
                top->members_tail = member;
            }
            else
            {
                top->members_tail->next = member;
                top->members_tail = member;
            }
        }
    }
    else
    {
        assert(js->token == JUDO_TOKEN_EOF); // LCOV_EXCL_BR_LINE
    }

    return result;
}

enum judo_result judo_parse(const char *source, int32_t length, judo_value **root, struct judo_error *error, void *udata, judo_memfunc memfunc) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    enum judo_result result;

    if (root == NULL)
    {
        result = JUDO_RESULT_INVALID_OPERATION;
    }
    else if (source == NULL)
    {
        result = JUDO_RESULT_INVALID_OPERATION;
        *root = NULL;
    }
    else if (memfunc == NULL)
    {
        result = JUDO_RESULT_INVALID_OPERATION;
        *root = NULL;
    }
    else
    {
        struct judo_stream stream = {0};
        struct context ctx = {
            .string = source,
            .udata = udata,
            .memfunc = memfunc,
        };

        for (;;)
        {
            result = judo_scan(&stream, source, length);
            if (result == JUDO_RESULT_SUCCESS)
            {
                result = process_value(&ctx, &stream);
            }

            if ((result != JUDO_RESULT_SUCCESS) || (stream.token == JUDO_TOKEN_EOF))
            {
                break;
            }
        }

        if (result == JUDO_RESULT_SUCCESS)
        {
            if (error != NULL)
            {
                (void)memset(error, 0, sizeof(error[0]));
            }
        }
        else
        {
            if (error != NULL)
            {
                if (result == JUDO_RESULT_OUT_OF_MEMORY)
                {
                    (void)memcpy(error->description, "memory allocation failed", 25);
                }
                else
                {
                    (void)memcpy(error->description, stream.error, JUDO_ERRMAX);
                }
                error->where = stream.where;
            }

            // Use the result code from the scan operation, not the free operation.
            (void)judo_free(ctx.root, udata, memfunc);
            ctx.root = NULL;
        }

        *root = ctx.root;
    }

    return result;
}

enum judo_result judo_free(judo_value *root, void *udata, judo_memfunc memfunc)
{
    struct freestack
    {
        judo_value *value;
        judo_value *element;
        judo_member *member;
    };

    enum judo_result result = JUDO_RESULT_SUCCESS;

    if (memfunc == NULL)
    {
        result = JUDO_RESULT_INVALID_OPERATION;
    }

    if ((result == JUDO_RESULT_SUCCESS) && (root != NULL))
    {
        int32_t depth;
        struct freestack stack[JUDO_MAXDEPTH] = {0}; // Arrays and objects.

        depth = 1;
        stack[0].value = root;

        while (depth > 0)
        {
            struct freestack *top = &stack[depth - 1];
            if (top->element != NULL)
            {
                judo_value *next = judo_next(top->element);
                if (next == NULL)
                {
                    top->value = top->element;
                }
                else
                {
                    stack[depth].value = top->element;
                    depth += 1;
                }
                top->element = next;
            }
            else if (top->member != NULL)
            {
                judo_member *member = top->member;
                judo_member *next = judo_membnext(member);

                if (next == NULL)
                {
                    top->value = judo_membvalue(member);
                }
                else
                {
                    stack[depth].value = judo_membvalue(member);
                    depth += 1;
                }

                (void)memfunc(udata, member, sizeof(member[0]));
                top->member = next;
            }
            else if (top->value == NULL)
            {
                depth -= 1;
            }
            else
            {
                judo_value *element;
                judo_member *member;
                judo_value *value = top->value;
                assert(value != NULL); // LCOV_EXCL_BR_LINE
                
                (void)memset(top, 0, sizeof(top[0]));
                depth -= 1; // Pop from the stack.

                // LCOV_EXCL_BR_START
                switch (judo_gettype(value))
                // LCOV_EXCL_BR_STOP
                {
                case JUDO_TYPE_NULL:
                case JUDO_TYPE_STRING:
                case JUDO_TYPE_NUMBER:
                    (void)memfunc(udata, value, sizeof(judo_value));
                    break;

                case JUDO_TYPE_BOOL:
                    (void)memfunc(udata, value, sizeof(struct boolean));
                    break;

                case JUDO_TYPE_ARRAY:
                    element = judo_first(value);
                    if (element != NULL)
                    {
                        stack[depth].element = element;
                        depth += 1;
                    }
                    (void)memfunc(udata, value, sizeof(struct array));
                    break;

                case JUDO_TYPE_OBJECT:
                    member = judo_membfirst(value);
                    if (member != NULL)
                    {
                        stack[depth].member = member;
                        depth += 1;
                    }
                    (void)memfunc(udata, value, sizeof(struct object));
                    break;

                // LCOV_EXCL_START
                default:
                    result = JUDO_RESULT_MALFUNCTION; // This will never be reachable.
                    break;
                // LCOV_EXCL_STOP
                }
            }
        }
    }
    
    return result;
}

enum judo_type judo_gettype(const judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    enum judo_type type;
    if (value == NULL)
    {
        type = JUDO_TYPE_INVALID;
    }
    else
    {
        type = value->type;
    }
    return type;
}

judo_value *judo_first(judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    judo_value *first;
    if ((value == NULL) || (value->type != (uint8_t)JUDO_TYPE_ARRAY))
    {
        first = NULL;
    }
    else
    {
        first = to_array(value)->next;
    }
    return first;
}

judo_value *judo_next(judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    judo_value *next;
    if (value == NULL)
    {
        next = NULL;
    }
    else
    {
        next = value->next;
    }
    return next;
}

judo_member *judo_membfirst(judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    judo_member *member;
    if ((value == NULL) || (value->type != (uint8_t)JUDO_TYPE_OBJECT))
    {
        member = NULL;
    }
    else
    {
        member = to_object(value)->members;
    }
    return member;
}

judo_member *judo_membnext(judo_member *member) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    judo_member *next;
    if (member == NULL)
    {
        next = NULL;
    }
    else
    {
        next = member->next;
    }
    return next;
}

bool judo_tobool(judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    bool b = false;
    if (value != NULL)
    {
        if (value->type == (uint8_t)JUDO_TYPE_BOOL)
        {
            b = to_boolean(value)->value ? true : false;
        }
    }
    return b;
}

struct judo_span judo_name2span(const judo_member *member) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    struct judo_span span = {0};
    if (member != NULL)
    {
        span = member->name;
    }
    return span;
}

int32_t judo_len(judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    int32_t length;
    if (value != NULL)
    {
        if (value->type == (uint8_t)JUDO_TYPE_ARRAY)
        {
            length = to_array(value)->length;
        }
        else if (value->type == (uint8_t)JUDO_TYPE_OBJECT)
        {
            length = to_object(value)->size;
        }
        else
        {
            length = 0;
        }
    }
    else
    {
        length = 0;
    }
    return length;
}

judo_value *judo_membvalue(judo_member *member) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    judo_value *value;
    if (member == NULL)
    {
        value = NULL;
    }
    else
    {
        value = member->value;
    }
    return value;
}

struct judo_span judo_value2span(const judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    struct judo_span span = {0};
    if (value != NULL)
    {
        span = value->where;
    }
    return span;
}

#endif
