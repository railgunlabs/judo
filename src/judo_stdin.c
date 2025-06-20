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

// Reads input from stdin on both Windows and *nix systems. This is used
// by the command-line interface and Judo examples. This code does not
// attempt to be MISRA compliant.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

char *judo_readstdin(size_t *size)
{
    char *dynbuf = NULL;
    size_t dynbuf_length = 0;
    size_t dynbuf_capacity = 0;

#if defined(_WIN32)
    const int stdin_fd = _fileno(stdin);
#endif

    for (;;)
    {
        char buffer[4096];
#if defined(_WIN32)
        const int bytes_read = _read(stdin_fd, buffer, sizeof(buffer));
#else
        const ssize_t bytes_read = read(STDIN_FILENO, buffer, sizeof(buffer));
#endif
        if (bytes_read == 0)
        {
            break;
        }
        else if (bytes_read < 0)
        {
            free(dynbuf);
            return NULL;
        }

        const size_t buffer_length = (size_t)bytes_read;
        const size_t new_capacity = dynbuf_length + buffer_length;

        // Limit the input to 10 megabytes to avoid integer overflow elsewhere in the implementation.
        // This also ensures the buffer capacity remains under the maximum signed 32-bit integer.
        if (new_capacity >= 1024 * 1024 * 10)
        {
            fprintf(stderr, "error: input too large\n");
            free(dynbuf);
            return NULL;
        }

        if (new_capacity >= dynbuf_capacity)
        {
            char *tmpbuf = realloc(dynbuf, new_capacity);
            if (tmpbuf == NULL)
            {
                free(dynbuf);
                return NULL;
            }
            dynbuf = tmpbuf;
            dynbuf_capacity = new_capacity;
        }

        memcpy(&dynbuf[dynbuf_length], buffer, buffer_length);
        dynbuf_length += buffer_length;
    }

    *size = dynbuf_length;
    return dynbuf;
}
