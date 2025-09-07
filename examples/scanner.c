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

// This example scans JSON source text and prints, to stdout, each token
// on its own separate line. Numbers, strings, and member names are printed
// by lexeme. You can use 'judo_stringify' and 'judo_numberify' functions
// to obtain the escaped string and floating-point number.

// This code does not attempt to be MISRA compliant.

#include "judo.h"
#include <stdio.h>
#include <stddef.h>

char *judo_readstdin(size_t *size);

static void process_token(struct judo_stream stream, const char *json)
{
//! [scanner_process_token]
    switch (stream.token)
    {
    case JUDO_TOKEN_NULL: puts("null"); break;
    case JUDO_TOKEN_TRUE: puts("true"); break;
    case JUDO_TOKEN_FALSE: puts("false"); break;
    case JUDO_TOKEN_ARRAY_BEGIN: puts("[push]"); break;
    case JUDO_TOKEN_ARRAY_END: puts("[pop]"); break;
    case JUDO_TOKEN_OBJECT_BEGIN: puts("{push}"); break;
    case JUDO_TOKEN_OBJECT_END: puts("{pop}"); break;
    case JUDO_TOKEN_NUMBER:
        printf("number: %.*s\n", stream.where.length, &json[stream.where.offset]);
        break;
    case JUDO_TOKEN_STRING:
        printf("string: %.*s\n", stream.where.length, &json[stream.where.offset]);
        break;
    case JUDO_TOKEN_OBJECT_NAME:
        printf("{name: %.*s}\n", stream.where.length, &json[stream.where.offset]);
        break;
    default:
        break;
    }
//! [scanner_process_token]
}

int main(int argc, char *argv[])
{
//! [scanner_process_stdin]
    size_t json_len = 0;
    const char *json = judo_readstdin(&json_len);
//! [scanner_process_stdin]
    if (json == NULL)
    {
        fprintf(stderr, "error: failed to read stdin\n");
        return 2;
    }

//! [scanner_process_stream]
    struct judo_stream stream = {0};
    enum judo_result result;
    for (;;)
    {
        result = judo_scan(&stream, json, json_len);
        if (result == JUDO_RESULT_SUCCESS)
        {
            if (stream.token == JUDO_TOKEN_EOF)
            {
                break;
            }
            process_token(stream, json);
        }
        else
        {
            fprintf(stderr, "error: %s\n", stream.error);
            return 1;
        }
    }
//! [scanner_process_stream]

    return 0;
}
