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

#include "judo.h"

#if defined(JUDO_PARSER)
#include <string.h>
#include <assert.h>

struct compound
{
    struct judo_value *value;
    struct judo_value *elements_tail;
    struct judo_member *members_tail;
};

struct parser
{
    void *user_data;
    judo_memfunc memfunc;
    const char *string;
    struct judo_value *root;
    int32_t depth;
    struct compound stack[JUDO_MAXDEPTH]; // Arrays and objects.
};

struct judo_boolean
{
    bool value;
};

struct judo_array
{
    struct judo_value *next;
    int32_t length;
};

struct judo_member
{
    struct judo_member *next;
    struct judo_value *value;
    struct judo_span name;
};

struct judo_object
{
    struct judo_member *members;
    int32_t size;
};

struct judo_value
{
    struct judo_value *next;
    struct judo_span where;
    union // cppcheck-suppress misra-c2012-19.2
    {
        struct judo_boolean boolean;
        struct judo_array array;
        struct judo_object object;
    } u; // cppcheck-suppress misra-c2012-19.2
    enum judo_type type;
};

static void *mem_alloc(struct parser *parser, size_t size)
{
    void *ptr = parser->memfunc(parser->user_data, NULL, size);
    if (ptr != NULL)
    {
        (void)memset(ptr, 0, size);
    }
    return ptr;
}

static struct judo_value *allocate_value(struct parser *parser)
{
    struct judo_value *value = parser->memfunc(parser->user_data, NULL, sizeof(value[0])); // cppcheck-suppress misra-c2012-11.5
    if (value != NULL)
    {
        (void)memset(value, 0, sizeof(value[0]));
    }
    return value;
}

static void link(struct parser *parser, struct judo_value *value)
{
    // LCOV_EXCL_START
    assert(parser != NULL);
    assert(value != NULL);
    // LCOV_EXCL_STOP

    // Assume the first JSON value encountered is the root value.
    if (parser->root == NULL)
    {
        parser->root = value;
    }

    // Check if processing an token of an array or a member of an object.
    // If so, then link the JSON value with the array/object.
    if (parser->depth > 0)
    {
        struct compound *top = &parser->stack[parser->depth - 1];
        if (top->value->type == JUDO_TYPE_ARRAY)
        {
            if (top->elements_tail == NULL)
            {
                assert(top->value->u.array.next == NULL); // LCOV_EXCL_BR_LINE: The head should be null if the tail is null.
                top->elements_tail = value;
                top->value->u.array.next = value;
            }
            else
            {
                assert(top->elements_tail != value); // LCOV_EXCL_BR_LINE
                top->elements_tail->next = value;
                top->elements_tail = value;
            }
            top->value->u.array.length += 1;
        }
        else
        {
            // LCOV_EXCL_START
            assert(top->value->type == JUDO_TYPE_OBJECT);
            assert(top->members_tail != NULL);
            // LCOV_EXCL_STOP

            struct judo_object *object = &top->value->u.object;
            top->members_tail->value = value;
            object->size += 1;
        }
    }
}

static enum judo_result process_value(struct parser *parser, const struct judo_stream *js)
{
    enum judo_result result = JUDO_RESULT_SUCCESS;

    if (js->token == JUDO_TOKEN_ARRAY_BEGIN)
    {
        struct judo_value *value = allocate_value(parser);
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_ARRAY;
            value->where = js->where;
            link(parser, value);

            parser->stack[parser->depth].value = value;
            parser->depth += 1;
        }
    }
    else if (js->token == JUDO_TOKEN_OBJECT_BEGIN)
    {
        struct judo_value *value = allocate_value(parser);
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_OBJECT;
            value->where = js->where;
            link(parser, value);

            parser->stack[parser->depth].value = value;
            parser->depth += 1;
        }
    }
    else if ((js->token == JUDO_TOKEN_ARRAY_END) ||
             (js->token == JUDO_TOKEN_OBJECT_END))
    {
        assert(parser->depth > 0); // LCOV_EXCL_BR_LINE
        struct compound *top = &parser->stack[parser->depth - 1];
        top->value->where.length = (js->where.offset + js->where.length) - top->value->where.offset;
        top->value = NULL;
        top->elements_tail = NULL;
        top->members_tail = NULL;
        parser->depth -= 1;
    }
    else if (js->token == JUDO_TOKEN_NULL)
    {
        struct judo_value *value = allocate_value(parser);
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_NULL;
            value->where = js->where;
            link(parser, value);
        }
    }
    else if ((js->token == JUDO_TOKEN_TRUE) ||
             (js->token == JUDO_TOKEN_FALSE))
    {
        struct judo_value *value = allocate_value(parser);
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_BOOL;
            value->where = js->where;
            value->u.boolean.value = (js->token == JUDO_TOKEN_TRUE) ? true : false;
            link(parser, value);
        }
    }
    else if (js->token == JUDO_TOKEN_NUMBER)
    {
        struct judo_value *value = allocate_value(parser);
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_NUMBER;
            value->where = js->where;
            link(parser, value);
        }
    }
    else if (js->token == JUDO_TOKEN_STRING)
    {
        struct judo_value *value = allocate_value(parser);
        if (value == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            value->type = JUDO_TYPE_STRING;
            value->where = js->where;
            link(parser, value);
        }
    }
    else if (js->token == JUDO_TOKEN_OBJECT_NAME)
    {
        assert(parser->depth > 0); // LCOV_EXCL_BR_LINE

        // There must be an object being parsed to have received this value.
        struct compound *top = &parser->stack[parser->depth - 1];
        assert(top->value->type == JUDO_TYPE_OBJECT); // LCOV_EXCL_BR_LINE

        // Allocate a structure to represent the object member.
        struct judo_member *member = mem_alloc(parser, sizeof(member[0])); // cppcheck-suppress misra-c2012-11.5
        if (member == NULL)
        {
            result = JUDO_RESULT_OUT_OF_MEMORY;
        }
        else
        {
            // Save the lexeme location.
            member->name = js->where;

            // Link the object name into the linked list.
            struct judo_object *object = &top->value->u.object;
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

enum judo_result judo_parse(const char *source, int32_t length, judo_value **root, struct judo_error *error, void *user_data, judo_memfunc memfunc) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
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
        struct parser parser = {
            .string = source,
            .user_data = user_data,
            .memfunc = memfunc,
        };

        for (;;)
        {
            result = judo_scan(&stream, source, length);
            if (result == JUDO_RESULT_SUCCESS)
            {
                result = process_value(&parser, &stream);
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
            (void)judo_free(parser.root, user_data, memfunc);
            parser.root = NULL;
        }

        *root = parser.root;
    }

    return result;
}

enum judo_result judo_free(judo_value *root, void *user_data, judo_memfunc memfunc)
{
    struct freestack
    {
        judo_value *value;
        judo_value *token;
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
            if (top->token != NULL)
            {
                judo_value *next = judo_next(top->token);
                if (next == NULL)
                {
                    top->value = top->token;
                }
                else
                {
                    stack[depth].value = top->token;
                    depth += 1;
                }
                top->token = next;
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

                (void)memfunc(user_data, member, sizeof(member[0]));
                top->member = next;
            }
            else if (top->value == NULL)
            {
                depth -= 1;
            }
            else
            {
                judo_value *token;
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
                case JUDO_TYPE_BOOL:
                case JUDO_TYPE_STRING:
                case JUDO_TYPE_NUMBER:
                    (void)memfunc(user_data, value, sizeof(value[0]));
                    break;

                case JUDO_TYPE_ARRAY:
                    token = judo_first(value);
                    if (token != NULL)
                    {
                        stack[depth].token = token;
                        depth += 1;
                    }
                    (void)memfunc(user_data, value, sizeof(value[0]));
                    break;

                case JUDO_TYPE_OBJECT:
                    member = judo_membfirst(value);
                    if (member != NULL)
                    {
                        stack[depth].member = member;
                        depth += 1;
                    }
                    (void)memfunc(user_data, value, sizeof(value[0]));
                    break;

                // LCOV_EXCL_START
                default:
                    result = JUDO_RESULT_MALFUNCTION; // This should never be reachable.
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
        type = JUDO_TYPE_NULL;
    }
    else
    {
        type = value->type;
    }
    return type;
}

judo_value *judo_first(judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    judo_value *first = NULL;
    if (value != NULL)
    {
        if (value->type == JUDO_TYPE_ARRAY)
        {
            first = value->u.array.next;
        }
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
    return value->u.object.members;
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
        if (value->type == JUDO_TYPE_BOOL)
        {
            b = value->u.boolean.value;
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

int32_t judo_len(const judo_value *value) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    int32_t length;
    if (value != NULL)
    {
        if (value->type == JUDO_TYPE_ARRAY)
        {
            length = value->u.array.length;
        }
        else if (value->type == JUDO_TYPE_OBJECT)
        {
            length = value->u.object.size;
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
