#pragma once
#include <stddef.h>
#include <string.h>

/* Formatting between sentences never becomes a speech request. */
static inline size_t muse_speech_skip_space(const char *text)
{
    size_t pos = 0;
    for (;;) {
        unsigned char c = (unsigned char)text[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') ++pos;
        else if (!strncmp(text + pos, "\xc2\xa0", 2)) pos += 2;
        else if (!strncmp(text + pos, "\xe3\x80\x80", 3)) pos += 3;
        else return pos;
    }
}

/* Choose a complete UTF-8 sentence, or a bounded prefix of a long sentence.
 * The unconsumed suffix stays in the message until more text arrives. */
static inline size_t muse_speech_segment(const char *text, bool done)
{
    size_t n = strlen(text), pos = 0;
    while (pos < n) {
        unsigned char c = (unsigned char)text[pos];
        size_t width = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (pos + width > n) break;
        bool stop = c == '\n' || c == '!' || c == '?' || c == ';' ||
                    (width == 3 && (!memcmp(text + pos, "。", 3) ||
                     !memcmp(text + pos, "！", 3) || !memcmp(text + pos, "？", 3) ||
                     !memcmp(text + pos, "；", 3)));
        pos += width;
        if (stop || pos >= 240) return pos;
    }
    return done ? pos : 0;
}
