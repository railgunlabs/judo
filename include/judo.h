/*
 *  Judo - Embeddable JSON and JSON5 parser.
 *  Copyright (c) 2025 Railgun Labs, LLC
 *
 *  This software is dual-licensed: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License version 3
 *  as published by the Free Software Foundation. For the terms of this
 *  license, see <https://www.gnu.org/licenses/>.
 *
 *  Alternatively, you can license this software under a proprietary
 *  license, as set out in <https://railgunlabs.com/judo/license/>.
 */

// Documentation is available at <https://railgunlabs.com/judo/manual/>.

#ifndef JUDO_H
#define JUDO_H

#include "judo_config.h"
#include <stdint.h>

#if defined(JUDO_PARSER)
#include <stddef.h>
#include <stdbool.h>
#endif

#ifdef DOXYGEN
#define JUDO_MAXDEPTH
typedef long double judo_number;
#endif

#define JUDO_ERRMAX 36

enum judo_result
{
    JUDO_SUCCESS,
    JUDO_BAD_SYNTAX,
    JUDO_NO_BUFFER_SPACE,
    JUDO_ILLEGAL_BYTE_SEQUENCE,
    JUDO_OUT_OF_RANGE,
    JUDO_INVALID_OPERATION,
    JUDO_MAXIMUM_NESTING,
    JUDO_OUT_OF_MEMORY,
    JUDO_MALFUNCTION,
};

// An "element" marks a point of interests when parsing the JSON stream.
// They may or may not correspond with a JSON token.
enum judo_element
{
    JUDO_INVALID,
    JUDO_NULL,
    JUDO_TRUE,
    JUDO_FALSE,
    JUDO_NUMBER,
    JUDO_STRING,
    JUDO_ARRAY_PUSH,
    JUDO_ARRAY_POP,
    JUDO_OBJECT_PUSH,
    JUDO_OBJECT_POP,
    JUDO_OBJECT_NAME,
    JUDO_EOF
};

// Field names beginning with "s_" are private to the scanner implementation and must not be accessed.
struct judo_stream
{
#ifndef DOXYGEN
    int32_t s_at;
#endif
    int32_t where;
    int32_t length;
    enum judo_element element;
#ifndef DOXYGEN
    int8_t s_stack;
    int8_t s_state[JUDO_MAXDEPTH];
#endif
    char error[JUDO_ERRMAX];
};

#if defined(JUDO_PARSER)
typedef void *(*judo_memfunc)(void *user_data, void *ptr, size_t size);

typedef struct judo_value judo_value;
typedef struct judo_member judo_member;

enum judo_type
{
    JUDO_TYPE_NULL,
    JUDO_TYPE_BOOL,
    JUDO_TYPE_NUMBER,
    JUDO_TYPE_STRING,
    JUDO_TYPE_ARRAY,
    JUDO_TYPE_OBJECT,
};

struct judo_error
{
    int32_t where;
    int32_t length;
    char description[JUDO_ERRMAX];
};
#endif

// This is conceptually like a generator function or coroutine in that it returns values on demand.
// Pass '-1' as the input length if the input is null terminated.
enum judo_result judo_scan(struct judo_stream *stream, const char *source, int32_t length);

enum judo_result judo_stringify(const char *lexeme, int32_t length, char *buf, int32_t *buflen);

#if defined(JUDO_HAVE_FLOATS)
enum judo_result judo_numberify(const char *lexeme, int32_t length, judo_number *number);
#endif

#if defined(JUDO_PARSER)
// Parses the input into an in-memory tree. Pass '-1' as the input length
// if the input is null terminated.
enum judo_result judo_parse(const char *source, int32_t length, judo_value **root, struct judo_error *error, void *user_data, judo_memfunc memfunc);

// Pass the root value returned from judo_parse() to free the entire tree.
enum judo_result judo_free(judo_value *root, void *user_data, judo_memfunc memfunc);

enum judo_type judo_gettype(const judo_value *value);

bool judo_tobool(judo_value *value);

int32_t judo_len(const judo_value *value);

judo_value *judo_first(judo_value *value);
judo_value *judo_next(judo_value *value);

judo_member *judo_membfirst(judo_value *value);
judo_member *judo_membnext(judo_member *member);
judo_value *judo_membvalue(judo_member *member);

enum judo_result judo_name2span(const judo_member *member, int32_t *lexeme, int32_t *length);
enum judo_result judo_value2span(const judo_value *value, int32_t *lexeme, int32_t *length);
#endif

#endif
