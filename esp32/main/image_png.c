/* SPDX-License-Identifier: Apache-2.0 */
#include "image_png.h"
#include <png.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
typedef union { max_align_t alignment; size_t size; } allocation_header_t;
typedef struct {
    image_png_read_t read; void *user;
    image_png_alloc_t alloc; void (*release)(void *);
    size_t png_bytes;
    unsigned char *rows;
    uint16_t *pixels;
} decode_t;
static void *png_alloc(png_structp png, png_alloc_size_t size) {
    decode_t *d = png_get_mem_ptr(png);
    if (size > 256*1024 || d->png_bytes + size > 256*1024) return NULL;
    allocation_header_t *p = d->alloc(size + sizeof(*p));
    if (!p) return NULL;
    p->size = size; d->png_bytes += size; return p + 1;
}
static void png_release(png_structp png, void *ptr) {
    if (!ptr) return;
    decode_t *d = png_get_mem_ptr(png); allocation_header_t *p = (allocation_header_t *)ptr - 1;
    d->png_bytes -= p->size; d->release(p);
}
static void png_input(png_structp png, png_bytep out, png_size_t n) {
    decode_t *d = png_get_io_ptr(png);
    if (!d->read(d->user, out, n)) png_error(png, "download failed");
}
static void png_failure(png_structp png, const char *message) { (void)message; png_longjmp(png, 1); }
static void png_warning_ignore(png_structp png, const char *message) { (void)png; (void)message; }
bool image_png_decode(image_png_read_t read, void *user, image_png_alloc_t alloc,
                      void (*release)(void *), int max_w, int max_h,
                      uint16_t **pixels, int *width, int *height) {
    *pixels = NULL;
    if (max_w < 1 || max_h < 1 || max_w > 1024 || max_h > 1024) return false;
    decode_t *d = alloc(sizeof(*d));
    if (!d) return false;
    memset(d, 0, sizeof(*d)); d->read=read; d->user=user; d->alloc=alloc; d->release=release;
    png_structp png = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, NULL, png_failure,
                                              png_warning_ignore, d, png_alloc, png_release);
    if (!png) { release(d); return false; }
    png_infop info = png_create_info_struct(png);
    bool ok = false;
    if (info && !setjmp(png_jmpbuf(png))) {
        png_set_user_limits(png, 4096, 4096);
        png_set_chunk_malloc_max(png, 32*1024);
        png_set_chunk_cache_max(png, 8);
        png_set_read_fn(png, d, png_input);
        png_read_info(png, info);
        unsigned w = png_get_image_width(png, info), h = png_get_image_height(png, info);
        int type = png_get_color_type(png, info), depth = png_get_bit_depth(png, info);
        if (!w || !h || w > 4096 || h > 4096) png_error(png,"dimensions");
        int out_w=(int)w, out_h=(int)h;
        if (out_w > max_w) { out_h=(int)((uint64_t)h*max_w/w); out_w=max_w; }
        if (out_h > max_h) { out_w=(int)((uint64_t)out_w*max_h/out_h); out_h=max_h; }
        if (out_w < 1) out_w=1;
        if (out_h < 1) out_h=1;
        if (depth==16) png_set_strip_16(png);
        if (type==PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
        if (type==PNG_COLOR_TYPE_GRAY && depth<8) png_set_expand_gray_1_2_4_to_8(png);
        bool transparent = png_get_valid(png,info,PNG_INFO_tRNS)!=0;
        if (transparent) png_set_tRNS_to_alpha(png);
        if (type==PNG_COLOR_TYPE_GRAY || type==PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
        if (!(type & PNG_COLOR_MASK_ALPHA) && !transparent) png_set_add_alpha(png,255,PNG_FILLER_AFTER);
        int passes=png_set_interlace_handling(png);
        png_read_update_info(png,info);
        size_t stride=png_get_rowbytes(png,info);
        if (stride != (size_t)w*4) png_error(png,"pixels");
        size_t rows_size=stride*(passes>1 ? (size_t)out_h : 1);
        if (rows_size>2*1024*1024) png_error(png,"interlace memory limit");
        d->rows=alloc(rows_size); d->pixels=alloc((size_t)out_w*out_h*2);
        if (!d->rows || !d->pixels) png_error(png,"memory");
        memset(d->rows,0,rows_size);
        for (int pass=0;pass<passes;++pass) {
            int dy=0;
            for (unsigned y=0;y<h;++y) {
                bool selected=dy<out_h && y==(unsigned)((uint64_t)dy*h/out_h);
                unsigned char *row=selected ? d->rows + (passes>1 ? (size_t)dy*stride : 0) : NULL;
                png_read_row(png,row,NULL);
                if (!selected) continue;
                if (pass==passes-1) for (int x=0;x<out_w;++x) {
                    unsigned char *p=row + ((uint64_t)x*w/out_w)*4;
                    unsigned a=p[3];
                    unsigned r=(p[0]*a+255*(255-a)+127)/255;
                    unsigned g=(p[1]*a+255*(255-a)+127)/255;
                    unsigned b=(p[2]*a+255*(255-a)+127)/255;
                    uint16_t v=((r&248)<<8)|((g&252)<<3)|(b>>3);
                    d->pixels[(size_t)dy*out_w+x]=(uint16_t)((v>>8)|(v<<8));
                }
                ++dy;
            }
        }
        png_read_end(png,NULL);
        *width=out_w; *height=out_h; *pixels=d->pixels; d->pixels=NULL; ok=true;
    }
    png_destroy_read_struct(&png,&info,NULL);
    release(d->rows); release(d->pixels); release(d);
    return ok;
}
