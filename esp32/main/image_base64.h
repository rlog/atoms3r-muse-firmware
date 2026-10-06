/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define IMAGE_BASE64_MAX 49152
#define IMAGE_INLINE_MAX (IMAGE_BASE64_MAX/4*3)
bool image_base64_decode(const char *text, void *out, size_t cap, size_t *len);
