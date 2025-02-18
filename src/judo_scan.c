/*
 *  Judo - Embeddable JSON and JSON5 scanner.
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

#include "judo.h"
#include "judo_utils.h"

#if defined(JUDO_HAVE_FLOATS)
#include <math.h>
#endif

#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define INVALID_CHAR UNICHAR_C(0x110000)

enum token_tag
{
    TOKEN_INVALID,
    TOKEN_EOF,
    TOKEN_NULL,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_ID,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LCURLYB,
    TOKEN_RCURLYB,
};

#define STATE_PARSE_ROOT (int8_t)0
#define STATE_FINISHED_PARSING_VALUE (int8_t)1
#define STATE_PARSE_ARRAY_END_OR_ARRAY_ELEMENT (int8_t)3
#define STATE_FINISHED_PARSING_ARRAY_ELEMENT (int8_t)4
#define STATE_PARSE_OBJECT_KEY_OR_OBJECT_END (int8_t)5
#define STATE_PARSE_OBJECT_VALUE (int8_t)6
#define STATE_FINISHED_PARSING_OBJECT_VALUE (int8_t)7
#define STATE_PARSING_ERROR (int8_t)8
#define STATE_ENCODING_ERROR (int8_t)9
#define STATE_MAX_NESTING_ERROR (int8_t)10
#define STATE_FINISHED_PARSING (int8_t)11

struct token
{
    enum token_tag type;
    int32_t lexeme;
    int32_t lexeme_length;
};

struct scanner
{
    const uint8_t *string;
    int32_t length;
    int32_t at;
    struct judo_stream *out;
};

static inline bool is_high_surrogate(unichar c)
{
    return (c >= UNICHAR_C(0xD800)) && (c <= UNICHAR_C(0xDBFF));
}

static inline bool is_low_surrogate(unichar c)
{
    return (c >= UNICHAR_C(0xDC00)) && (c <= UNICHAR_C(0xDFFF));
}

static inline bool judo_isdigit(unichar cp)
{
    return ((cp >= UNICHAR_C('0')) && (cp <= UNICHAR_C('9')));
}

static inline bool judo_isalpha(unichar cp)
{
    bool is;
    if ((cp >= UNICHAR_C('a')) && (cp <= UNICHAR_C('z')))
    {
        is = true;
    }
    else if ((cp >= UNICHAR_C('A')) && (cp <= UNICHAR_C('Z')))
    {
        is = true;
    }
    else
    {
        is = false;
    }
    return is;
}

static inline bool judo_isxdigit(unichar cp)
{
    bool is;
    if ((cp >= UNICHAR_C('a')) && (cp <= UNICHAR_C('f')))
    {
        is = true;
    }
    else if ((cp >= UNICHAR_C('A')) && (cp <= UNICHAR_C('F')))
    {
        is = true;
    }
    else
    {
        is = judo_isdigit(cp);
    }
    return is;
}

static enum judo_result bad_syntax(const struct scanner *scanner, int32_t position, int32_t length, const char *msg)
{
    struct judo_stream *stream = scanner->out;
    const size_t msglen = strlen(msg) + (size_t)1;
    assert(msglen < (size_t)JUDO_ERRMAX); // LCOV_EXCL_BR_LINE
    scanner->out->where = (struct judo_span){position, length};
    scanner->out->element = JUDO_INVALID;
    scanner->out->s_state[scanner->out->s_stack] = STATE_PARSING_ERROR;
    (void)memcpy(stream->error, msg, msglen);
    return JUDO_BAD_SYNTAX;
}

static enum judo_result bad_encoding(const struct scanner *scanner, int32_t position, int32_t length)
{
    struct judo_stream *stream = scanner->out;
    scanner->out->where = (struct judo_span){position, length};
    scanner->out->element = JUDO_INVALID;
    scanner->out->s_state[scanner->out->s_stack] = STATE_ENCODING_ERROR;
    (void)memcpy(stream->error, "malformed encoded character", 28);
    return JUDO_ILLEGAL_BYTE_SEQUENCE;
}

static enum judo_result max_nesting_depth(const struct scanner *scanner)
{
    struct judo_stream *stream = scanner->out;
    scanner->out->where = (struct judo_span){scanner->at, 1};
    scanner->out->element = JUDO_INVALID;
    scanner->out->s_state[scanner->out->s_stack] = STATE_MAX_NESTING_ERROR;
    (void)memcpy(stream->error, "maximum nesting depth exceeded", 31);
    return JUDO_MAXIMUM_NESTING;
}

static bool is_bounded(const uint8_t *string, int32_t length, int32_t position, int32_t byte_count)
{
    bool bounded = true;

    if (length < 0)
    {
        for (int32_t i = 0; i < byte_count; i++)
        {
            if ((char)string[position + i] == '\0')
            {
                bounded = false;
                break;
            }
        }
    }
    else
    {
        assert(length >= position); // LCOV_EXCL_BR_LINE
        const int32_t remaining_bytes = length - position;
        if (remaining_bytes < byte_count)
        {
            bounded = false;
        }
    }

    return bounded;
}

static unichar utf8_decode(const uint8_t *string, int32_t length, int32_t position, int32_t *byte_count)
{
    unichar cp = INVALID_CHAR;
    if (byte_count != NULL)
    {
        *byte_count = 0;
    }

    /*
     *  Lookup table for determining how many bytes are in a UTF-8 encoded sequence
     *  using only the first code unit. It is based on RFC 3629.
     *
     *  Using branches, written in pseudo code, the table looks like this:
     *
     *      if (c >= 0) and (c <= 127) return 1
     *      elif (c >= 194) and (c <= 223) return 2
     *      elif (c >= 224) and (c <= 239) return 3
     *      elif (c >= 240) and (c <= 244) return 4
     *      else return 0
     *
     *  This lookup table will return '0' for continuation bytes, overlong bytes,
     *  and bytes which do not appear in a valid UTF-8 sequence.
     */
    static const uint8_t bytes_needed_for_UTF8_sequence[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
        4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        // Defines bit patterns for masking the leading byte of a UTF-8 sequence.
        0,
        (uint8_t)0xFF, // Single byte (i.e. fits in ASCII).
        0x1F, // Two byte sequence: 110xxxxx 10xxxxxx.
        0x0F, // Three byte sequence: 1110xxxx 10xxxxxx 10xxxxxx.
        0x07, // Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
    };

    static const uint8_t next_UTF8_DFA[] = {
        0, 12, 24, 36, 60, 96, 84, 12, 12, 12, 48, 72,  // state 0
        12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, // state 1
        12, 0, 12, 12, 12, 12, 12, 0, 12, 0, 12, 12,    // state 2
        12, 24, 12, 12, 12, 12, 12, 24, 12, 24, 12, 12, // state 3
        12, 12, 12, 12, 12, 12, 12, 24, 12, 12, 12, 12, // state 4
        12, 24, 12, 12, 12, 12, 12, 12, 12, 24, 12, 12, // state 5
        12, 12, 12, 12, 12, 12, 12, 36, 12, 36, 12, 12, // state 6
        12, 36, 12, 12, 12, 12, 12, 36, 12, 36, 12, 12, // state 7
        12, 36, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, // state 8
    };

    static const uint8_t byte_to_character_class[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        8, 8, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3,
        11, 6, 6, 6, 5, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    };

    // The acceptance state for the UTF-8 DFA.
    static const uint8_t DFA_ACCEPTANCE_STATE = 0;

    // The string must be unsigned.
    const uint8_t *bytes = (const uint8_t *)&string[position];

    // Check for the END of the string.
    if ((length >= 0) && (position >= length))
    {
        cp = UNICHAR_C('\0');
    }
    else
    {
        // Lookup expected UTF-8 sequence length based on the first byte.
        int32_t seqlen = (int32_t)bytes_needed_for_UTF8_sequence[bytes[0]];

        // Verify the sequence isn't truncated by the end of the string.
        if (length < 0)
        {
            for (int32_t i = 1; i < seqlen; i++)
            {
                if (bytes[i] == UNICHAR_C('\0'))
                {
                    seqlen = 0;
                    break;
                }
            }
        }
        else
        {
            assert(length >= position); // LCOV_EXCL_BR_LINE
            const int32_t bytes_remaining = length - position;
            if (bytes_remaining < seqlen)
            {
                seqlen = 0;
            }
        }

        // Verify the UTF-8 sequence is valid.
        if (seqlen > 0)
        {
            // Consume the first UTF-8 byte.
            unichar value = (unichar)bytes[0] & (unichar)bytes_needed_for_UTF8_sequence[256 + seqlen];

            // Transition to the first DFA state.
            uint8_t state = next_UTF8_DFA[byte_to_character_class[bytes[0]]];

            // Consume the remaining UTF-8 bytes.
            for (int32_t i = 1; i < seqlen; i++)
            {
                // Mask off the next byte.
                // It's of the form 10xxxxxx if valid UTF-8.
                value = (value << UNICHAR_C(6)) | ((unichar)bytes[i] & UNICHAR_C(0x3F));

                // Transition to the next DFA state.
                state = next_UTF8_DFA[(const uint8_t)state + byte_to_character_class[bytes[i]]];
            }

            // Verify the encoded character was well-formed.
            if (state == DFA_ACCEPTANCE_STATE)
            {
                // If the string is null terminated, then substitute the null character for the special EOF character.
                if ((length < 0) && (value == UNICHAR_C('\0')))
                {
                    cp = UNICHAR_C('\0');
                }
                else
                {
                    if (byte_count != NULL)
                    {
                        *byte_count = seqlen;
                    }
                    cp = value;
                }
            }
        }
    }

    return cp;
}

static int32_t utf8_encode(unichar codepoint, char bytes[4])
{
    uint8_t *data = (uint8_t *)bytes;
    int32_t length;
    if (codepoint <= UNICHAR_C(0x7F))
    {
        data[0] = (uint8_t)codepoint;
        length = 1;
    }
    else if (codepoint <= UNICHAR_C(0x7FF))
    {
        data[0] = (uint8_t)(codepoint >> UNICHAR_C(6)) | (uint8_t)0xC0;
        data[1] = (uint8_t)(codepoint & UNICHAR_C(0x3F)) | (uint8_t)0x80;
        length = 2;
    }
    else if (codepoint <= UNICHAR_C(0xFFFF))
    {
        data[0] = (uint8_t)(codepoint >> UNICHAR_C(12)) | (uint8_t)0xE0;
        data[1] = (uint8_t)((codepoint >> UNICHAR_C(6)) & UNICHAR_C(0x3F)) | (uint8_t)0x80;
        data[2] = (uint8_t)(codepoint & UNICHAR_C(0x3F)) | (uint8_t)0x80;
        length = 3;
    }
    else
    {
        assert(codepoint <= UNICHAR_C(0x10FFFF)); // LCOV_EXCL_BR_LINE
        data[0] = (uint8_t)(codepoint >> UNICHAR_C(18)) | (uint8_t)0xF0;
        data[1] = (uint8_t)((codepoint >> UNICHAR_C(12)) & UNICHAR_C(0x3F)) | (uint8_t)0x80;
        data[2] = (uint8_t)((codepoint >> UNICHAR_C(6)) & UNICHAR_C(0x3F)) | (uint8_t)0x80;
        data[3] = (uint8_t)(codepoint & UNICHAR_C(0x3F)) | (uint8_t)0x80;
        length = 4;
    }
    return length;
}

static unichar parse_character(const char *string)
{
    unichar cp = UNICHAR_C(0x0);
    int32_t index = 0;

    // Parse hexadecimal digits.
    while (string[index] != '\0')
    {
        char digit;
        const char c = string[index];
        index += 1;

        if (c <= '9')
        {
            assert('0' <= c); // LCOV_EXCL_BR_LINE
            digit = c - '0';
        }
        else if (c <= 'F')
        {
            assert('A' <= c); // LCOV_EXCL_BR_LINE
            digit = (c - 'A') + 10;
        }
        else
        {
            assert(('a' <= c) && (c <= 'f')); // LCOV_EXCL_BR_LINE
            digit = (c - 'a') + 10;
        }

        cp = (cp * UNICHAR_C(16)) + (unichar)digit;
    }

    return cp;
}

static bool is_match(const uint8_t *string, const char *prefix, int32_t string_length)
{
    bool match = true;
    int32_t index = 0;

    while ((index < string_length) && (prefix[index] != '\0'))
    {
        if (string[index] != (uint8_t)prefix[index])
        {
            match = false;
            break;
        }
        index += 1;
    }

    if (index != string_length)
    {
        match = false;
    }

    if (prefix[index] != '\0')
    {
        match = false;
    }

    return match;
}

#if defined(JUDO_HAVE_FLOATS)
#if defined(JUDO_JSON5)
// This only parses hexidecimal numbers.
static enum judo_result json_atol(const char *string, int32_t string_length, judo_number *number)
{
    enum judo_result status;
    judo_number value = (judo_number)0.0;
    judo_number sign = (judo_number)1.0;
    int32_t index = 0;
    char c;

    // Parse sign.
    if (string[index] == '+')
    {
        index += 1;
}
    else
    {
        if ((char)string[index] == '-')
        {
            sign = (judo_number)-1.0;
            index += 1;
        }
    }

    // The sign (if present) must be followed by a '0x' or '0X'.
    // LCOV_EXCL_START
    assert(string[index] == '0');
    assert((string[index + 1] == 'x') || (string[index + 1] == 'X'));
    // LCOV_EXCL_STOP
    index += 2;

    // Parse hexadecimal digits.
    while (index < string_length)
    {
        c = string[index];
        index += 1;

        if (c <= '9')
        {
            assert('0' <= c); // LCOV_EXCL_BR_LINE
            c = c - '0';
        }
        else if (c <= 'F')
        {
            assert('A' <= c); // LCOV_EXCL_BR_LINE
            c = (c - 'A') + 10;
        }
        else
        {
            assert(('a' <= c) && (c <= 'f')); // LCOV_EXCL_BR_LINE
            c = (c - 'a') + 10;
        }

        const int32_t digit = (int32_t)c;
        value = (value * (judo_number)16.0) + (judo_number)digit;
    }

    if (value == (judo_number)INFINITY)
    {
        status = JUDO_OUT_OF_RANGE;
    }
    else
    {
        status = JUDO_SUCCESS;
    }

    *number = value * sign;
    return status;
}
#endif

// Locale independent 'atof' implementation.
static enum judo_result json_atof(const char *string, int32_t string_length, judo_number *number)
{
    enum judo_result status;
    judo_number value = (judo_number)0.0;
    judo_number sign = (judo_number)1.0;
    int32_t exponent = 0;
    int32_t index = 0;
    char ch = '\0';

#if defined(JUDO_JSON5)
    // Parse sign.
    if (string[index] == '+')
    {
        index += 1;
    }
    else
#endif
    {
        if (string[index] == '-')
        {
            sign = (judo_number)-1.0;
            index += 1;
        }
    }

    // Parse whole numbers.
    while (index < string_length)
    {
        ch = string[index];
        index += 1;

        if (!judo_isdigit((unichar)ch))
        {
            break;
        }

        const char c = ch - '0';
        const int32_t n = (int32_t)c;
        value = (value * (judo_number)10.0) + (judo_number)n;
    }

    // Parse the fractional part.
    if (ch == '.')
    {
        while (index < string_length)
        {
            ch = string[index];
            index += 1;

            if (!judo_isdigit((unichar)ch))
            {
                break;
            }

            const char c = ch - '0';
            const int32_t n = (int32_t)c;
            value = (value * (judo_number)10.0) + (judo_number)n;
            exponent = exponent - 1;
        }
    }

    // Parse scientific notation.
    if ((ch == 'e') || (ch == 'E'))
    {
        int32_t exp_sign = 1;
        int32_t exp_value = 0;

        while (index < string_length)
        {
            ch = string[index];
            index += 1;

            if (ch == '+')
            {
                ch = string[index];
                index += 1;
            }
            else
            {
                if (ch == '-')
                {
                    ch = string[index];
                    index += 1;
                    exp_sign = -1;
                }
            }

            for (;;)
            {
                const char c = ch - '0';
                const int32_t n = (int32_t)c;
                exp_value = (exp_value * 10) + (int32_t)n;
                if (index >= string_length)
                {
                    break;
                }
                ch = string[index];
                index += 1;
            }
        }

        exponent += exp_value * exp_sign;
    }

    while (exponent > 0)
    {
        value *= (judo_number)10.0;
        exponent--;
    }

    while (exponent < 0)
    {
        value *= (judo_number)0.1;
        exponent++;
    }

    if (value == (judo_number)INFINITY)
    {
        status = JUDO_OUT_OF_RANGE;
    }
    else
    {
        status = JUDO_SUCCESS;
    }

    *number = value * sign;
    return status;
}

enum judo_result judo_numberify(const char *lexeme, int32_t length, judo_number *number) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    const uint8_t *bytes = (const uint8_t *)lexeme;
    enum judo_result status = JUDO_SUCCESS;

    if (lexeme == NULL)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else if (length <= 0)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else if (number == NULL)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else
    {
        judo_number sign = (judo_number)1.0;
        int32_t ident = 0;
        int32_t ident_length = length;
        if (lexeme[ident] == '-')
        {
            sign = (judo_number)-1.0;
            ident += 1;
            ident_length -= 1;
        }
#if defined(JUDO_JSON5)
        else
        {
            if (lexeme[ident] == '+')
            {
                ident += 1;
                ident_length -= 1;
            }
        }
#endif

#if defined(JUDO_JSON5)
        if (is_match(&bytes[ident], "NaN", ident_length))
        {
            *number = (judo_number)NAN;
        }
        else if (is_match(&bytes[ident], "Infinite", ident_length))
        {
            *number = sign * (judo_number)INFINITY;
        }
        else if ((strncmp(&lexeme[ident], "0x", 2) == 0) ||
                 (strncmp(&lexeme[ident], "0X", 2) == 0))
        {
            status = json_atol(lexeme, length, number);
        }
        else
#endif
        {
            status = json_atof(lexeme, length, number);
        }
    }

    return status;
}
#endif

#if defined(JUDO_JSON5) || defined(JUDO_WITH_COMMENTS)
static int32_t is_newline(const uint8_t *string, int32_t length, int32_t position)
{
    int32_t byte_count = 0;
    
    if (is_bounded(string, length, position, 2))
    {
        if (is_match(&string[position], "\r\n", 2))
        {
            byte_count = 2;
        }
    }

    if (byte_count == 0)
    {
        if (is_bounded(string, length, position, 1))
        {
            switch (utf8_decode(string, length, position, &byte_count))
            {
            case 0x000A: // Line feed
            case 0x000D: // Carriage return
            case 0x2028: // Line separator
            case 0x2029: // Paragraph separator
                break;
            default:
                byte_count = 0;
                break;
            }
        }
    }

    return byte_count;
}
#endif

#if defined(JUDO_JSON5)
static enum judo_result scan_number(const struct scanner *scanner, struct token *token)
{
    enum judo_result result = JUDO_SUCCESS;
    int32_t current = scanner->at;
    unichar sign = UNICHAR_C(0);
    bool has_decimal = false;
    unichar cp = scanner->string[current];

    // Consume the sign.
    if (cp == UNICHAR_C('-'))
    {
        sign = UNICHAR_C('-');
        current++;
    }
    else
    {
        if (cp == UNICHAR_C('+'))
        {
            sign = UNICHAR_C('+');
            current++;
        }
    }

    // [0-9]+
    cp = utf8_decode(scanner->string, scanner->length, current, NULL);
    if (judo_isdigit(cp))
    {
        if (is_bounded(scanner->string, scanner->length, current, 2))
        {
            // Special case: JSON5 allows hexadecimal numbers.
            if (is_match(&scanner->string[current], "0x", 2) ||
                is_match(&scanner->string[current], "0X", 2))
            {
                current += 2;

                cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                if (!judo_isxdigit(cp))
                {
                    result = bad_syntax(scanner, current, 1, "expected hexadecimal number");
                }

                for (;;)
                {
                    cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                    if (!judo_isxdigit(cp))
                    {
                        break;
                    }
                    current += 1; // Consume hexadecimal digits.
                }

                token->type = TOKEN_NUMBER;
                token->lexeme_length = (int32_t)(current - scanner->at);
            }
        }

        if (token->type != TOKEN_NUMBER)
        {
            // Consume the first integer digit.
            current += 1;

            // We've now read in one digit.
            const unichar first_digit = cp;
            int32_t digit_count = 1;

            // Consume remaining integer digits.
            for (;;)
            {
                cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                if (!judo_isdigit(cp))
                {
                    break;
                }
                current += 1;
                digit_count += 1;
            }

            if ((digit_count > 1) && (first_digit == UNICHAR_C('0')))
            {
                result = bad_syntax(scanner, scanner->at, current - scanner->at, "illegal octal number");
            }
        }
    }
    else if (judo_isalpha(cp)) // Special case: JSON5 allows NaN and Infinite.
    {
        int32_t id_start = current;
        for (;;)
        {
            cp = utf8_decode(scanner->string, scanner->length, current, NULL);
            if (!judo_isalpha(cp))
            {
                break;
            }
            current += 1;
        }

        const int32_t id_length = current - id_start;
        if (!is_match(&scanner->string[id_start], "NaN", id_length) &&
            !is_match(&scanner->string[id_start], "Infinite", id_length))
        {
            result = bad_syntax(scanner, id_start, id_length, "expected NaN or Infinite");
        }

        token->type = TOKEN_NUMBER;
        token->lexeme_length = (int32_t)(current - scanner->at);
    }
    else
    {
        // No action to take.
    }
    
    if ((result == JUDO_SUCCESS) && (token->type == TOKEN_INVALID))
    {
        // '.'
        if (cp == UNICHAR_C('.'))
        {
            has_decimal = true;
            current++; // Consume '.'

            // Consume remaining digits.
            for (;;)
            {
                cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                if (!judo_isdigit(cp))
                {
                    break;
                }
                current += 1;
            }
        }

        // JSON5 allows numbers to begin and end with a trailing decimal point.
        // Make sure a number was parsed and we didn't just receive a sign or decimal point by themselves.
        int32_t digit_count = current - scanner->at;
        if (sign != UNICHAR_C(0))
        {
            digit_count -= 1; // One of the characters is a sign.
        }
        if (has_decimal)
        {
            digit_count -= 1; // One of the characters is a decimal point.
        }
        if (digit_count == 0)
        {
            result = bad_syntax(scanner, current, 1, "expected number");
        }

        // ['e' | 'E']
        if ((cp == UNICHAR_C('e')) || (cp == UNICHAR_C('E')))
        {
            current++; // Consume 'e'.
            cp = utf8_decode(scanner->string, scanner->length, current, NULL);

            // ['+' | '-']?
            if ((cp == UNICHAR_C('+')) || (cp == UNICHAR_C('-')))
            {
                current++; // Consume +/-.
                cp = utf8_decode(scanner->string, scanner->length, current, NULL);
            }

            // [0-9]+
            if (!judo_isdigit(cp))
            {
                result = bad_syntax(scanner, current, 1, "missing exponent");
            }

            // Consume exponent.
            for (;;)
            {
                cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                if (!judo_isdigit(cp))
                {
                    break;
                }
                current += 1;
            }
        }

        if (result == JUDO_SUCCESS)
        {
            token->type = TOKEN_NUMBER;
            token->lexeme_length = (int32_t)(current - scanner->at);
        }
    }

    return result;
}
#else
static enum judo_result scan_number(const struct scanner *scanner, struct token *token)
{
    enum judo_result result = JUDO_SUCCESS;
    int32_t current = scanner->at;
    unichar cp = scanner->string[current];

    // Consume the sign.
    if (cp == UNICHAR_C('-'))
    {
        current++;
    }

    // [0-9]+
    cp = utf8_decode(scanner->string, scanner->length, current, NULL);
    if (!judo_isdigit(cp))
    {
        result = bad_syntax(scanner, current, 1, "expected number");
    }
    else
    {
        // Consume the first integer digit.
        current += 1;

        // We've now read in one digit.
        const unichar first_digit = cp;
        int32_t digits = 1;

        // Consume remaining integer digits.
        for (;;)
        {
            cp = utf8_decode(scanner->string, scanner->length, current, NULL);
            if (!judo_isdigit(cp))
            {
                break;
            }
            current += 1;
            digits += 1;
        }

        if ((digits > 1) && (first_digit == UNICHAR_C('0')))
        {
            result = bad_syntax(scanner, scanner->at, current - scanner->at, "illegal octal number");
        }
        else
        {
            // '.'
            if (cp == UNICHAR_C('.'))
            {
                current++; // Consume '.'

                // Consume remaining digits.
                digits = 0;
                for (;;)
                {
                    cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                    if (!judo_isdigit(cp))
                    {
                        break;
                    }
                    current += 1;
                    digits += 1;
                }

                // Check for a decimal without fraction digits.
                if (digits == 0)
                {
                    result = bad_syntax(scanner, scanner->at, current - scanner->at, "expected fractional part");
                }
            }

            // ['e' | 'E']
            if ((cp == UNICHAR_C('e')) || (cp == UNICHAR_C('E')))
            {
                current++; // Consume 'e'.
                cp = utf8_decode(scanner->string, scanner->length, current, NULL);

                // ['+' | '-']?
                if ((cp == UNICHAR_C('+')) || (cp == UNICHAR_C('-')))
                {
                    current++; // Consume +/-.
                    cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                }

                // [0-9]+
                if (!judo_isdigit(cp))
                {
                    result = bad_syntax(scanner, current, 1, "missing exponent");
                }

                // Consume exponent.
                for (;;)
                {
                    cp = utf8_decode(scanner->string, scanner->length, current, NULL);
                    if (!judo_isdigit(cp))
                    {
                        break;
                    }
                    current += 1;
                }
            }

            if (result == JUDO_SUCCESS)
            {
                token->type = TOKEN_NUMBER;
                token->lexeme_length = (int32_t)(current - scanner->at);
            }
        }
    }
   
    return result;
}
#endif

static enum judo_result scan_string(const struct scanner *scanner, struct token *token)
{
    enum judo_result status = JUDO_SUCCESS;
    const uint8_t *string = scanner->string;
    const uint8_t quote_char = scanner->string[scanner->at];
    int32_t current = scanner->at;
    unichar codepoint;

    current += 1; // consume opening quote
    
    // Loop until the closing quote is encountered or EOF.
    while (is_bounded(string, scanner->length, current, 1) && (status == JUDO_SUCCESS))
    {
        // Check for characters that MUST be escaped.
        if (string[current] <= (uint8_t)0x001F)
        {
            status = bad_syntax(scanner, current, 1, "unescaped control character");
        }
        // Check for escape sequence.
        else if (string[current] == (uint8_t)0x5C)
        {
            const int32_t escape_start = current;
            current += 1; // consume backslash

            if (is_bounded(string, scanner->length, current, 1))
            {
                int32_t byte_count;

#if defined(JUDO_JSON5)
                // Strings with a backslash followed by a new line character
                // continue to the next line.
                byte_count = is_newline(string, scanner->length, current);
                if (byte_count >= 1)
                {
                    current += byte_count;
                    continue;
                }
#else
                byte_count = 0;
#endif

                char digits[5] = {'\0', '\0', '\0', '\0', '\0'};
                int32_t digit_count = 0;

                switch ((char)string[current])
                {
                case '"': case '\\': case '/': case 'b':
                case 'f': case 'n': case 'r': case 't':
#if defined(JUDO_JSON5)
                case '\'': case 'v': case '0':
#endif
                    current += 1; // consume escape character
                    break;

#if defined(JUDO_JSON5)
                case 'x':
                    current += 1; // consume 'x'
                    while (is_bounded(string, scanner->length, current, 1))
                    {
                        if (!judo_isxdigit(string[current]) || (digit_count == 2))
                        {
                            break;
                        }
                        digit_count += 1;
                        current += 1;
                    }
                    if (digit_count < 2)
                    {
                        status = bad_syntax(scanner, escape_start, current - escape_start, "expected two hex digits");
                    }
                    break;
#endif

                case 'u':
                    current += 1; // consume 'u'
                    while (is_bounded(string, scanner->length, current, 1))
                    {
                        if (!judo_isxdigit(string[current]) || (digit_count == 4))
                        {
                            break;
                        }
                        digits[digit_count] = (char)string[current];
                        digit_count += 1;
                        current += 1;
                    }

                    if (digit_count < 4)
                    {
                        status = bad_syntax(scanner, escape_start, current - escape_start, "expected four hex digits");
                    }
                    else
                    {
                        codepoint = parse_character(digits);
                        if (is_high_surrogate(codepoint))
                        {
                            const int32_t escape_end = current;

                            (void)memset(digits, 0, sizeof(digits));
                            digit_count = 0;

                            // There needs to be a '\u' followed by four hex digits.
                            if (is_bounded(string, scanner->length, current, 6))
                            {
                                // Low surrogates must be followed by high surrogates.
                                if (is_match(&string[current], "\\u", 2))
                                {
                                    current += 2; // skip the '\u' sequence
                                    while (judo_isxdigit(string[current]) && (digit_count < 4))
                                    {
                                        digits[digit_count] = (char)string[current];
                                        digit_count += 1;
                                        current += 1;
                                    }

                                    if (digit_count == 4)
                                    {
                                        codepoint = parse_character(digits);
                                    }
                                }
                            }
                            
                            if (!is_low_surrogate(codepoint))
                            {
                                status = bad_syntax(scanner, escape_start, escape_end - escape_start, "unmatched surrogate pair");
                            }
                        }
                        else
                        {
                            if (is_low_surrogate(codepoint))
                            {
                                status = bad_syntax(scanner, escape_start, current - escape_start, "unmatched surrogate pair");
                            }
                        }
                    }
                    break;

                default:
                    (void)utf8_decode(string, scanner->length, current, &byte_count);
                    current += byte_count;
                    status = bad_syntax(scanner, escape_start, current - escape_start, "invalid escape sequence");
                    break;
                }
            }
        }
        // Check for the closing quote, but be sure its not an escaped closing quote.
        else if (string[current] == quote_char)
        {
            current += 1; // Consume closing quote.
            token->type = TOKEN_STRING;
            token->lexeme_length = (int32_t)(current - scanner->at);
            break;
        }
        else
        {
            // Consume a UTF-8 code point.
            int32_t byte_count;
            codepoint = utf8_decode(string, scanner->length, current, &byte_count);
            if (codepoint == INVALID_CHAR)
            {
                status = bad_encoding(scanner, current, 1);
            }
            else
            {
                current += byte_count;
            }
        }
    }
    
    // If no errors occurred, but the end of the string was not found, then report an error.
    if ((status == JUDO_SUCCESS) && (token->type == TOKEN_INVALID))
    { 
        status = bad_syntax(scanner, scanner->at, 1, "unclosed string");
    }

    return status;
}

struct bytebuf
{
    int32_t written;
    int32_t length;
    int32_t capacity;
    char *dest;
};

static void write_bytes(struct bytebuf *b, unichar cp)
{
    char bytes[4];
    int32_t bytes_needed = utf8_encode(cp, bytes);

    if ((b->length + bytes_needed) <= b->capacity)
    {
        // LCOV_EXCL_START
        assert(b->dest != NULL);
        assert(b->capacity > 0);
        // LCOV_EXCL_STOP
        (void)memcpy(&b->dest[b->length], bytes, (size_t)bytes_needed);
        b->written += bytes_needed;
    }

    b->length += bytes_needed;
}

enum judo_result judo_stringify(const char *lexeme, int32_t length, char *buf, int32_t *buflen) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    enum judo_result status = JUDO_SUCCESS;
    unichar codepoint = UNICHAR_C(0x0);
    struct bytebuf out = {
        .written = 0,
        .length = 0,
        .capacity = 0,
        .dest = buf,
    };

    if (lexeme == NULL)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else if (buflen == NULL)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else if (*buflen < 0)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else if ((buf == NULL) && (*buflen != 0))
    {
        status = JUDO_INVALID_OPERATION;
    }
    else if (length <= 0)
    {
        status = JUDO_INVALID_OPERATION;
    }
    else
    {
        const char *string = lexeme;
        out.capacity = *buflen;

#if defined(JUDO_JSON5)
        if ((string[0] == '"') || (string[0] == '\''))
#endif
        {
            char buffer[5];
            unichar low;
            int32_t curr = 1;
            int32_t stop = length - 1;
            while ((status == JUDO_SUCCESS) && (curr < stop))
            {
                if (string[curr] == '\\')
                {
                    curr++; // skip the backslash

#if defined(JUDO_JSON5)
                    // Strings with a backslash followed by a new line character
                    // continue to the next line.
                    const int32_t byte_count = is_newline((const uint8_t *)string, length, curr);
                    if (byte_count >= 1)
                    {
                        curr += byte_count;
                        continue;
                    }
#endif

                    switch (string[curr++])
                    {
                    case '"': write_bytes(&out, UNICHAR_C('"')); break;
                    case '\\': write_bytes(&out, UNICHAR_C('\\')); break;
                    case '/': write_bytes(&out, UNICHAR_C('/')); break;
                    case 'b': write_bytes(&out, UNICHAR_C('\b')); break;
                    case 'f': write_bytes(&out, UNICHAR_C('\f')); break;
                    case 'n': write_bytes(&out, UNICHAR_C('\n')); break;
                    case 'r': write_bytes(&out, UNICHAR_C('\r')); break;
                    case 't': write_bytes(&out, UNICHAR_C('\t')); break;
#if defined(JUDO_JSON5)
                    case '\'': write_bytes(&out, UNICHAR_C('\'')); break;
                    case 'v': write_bytes(&out, UNICHAR_C('\v')); break;
                    case '0': write_bytes(&out, UNICHAR_C('\0')); break;
                    case 'x':
                        // JSON5 allows Basic Latin or Latin-1 Supplement Unicode character ranges (U+0000 through U+00FF)
                        // to be expressed using a backslash 'x' followed by two hexadecimal digits.
                        buffer[0] = string[curr];
                        buffer[1] = string[curr + 1];
                        buffer[2] = '\0';
                        codepoint = parse_character(buffer);
                        write_bytes(&out, codepoint);
                        curr += 2;
                        break;
#endif

                    case 'u':
                        buffer[0] = string[curr];
                        buffer[1] = string[curr + 1];
                        buffer[2] = string[curr + 2];
                        buffer[3] = string[curr + 3];
                        buffer[4] = '\0';
                        codepoint = parse_character(buffer);
                        curr += 4;

                        if (is_high_surrogate(codepoint))
                        {
                            // Low surrogates must be followed by high surrogates.
                            curr += 2; // skip the '\u' escape sequence
                            buffer[0] = string[curr];
                            buffer[1] = string[curr + 1];
                            buffer[2] = string[curr + 2];
                            buffer[3] = string[curr + 3];
                            buffer[4] = '\0';
                            low = parse_character(buffer);
                            curr += 4;

                            // Create a code point from the the high and low surrogates.
                            codepoint = (codepoint << UNICHAR_C(10)) + (unichar)low + 0xFCA02400u;
                        }

                        write_bytes(&out, codepoint);
                        break;

                    default:
                        status = JUDO_MALFUNCTION;
                        break;
                    }
                }
                else
                {
                    // Consume a UTF-8 code point.
                    int32_t byte_count;
                    codepoint = utf8_decode((const uint8_t *)string, length, curr, &byte_count);
                    write_bytes(&out, codepoint);
                    curr += byte_count;
                }
            }
        }
#if defined(JUDO_JSON5)
        else
        {
            assert(status == JUDO_SUCCESS); // LCOV_EXCL_BR_LINE

            int32_t curr = 0;
            int32_t stop = length;
            while (curr < stop)
            {
                if (string[curr] == '\\')
                {
                    assert(string[curr + 1] == 'u'); // LCOV_EXCL_BR_LINE

                    char buffer[5];
                    buffer[0] = string[curr + 2];
                    buffer[1] = string[curr + 3];
                    buffer[2] = string[curr + 4];
                    buffer[3] = string[curr + 5];
                    buffer[4] = '\0';

                    codepoint = parse_character(buffer);
                    curr += 6;

                    write_bytes(&out, codepoint);
                }
                else
                {
                    int32_t byte_count;
                    codepoint = utf8_decode((const uint8_t *)string, length, curr, &byte_count);
                    write_bytes(&out, codepoint);
                    curr += byte_count;
                }
            }
        }
#endif
    }

    if (status == JUDO_SUCCESS)
    {
        // If the output buffer is NULL, then record how many bytes are needed in the fully escaped string.
        // Otherwise, record how many bytes were actually written.
        if (buf == NULL)
        {
            assert(*buflen == 0); // LCOV_EXCL_BR_LINE
            *buflen = out.length;
        }
        else
        {
            if (out.length > out.capacity)
            {
                status = JUDO_NO_BUFFER_SPACE;
            }
            *buflen = out.written;
        }
    }

    return status;
}

static bool is_start(unichar c)
{
    bool s;

    if (judo_isalpha(c))
    {
        s = true;
    }
#if defined(JUDO_JSON5)
    else if ((c == UNICHAR_C('$')) || (c == UNICHAR_C('_')))
    {
        s = true;
    }
#endif
    else
    {
        s = false;
    }

    return s;
}

static bool is_continue(unichar c)
{
    bool s;

    if (is_start(c) || judo_isdigit(c))
    {
        s = true;
    }
    else
    {
        s = false;
    }

    return s;
}

static void scan_keyword(const struct scanner *scanner, struct token *token)
{
    const uint8_t *string = &scanner->string[scanner->at];
    int32_t current = scanner->at;

    int32_t byte_count = 0;
    unichar cp = utf8_decode(scanner->string, scanner->length, current, &byte_count);
    if (is_start(cp))
    {
        current += byte_count;

        for (;;)
        {
            cp = utf8_decode(scanner->string, scanner->length, current, &byte_count);
            if (!is_continue(cp))
            {
                break;
            }
            current += byte_count;
        }

        int32_t token_length = current - scanner->at;
        if (is_match(string, "null", token_length))
        {
            token->type = TOKEN_NULL;
            token->lexeme_length = token_length;
        }
        else if (is_match(string, "true", token_length))
        {
            token->type = TOKEN_TRUE;
            token->lexeme_length = token_length;
        }
        else if (is_match(string, "false", token_length))
        {
            token->type = TOKEN_FALSE;
            token->lexeme_length = token_length;
        }
#if defined(JUDO_JSON5)
        else if (is_match(string, "NaN", token_length) ||
                 is_match(string, "Infinite", token_length))
        {
            token->type = TOKEN_NUMBER;
            token->lexeme_length = token_length;
        }
#endif
        else
        {
            token->type = TOKEN_INVALID;
            token->lexeme_length = 0;
        }
    }
}

#if defined(JUDO_JSON5)
static enum judo_result scan_unicode_escape(const struct scanner *scanner, int32_t position)
{
    enum judo_result status = JUDO_SUCCESS;
    int32_t current = position;
    current += 1; // skip the backslash

    // There needs to be at least 5 more characters have the slash: the 'u' character and four hex digits.
    if (!is_bounded(scanner->string, scanner->length, current, 5))
    {
        status = bad_syntax(scanner, position, 1, "expected Unicode escape sequence");
    }
    else if ((char)scanner->string[current] != 'u')
    {
        status = bad_syntax(scanner, position, 2, "expected 'u' after backslash");
    }
    else
    {
        current += 1; // skip the 'u'

        int32_t digit_count = 0;
        while (judo_isxdigit(scanner->string[current]) && (digit_count < 4))
        {
            digit_count += 1;
            current += 1;
        }

        if (digit_count < 4)
        {
            status = bad_syntax(scanner, position, current, "expected four hex digits");
        }
    }

    return status;
}

static enum judo_result scan_ES5_identifier(const struct scanner *scanner, struct token *token)
{
    enum judo_result status = JUDO_SUCCESS;
    const uint8_t *string = &scanner->string[scanner->at];
    int32_t current = scanner->at;

    int32_t byte_count = 0;
    unichar cp = utf8_decode(scanner->string, scanner->length, current, &byte_count);
    if (((judo_uniflags(cp) & ID_START) == ID_START) || (cp == UNICHAR_C('\\')))
    {
        // Special case: The identifier begins with a Unicode escape sequence.
        if (cp == UNICHAR_C('\\'))
        {
            status = scan_unicode_escape(scanner, current);
            byte_count = 6;
        }
        current += byte_count;

        while (status == JUDO_SUCCESS)
        {
            cp = utf8_decode(scanner->string, scanner->length, current, &byte_count);
            if (cp == UNICHAR_C('\\'))
            {
                status = scan_unicode_escape(scanner, current);
                byte_count = 6;
            }
            else
            {
                if ((judo_uniflags(cp) & ID_EXTEND) != ID_EXTEND)
                {
                    break;
                }
            }
            current += byte_count;
        }

        if (status == JUDO_SUCCESS)
        {
            // JSON5 requires that object keys may be an ECMAScript 5.1 IdentifierName.
            int32_t token_length = current - scanner->at;
            switch ((char)string[0])
            {
            case 'b':
                if (is_match(string, "break", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'c':
                if (is_match(string, "case", token_length) ||
                    is_match(string, "catch", token_length) ||
                    is_match(string, "const", token_length) ||
                    is_match(string, "class", token_length) ||
                    is_match(string, "continue", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'd':
                if (is_match(string, "do", token_length) ||
                    is_match(string, "delete", token_length) ||
                    is_match(string, "default", token_length) ||
                    is_match(string, "debugger", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'e':
                if (is_match(string, "else", token_length) ||
                    is_match(string, "enum", token_length) ||
                    is_match(string, "export", token_length) ||
                    is_match(string, "extends", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'f':
                if (is_match(string, "for", token_length) ||
                    is_match(string, "finally", token_length) ||
                    is_match(string, "function", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'i':
                if (is_match(string, "if", token_length) ||
                    is_match(string, "in", token_length) ||
                    is_match(string, "import", token_length) ||
                    is_match(string, "interface", token_length) ||
                    is_match(string, "implements", token_length) ||
                    is_match(string, "instanceof", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'l':
                if (is_match(string, "let", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'n':
                if (is_match(string, "new", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'p':
                if (is_match(string, "public", token_length) ||
                    is_match(string, "package", token_length) ||
                    is_match(string, "private", token_length) ||
                    is_match(string, "protected", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'r':
                if (is_match(string, "return", token_length))
                {
                    token_length = 0;
                }
                break;

            case 's':
                if (is_match(string, "super", token_length) ||
                    is_match(string, "static", token_length) ||
                    is_match(string, "switch", token_length))
                {
                    token_length = 0;
                }
                break;

            case 't':
                if (is_match(string, "try", token_length) ||
                    is_match(string, "this", token_length) ||
                    is_match(string, "throw", token_length) ||
                    is_match(string, "typeof", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'v':
                if (is_match(string, "var", token_length) ||
                    is_match(string, "void", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'w':
                if (is_match(string, "with", token_length) ||
                    is_match(string, "while", token_length))
                {
                    token_length = 0;
                }
                break;

            case 'y':
                if (is_match(string, "yield", token_length))
                {
                    token_length = 0;
                }
                break;

            default:
                // No action.
                break;
            }

            if (token_length > 0)
            {
                token->type = TOKEN_ID;
                token->lexeme_length = token_length;
            }
            else
            {
                status = bad_syntax(scanner, scanner->at, current - scanner->at, "reserved word");
            }
        }
    }

    return status;
}
#endif

#if defined(JUDO_JSON5) || defined(JUDO_WITH_COMMENTS)
static enum judo_result scan_comment(const struct scanner *scanner, int32_t *byte_count)
{
    int32_t current = scanner->at + 2; // +2 to skip the '/' and '/'

    while (is_newline(scanner->string, scanner->length, current) == 0)
    {
        int32_t seqlen = 0;
        (void)utf8_decode(scanner->string, scanner->length, current, &seqlen);
        if (seqlen == 0)
        {
            // Either a malformed character OR end-of-file was found.
            // In either case, let the main tokenization switch handle it.
            break;
        }
        current += seqlen;
    }

    *byte_count = current - scanner->at;
    return JUDO_SUCCESS;
}

static enum judo_result scan_multiline_comment(const struct scanner *scanner, int32_t *byte_count)
{
    enum judo_result result;
    int32_t current = scanner->at + 2; // +2 to skip the '/' and '*'
    int32_t seqlen = 0;
    unichar cp = 0x0;

    *byte_count = 0;

    do
    {
        if (is_bounded(scanner->string, scanner->length, current, 2))
        {
            if (is_match(&scanner->string[current], "*/", 2))
            {
                current += 2;
                *byte_count = current - scanner->at;
                break;
            }
        }

        // Decode the character in the comment.
        cp = utf8_decode(scanner->string, scanner->length, current, &seqlen);
        current += seqlen;
    } while (seqlen > 0);

    if (cp == INVALID_CHAR)
    {
        result = bad_encoding(scanner, current, 1);
    }
    else if (*byte_count == 0)
    {
        result = bad_syntax(scanner, scanner->at, 2, "unterminated multi-line comment");
    }
    else
    {
        result = JUDO_SUCCESS;
    }

    return result;
}
#endif

static bool is_space(unichar cp)
{
    bool b;

    switch (cp)
    {
        case 0x0020: // Space
        case 0x0009: // Horizontal tab
        case 0x000A: // Line feed
        case 0x000D: // Carriage return
#if defined(JUDO_JSON5)
        case 0x000B: // Vertical tab
        case 0x000C: // Form feed
        case 0x00A0: // Non-breaking space
        case 0x2028: // Line separator
        case 0x2029: // Paragraph separator
#endif
            b = true;
            break;

        default:
            b = false;
#if defined(JUDO_JSON5)
            if ((judo_uniflags(cp) & IS_SPACE) == IS_SPACE)
            {
                b = true;
            }
#endif
            break;
    }

    return b;
}

static enum judo_result consume_space_and_comments(struct scanner *scanner)
{
    enum judo_result status = JUDO_SUCCESS;

    // Consume all the whitespace characters leading up to the token.
    for (;;)
    {
        int32_t byte_count = 0;
        const unichar codepoint = utf8_decode(scanner->string, scanner->length, scanner->at, &byte_count);
        if (!is_space(codepoint))
        {
            byte_count = 0;

#if defined(JUDO_WITH_COMMENTS) || defined(JUDO_JSON5)
            if (is_bounded(scanner->string, scanner->length, scanner->at, 2))
            {
                if (is_match(&scanner->string[scanner->at], "//", 2))
                {
                    status = scan_comment(scanner, &byte_count);
                }
                else if (is_match(&scanner->string[scanner->at], "/*", 2))
                {
                    status = scan_multiline_comment(scanner, &byte_count);
                }
                else
                {
                    // No action.
                }
            }
#endif
        }

        if (byte_count == 0)
        {
            break;
        }

        scanner->at += byte_count;
    }

    return status;
}

static enum judo_result peek(struct scanner *scanner, struct token *token)
{
    enum judo_result status;

    assert(token != NULL); // LCOV_EXCL_BR_LINE
    (void)memset(token, 0, sizeof(*token));
    
    // Consume all white space and comments.
    status = consume_space_and_comments(scanner);

#if defined(JUDO_WITH_COMMENTS) || defined(JUDO_JSON5)
    if (status == JUDO_SUCCESS)
#endif
    {
        token->type = TOKEN_INVALID;
        token->lexeme = scanner->at;

        int32_t byte_count = 0;
        const unichar codepoint = utf8_decode(scanner->string, scanner->length, scanner->at, &byte_count);
        switch (codepoint)
        {
        case INVALID_CHAR:
            status = bad_encoding(scanner, scanner->at, 1);
            break;

        case UNICHAR_C('\0'):
            if (byte_count > 0)
            {
                status = bad_syntax(scanner, scanner->at, 1, "unexpected null byte");
            }
            else
            {
                token->type = TOKEN_EOF;
                token->lexeme_length = 0;
            }
            break;

#if defined(JUDO_JSON5)
        case UNICHAR_C('.'):
        case UNICHAR_C('+'):
#endif
        case UNICHAR_C('-'):
        case UNICHAR_C('0'): case UNICHAR_C('1'): case UNICHAR_C('2'): case UNICHAR_C('3'): case UNICHAR_C('4'):
        case UNICHAR_C('5'): case UNICHAR_C('6'): case UNICHAR_C('7'): case UNICHAR_C('8'): case UNICHAR_C('9'):
            status = scan_number(scanner, token);
            break;

        case UNICHAR_C('"'):
#if defined(JUDO_JSON5)
        case UNICHAR_C('\''):
#endif
            status = scan_string(scanner, token);
            break;

        case UNICHAR_C(','):
            token->type = TOKEN_COMMA;
            token->lexeme_length = 1;
            break;

        case UNICHAR_C(':'):
            token->type = TOKEN_COLON;
            token->lexeme_length = 1;
            break;

        case UNICHAR_C('['):
            token->type = TOKEN_LBRACE;
            token->lexeme_length = 1;
            break;

        case UNICHAR_C(']'):
            token->type = TOKEN_RBRACE;
            token->lexeme_length = 1;
            break;

        case UNICHAR_C('{'):
            token->type = TOKEN_LCURLYB;
            token->lexeme_length = 1;
            break;

        case UNICHAR_C('}'):
            token->type = TOKEN_RCURLYB;
            token->lexeme_length = 1;
            break;

        default:
            scan_keyword(scanner, token);
#if defined(JUDO_JSON5)
            if (token->type == TOKEN_INVALID)
            {
                status = scan_ES5_identifier(scanner, token);
            }
            if (status == JUDO_SUCCESS)
#endif
            {
                if (token->type == TOKEN_INVALID)
                {
                    status = bad_syntax(scanner, scanner->at, byte_count, "unrecognized token");
                }
            }
            break;
        }
    }

    return status;
}

static enum judo_result accept(struct scanner *scanner, enum token_tag tag, bool *accepted)
{
    struct token token;
    enum judo_result status = peek(scanner, &token);

    if (token.type == tag)
    {
        scanner->at += token.lexeme_length;
        *accepted = true;
    }
    else
    {
        *accepted = false;
    }

    return status;
}

static inline void eat(struct scanner *scanner, const struct token *token)
{
    scanner->at += token->lexeme_length;
}

static enum judo_result parse_null(struct scanner *scanner, const struct token *token)
{
    assert(token->type == TOKEN_NULL); // LCOV_EXCL_BR_LINE
    eat(scanner, token);
    scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
    scanner->out->element = JUDO_NULL;
    scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
    return JUDO_SUCCESS;
}

static enum judo_result parse_bool(struct scanner *scanner, const struct token *token)
{
    // LCOV_EXCL_START
    assert(
        (token->type == TOKEN_TRUE) ||
        (token->type == TOKEN_FALSE)
    );
    // LCOV_EXCL_STOP
    eat(scanner, token);
    scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
    scanner->out->element = (token->type == TOKEN_TRUE) ? JUDO_TRUE : JUDO_FALSE;
    scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
    return JUDO_SUCCESS;
}

static enum judo_result parse_number(struct scanner *scanner, const struct token *token)
{
    assert(token->type == TOKEN_NUMBER); // LCOV_EXCL_BR_LINE
    eat(scanner, token);
    scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
    scanner->out->element = JUDO_NUMBER;
    scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
    return JUDO_SUCCESS;
}

static enum judo_result parse_string(struct scanner *scanner, const struct token *token)
{
    assert(token->type == TOKEN_STRING); // LCOV_EXCL_BR_LINE
    eat(scanner, token);
    scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
    scanner->out->element = JUDO_STRING;
    scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
    return JUDO_SUCCESS;
}

static enum judo_result parse_value(struct scanner *scanner, const char *msg);

static enum judo_result parse_array(struct scanner *scanner, const struct token *token)
{
    assert(token->type == TOKEN_LBRACE); // LCOV_EXCL_BR_LINE
    eat(scanner, token);
    scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
    scanner->out->element = JUDO_ARRAY_PUSH;
    scanner->out->s_state[scanner->out->s_stack] = STATE_PARSE_ARRAY_END_OR_ARRAY_ELEMENT;
    return JUDO_SUCCESS;
}

static enum judo_result parse_array_element(struct scanner *scanner)
{
    // After the array element has been parsed, we should check for a comma.
    scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_ARRAY_ELEMENT;
    return parse_value(scanner, "expected value");
}

static enum judo_result parse_array_element_or_array_end(struct scanner *scanner)
{
    struct token token = {0};
    enum judo_result status = peek(scanner, &token);
    if (status == JUDO_SUCCESS)
    {
        if (token.type == TOKEN_RBRACE)
        {
            eat(scanner, &token);
            scanner->out->where = (struct judo_span){token.lexeme, token.lexeme_length};
            scanner->out->element = JUDO_ARRAY_POP;
            scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
        }
        else
        {
            status = parse_array_element(scanner);
        }
    }
    return status;
}

static enum judo_result finished_parsing_array_element(struct scanner *scanner)
{
    struct token token = {0};
    enum judo_result status = peek(scanner, &token);
    if (status == JUDO_SUCCESS)
    {
        if (token.type == TOKEN_COMMA)
        {
            eat(scanner, &token);
#if defined(JUDO_WITH_TRAILING_COMMAS) || defined(JUDO_JSON5)
            status = parse_array_element_or_array_end(scanner);
#else
            status = parse_array_element(scanner);
#endif
        }
        else
        {
            if (token.type == TOKEN_RBRACE)
            {
                eat(scanner, &token);
                scanner->out->where = (struct judo_span){token.lexeme, token.lexeme_length};
                scanner->out->element = JUDO_ARRAY_POP;
                scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
                status = JUDO_SUCCESS;
            }
            else
            {
                status = bad_syntax(scanner, scanner->at, 1, "expected ']' or ','");
            }
        }
    }
    return status;
}

static enum judo_result parse_object(struct scanner *scanner, const struct token *token)
{
    assert(token->type == TOKEN_LCURLYB); // LCOV_EXCL_BR_LINE
    eat(scanner, token);
    scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
    scanner->out->element = JUDO_OBJECT_PUSH;
    scanner->out->s_state[scanner->out->s_stack] = STATE_PARSE_OBJECT_KEY_OR_OBJECT_END;
    return JUDO_SUCCESS;
}

static enum judo_result parse_object_key(struct scanner *scanner, const struct token *token)
{
    enum judo_result status = JUDO_SUCCESS;
    if (token->type == TOKEN_STRING)
    {
        eat(scanner, token);
        scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
        scanner->out->element = JUDO_OBJECT_NAME;
        scanner->out->s_state[scanner->out->s_stack] = STATE_PARSE_OBJECT_VALUE;
    }
#if defined(JUDO_JSON5)
    else if (token->type == TOKEN_ID)
    {
        eat(scanner, token);
        scanner->out->where = (struct judo_span){token->lexeme, token->lexeme_length};
        scanner->out->element = JUDO_OBJECT_NAME;
        scanner->out->s_state[scanner->out->s_stack] = STATE_PARSE_OBJECT_VALUE;
    }
#endif
    else
    {
        status = bad_syntax(scanner, scanner->at, 1, "expected '}' or string");
    }
    return status;
}

static enum judo_result parse_object_value(struct scanner *scanner)
{
    bool accepted = false;
    enum judo_result status = accept(scanner, TOKEN_COLON, &accepted);
    if (status == JUDO_SUCCESS)
    {
        if (accepted)
        {
            scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_OBJECT_VALUE;
            status = parse_value(scanner, "expected value after ':'");
        }
        else
        {
            status = bad_syntax(scanner, scanner->at, 1, "expected ':'");
        }
    }
    return status;
}

static enum judo_result parse_object_key_or_object_end(struct scanner *scanner)
{
    struct token token;
    enum judo_result status = peek(scanner, &token);
    if (status == JUDO_SUCCESS)
    {
        if (token.type == TOKEN_RCURLYB)
        {
            eat(scanner, &token);
            scanner->out->where = (struct judo_span){token.lexeme, token.lexeme_length};
            scanner->out->element = JUDO_OBJECT_POP;
            scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
            status = JUDO_SUCCESS;
        }
        else
        {
            status = parse_object_key(scanner, &token);
        }
    }
    return status;
}

static enum judo_result finished_parsing_object_value(struct scanner *scanner)
{
    struct token token = {0};
    enum judo_result status = peek(scanner, &token);
    if (status == JUDO_SUCCESS)
    {
        if (token.type == TOKEN_COMMA)
        {
            eat(scanner, &token);
#if defined(JUDO_WITH_TRAILING_COMMAS) || defined(JUDO_JSON5)
            status = parse_object_key_or_object_end(scanner);
#else
            // Get the next token, which is supposed to be an object name.
            status = peek(scanner, &token);
            if (status == JUDO_SUCCESS)
            {
                status = parse_object_key(scanner, &token);
            }
#endif
        }
        else
        {
            if (token.type == TOKEN_RCURLYB)
            {
                eat(scanner, &token);
                scanner->out->where = (struct judo_span){token.lexeme, token.lexeme_length};
                scanner->out->element = JUDO_OBJECT_POP;
                scanner->out->s_state[scanner->out->s_stack] = STATE_FINISHED_PARSING_VALUE;
                status = JUDO_SUCCESS;
            }
            else
            {
                status = bad_syntax(scanner, scanner->at, 1, "expected '}' or ','");
            }
        }
    }
    return status;
}

static enum judo_result parse_value(struct scanner *scanner, const char *msg)
{
    enum judo_result status;

    // Check to ensure that the maximum level of nesting hasn't been reached.
    if (scanner->out->s_stack >= (JUDO_MAXDEPTH - 1))
    {
        status = max_nesting_depth(scanner);;
    }
    else
    {
        // Before we parse the next value, reserve space on the stack for its state.
        scanner->out->s_stack += 1;

        struct token token;
        status = peek(scanner, &token);
        if (status == JUDO_SUCCESS)
        {
            switch (token.type)
            {
            case TOKEN_NULL:
                status = parse_null(scanner, &token);
                break;

            case TOKEN_NUMBER:
                status = parse_number(scanner, &token);
                break;

            case TOKEN_STRING:
                status = parse_string(scanner, &token);
                break;

            case TOKEN_TRUE:
                status = parse_bool(scanner, &token);
                break;

            case TOKEN_FALSE:
                status = parse_bool(scanner, &token);
                break;

            case TOKEN_LBRACE:
                status = parse_array(scanner, &token);
                break;

            case TOKEN_LCURLYB:
                status = parse_object(scanner, &token);
                break;

            default:
                status = bad_syntax(scanner, scanner->at, 1, msg);
                break;
            }
        }
    }

    return status;
}

static enum judo_result parse_root(struct scanner *scanner)
{
    // Skip UTF-8 BOM (if present).
    if (is_bounded(scanner->string, scanner->length, scanner->at, 3))
    {
        const uint8_t utf8_bom[] = {(uint8_t)0xEF, (uint8_t)0xBB, (uint8_t)0xBF};
        if (memcmp(scanner->string, utf8_bom, 3) == 0)
        {
            scanner->at += 3;
        }
    }

    // RFC 4627 requires the top-level value to be an object or array.
    struct token token;
    enum judo_result status = peek(scanner, &token);
    if (status == JUDO_SUCCESS)
    {
        switch (token.type)
        {
        case TOKEN_LBRACE:
            status = parse_array(scanner, &token);
            break;

        case TOKEN_LCURLYB:
            status = parse_object(scanner, &token);
            break;

#if defined(JUDO_RFC8259) || defined(JUDO_JSON5)
        case TOKEN_NULL:
            status = parse_null(scanner, &token);
            break;

        case TOKEN_NUMBER:
            status = parse_number(scanner, &token);
            break;

        case TOKEN_STRING:
            status = parse_string(scanner, &token);
            break;

        case TOKEN_FALSE:
        case TOKEN_TRUE:
            status = parse_bool(scanner, &token);
            break;
#endif

        default:
            status = bad_syntax(scanner, 0, 0, "expected root value");
            break;
        }
    }

    return status;
}

enum judo_result judo_scan(struct judo_stream *stream, const char *source, int32_t length) // cppcheck-suppress misra-c2012-8.7 ; Public function must have external linkage.
{
    enum judo_result status = JUDO_INVALID_OPERATION;

    if ((stream != NULL) && (source != NULL))
    {
        struct scanner scanner;
        scanner.out = stream;
        scanner.string = (const uint8_t *)source;
        scanner.length = length;
        scanner.at = stream->s_at;
        status = JUDO_SUCCESS;

        // If we finished parsing a value at the current stack depth, then pop the stack.
        // We do this before the switch statement to ensure it always operators on an unfinished value.
        if (stream->s_state[stream->s_stack] == STATE_FINISHED_PARSING_VALUE)
        {
            if (stream->s_stack == 0)
            {
                struct token token = {0};
                status = peek(&scanner, &token);
                if (status == JUDO_SUCCESS)
                {
                    if (token.type == TOKEN_EOF)
                    {
                        stream->element = JUDO_EOF;
                        stream->where = (struct judo_span){token.lexeme, token.lexeme_length};
                        stream->s_state[stream->s_stack] = STATE_FINISHED_PARSING;
                    }
                    else
                    {
                        const int32_t at = scanner.at;
                        status = bad_syntax(&scanner, at, 1, "expected EOF");
                    }
                }
            }
            else
            {
                stream->s_stack -= 1;
            }
        }

        if (status == JUDO_SUCCESS)
        {
            switch (stream->s_state[stream->s_stack])
            {
            case STATE_PARSE_ROOT:
                status = parse_root(&scanner);
                break;

            case STATE_FINISHED_PARSING_ARRAY_ELEMENT:
                status = finished_parsing_array_element(&scanner);
                break;

            case STATE_PARSE_ARRAY_END_OR_ARRAY_ELEMENT:
                status = parse_array_element_or_array_end(&scanner);
                break;

            case STATE_PARSE_OBJECT_KEY_OR_OBJECT_END:
                status = parse_object_key_or_object_end(&scanner);
                break;

            case STATE_PARSE_OBJECT_VALUE:
                status = parse_object_value(&scanner);
                break;

            case STATE_FINISHED_PARSING_OBJECT_VALUE:
                status = finished_parsing_object_value(&scanner);
                break;

            case STATE_PARSING_ERROR:
                status = JUDO_BAD_SYNTAX;
                break;

            case STATE_ENCODING_ERROR:
                status = JUDO_ILLEGAL_BYTE_SEQUENCE;
                break;

            case STATE_MAX_NESTING_ERROR:
                status = JUDO_MAXIMUM_NESTING;
                break;

            case STATE_FINISHED_PARSING:
                break;

            default:
                status = JUDO_MALFUNCTION;
                break;
            }

            stream->s_at = scanner.at;
        }
    }

    return status;
}
