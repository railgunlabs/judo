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

// This example scans JSON source text and prints, to stdout, each semantic
// token on its own separate line. Numbers, strings, and member names are
// printed by lexeme. Use 'judo_stringify' and 'judo_numberify' functions to
// obtain the escaped string and floating-point number.

// This code does not attempt to be MISRA compliant.

#include "judo.h"
#include <stdio.h>
#include <stddef.h>

char *judo_readstdin(size_t *size);

static void process_element(struct judo_stream stream, const char *json)
{
//! [scanner_process_element]
    switch (stream.element)
    {
    case JUDO_NULL: puts("null"); break;
    case JUDO_TRUE: puts("true"); break;
    case JUDO_FALSE: puts("false"); break;
    case JUDO_ARRAY_PUSH: puts("[push]"); break;
    case JUDO_ARRAY_POP: puts("[pop]"); break;
    case JUDO_OBJECT_PUSH: puts("{push}"); break;
    case JUDO_OBJECT_POP: puts("{pop}"); break;
    case JUDO_NUMBER:
        printf("number: %.*s\n", stream.length, &json[stream.where]);
        break;
    case JUDO_STRING:
        printf("string: %.*s\n", stream.length, &json[stream.where]);
        break;
    case JUDO_OBJECT_NAME:
        printf("{name: %.*s}\n", stream.length, &json[stream.where]);
        break;
    default:
        break;
    }
//! [scanner_process_element]
}

int main(int argc, char *argv[])
{
//! [scanner_process_stdin]
    size_t json_len = 0;
    const char *json = judo_readstdin(&json_len);
//! [scanner_process_stdin]
    if (json == NULL)
    {
        fprintf(stderr, "error: failed to read stdin");
        return 2;
    }

//! [scanner_process_stream]
    struct judo_stream stream = {0};
    enum judo_result result;
    for (;;)
    {
        result = judo_scan(&stream, json, json_len);
        if (result == JUDO_SUCCESS)
        {
            if (stream.element == JUDO_EOF)
            {
                break;
            }
            process_element(stream, json);
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
