#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef bool (*image_png_read_t)(void *, void *, size_t);
typedef void *(*image_png_alloc_t)(size_t);
/* RGB565, high byte first, aspect fitted without upscaling. Caller frees pixels. */
bool image_png_decode(image_png_read_t read, void *user, image_png_alloc_t alloc,
                      void (*release)(void *), int max_w, int max_h,
                      uint16_t **pixels, int *width, int *height);
