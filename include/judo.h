/*
 *  Judo - Embeddable JSON and JSON5 parser.
 *  Copyright (c) 2025 Railgun Labs, LLC
 *
 *  This software is dual-licensed: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation. For the terms of this
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
    JUDO_RESULT_SUCCESS,
    JUDO_RESULT_BAD_SYNTAX,
    JUDO_RESULT_NO_BUFFER_SPACE,
    JUDO_RESULT_ILLEGAL_BYTE_SEQUENCE,
    JUDO_RESULT_OUT_OF_RANGE,
    JUDO_RESULT_INVALID_OPERATION,
    JUDO_RESULT_MAXIMUM_NESTING,
    JUDO_RESULT_OUT_OF_MEMORY,
    JUDO_RESULT_MALFUNCTION,
};

// Judo semantic tokens mark a point of interests when parsing the JSON stream.
// They may or may not correspond with a JSON token.
enum judo_token
{
    JUDO_TOKEN_INVALID,
    JUDO_TOKEN_NULL,
    JUDO_TOKEN_TRUE,
    JUDO_TOKEN_FALSE,
    JUDO_TOKEN_NUMBER,
    JUDO_TOKEN_STRING,
    JUDO_TOKEN_ARRAY_BEGIN,
    JUDO_TOKEN_ARRAY_END,
    JUDO_TOKEN_OBJECT_BEGIN,
    JUDO_TOKEN_OBJECT_END,
    JUDO_TOKEN_OBJECT_NAME,
    JUDO_TOKEN_EOF
};

// A range of UTF-8 code units in the JSON source text.
struct judo_span
{
    int32_t offset;
    int32_t length;
};

// Field names beginning with "s_" are private to the scanner implementation and must not be accessed.
struct judo_stream
{
#ifndef DOXYGEN
    int32_t s_at;
#endif
    struct judo_span where;
    enum judo_token token;
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
    JUDO_TYPE_INVALID,
    JUDO_TYPE_NULL,
    JUDO_TYPE_BOOL,
    JUDO_TYPE_NUMBER,
    JUDO_TYPE_STRING,
    JUDO_TYPE_ARRAY,
    JUDO_TYPE_OBJECT,
};

struct judo_error
{
    struct judo_span where;
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
enum judo_result judo_parse(const char *source, int32_t length, judo_value **root, struct judo_error *error, void *udata, judo_memfunc memfunc);

// Pass the root value returned from judo_parse() to free the entire tree.
enum judo_result judo_free(judo_value *root, void *udata, judo_memfunc memfunc);

enum judo_type judo_gettype(const judo_value *value);

bool judo_tobool(judo_value *value);

int32_t judo_len(judo_value *value);

judo_value *judo_first(judo_value *value);
judo_value *judo_next(judo_value *value);

judo_member *judo_membfirst(judo_value *value);
judo_member *judo_membnext(judo_member *member);
judo_value *judo_membvalue(judo_member *member);

struct judo_span judo_name2span(const judo_member *member);
struct judo_span judo_value2span(const judo_value *value);
#endif

#endif
