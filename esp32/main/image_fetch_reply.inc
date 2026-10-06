/* SPDX-License-Identifier: Apache-2.0
 * PSRAM image pipeline. URLs and redirect targets never appear in logs. */
#include "image_png.h"
#include "image_base64.h"
#include "muse_proxy.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "rom/tjpgd.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define IMAGE_URL_CAP 2048
#define IMAGE_BYTES_MAX (4*1024*1024)
static const char *TAG = "link.image";
static atomic_bool image_busy;
static atomic_uint image_generation;
typedef struct {
    char url[IMAGE_URL_CAP], location[IMAGE_URL_CAP];
    char routed[IMAGE_URL_CAP+64], origin[256];
    esp_http_client_handle_t http;
    image_fetch_done_cb done; void *user;
    unsigned generation;
    int row, sw, sh, dw, dh, jw, jh;
    int64_t deadline;
    size_t bytes;
    uint8_t peek[8]; size_t peek_len, peek_pos;
    uint16_t *frame;
    uint8_t scratch[512];
    char *base64;
    uint8_t *inline_data;
    size_t inline_len,inline_pos;
} reply_image_t;
static void *image_alloc(size_t n) { return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
void image_fetch_cancel(void) { atomic_fetch_add(&image_generation,1); }
static bool image_current(reply_image_t *f) {
    return f->generation==atomic_load(&image_generation) && esp_timer_get_time()<f->deadline;
}
static int image_read(reply_image_t *f, void *out, size_t len) {
    uint8_t *p=out; size_t have=0;
    while (have<len && f->peek_pos<f->peek_len) p[have++]=f->peek[f->peek_pos++];
    while (have<len) {
        if (!image_current(f) || f->bytes>=IMAGE_BYTES_MAX) return -1;
        if(f->inline_data) {
            size_t n=f->inline_len-f->inline_pos;
            if(n>len-have) n=len-have;
            if(!n) return have;
            memcpy(p+have,f->inline_data+f->inline_pos,n);
            f->inline_pos+=n; have+=n; f->bytes+=n;
            continue;
        }
        size_t nread=len-have;
        if (nread>IMAGE_BYTES_MAX-f->bytes) nread=IMAGE_BYTES_MAX-f->bytes;
        int n=esp_http_client_read(f->http,(char*)p+have,nread);
        if (n<0) return -1;
        if (!n) return esp_http_client_is_complete_data_received(f->http) ? (int)have : -1;
        have+=n; f->bytes+=n;
    }
    return have;
}
static bool png_reader(void *user,void *out,size_t n) { return image_read(user,out,n)==(int)n; }
static esp_err_t image_event(esp_http_client_event_t *e) {
    reply_image_t *f=e->user_data;
    if(e->event_id==HTTP_EVENT_ON_HEADER && !strcasecmp(e->header_key,"Location"))
        snprintf(f->location,sizeof(f->location),"%s",e->header_value);
    if(f->http) {
        int64_t left=(f->deadline-esp_timer_get_time())/1000;
        if(!image_current(f)) left=1;
        esp_http_client_set_timeout_ms(f->http,left<1?1:left<8000?(int)left:8000);
    }
    return ESP_OK;
}
/* Resolve against the origin URL, never the loopback URL. Every hop is routed
 * again and keeps its own TLS hostname verification and Host header. */
static bool redirect_url(reply_image_t *f) {
    const char *l=f->location;
    if(!*l) return false;
    char *next=image_alloc(IMAGE_URL_CAP); if(!next) return false;
    int n=-1;
    if(!strncasecmp(l,"https://",8)) n=snprintf(next,IMAGE_URL_CAP,"%s",l);
    else if(!strncmp(l,"//",2)) n=snprintf(next,IMAGE_URL_CAP,"https:%s",l);
    else if(!strstr(l,":")) {
        const char *path=strchr(f->url+8,'/');
        size_t host=path?(size_t)(path-f->url):strlen(f->url);
        if(*l=='/') n=snprintf(next,IMAGE_URL_CAP,"%.*s%s",(int)host,f->url,l);
        else {
            const char *q=strchr(f->url,'?'); size_t end=q?(size_t)(q-f->url):strlen(f->url);
            if(*l=='?') n=snprintf(next,IMAGE_URL_CAP,"%.*s%s",(int)end,f->url,l);
            else {
                while(end>host && f->url[end-1]!='/') --end;
                n=snprintf(next,IMAGE_URL_CAP,"%.*s%s%s",(int)end,f->url,end==host?"/":"",l);
            }
        }
    }
    bool ok=n>0 && n<IMAGE_URL_CAP;
    if(ok) memcpy(f->url,next,n+1);
    free(next); return ok;
}
static bool image_open(reply_image_t *f) {
    if(f->base64) {
        f->inline_data=image_alloc(IMAGE_INLINE_MAX);
        return f->inline_data && image_base64_decode(f->base64,f->inline_data,IMAGE_INLINE_MAX,&f->inline_len);
    }
    for(int hop=0;hop<4;++hop) {
        if(!image_current(f)) return false;
        if(muse_proxy_http_url(f->url,f->routed,sizeof(f->routed),f->origin,sizeof(f->origin))!=ESP_OK) return false;
        f->location[0]=0;
        esp_http_client_config_t cfg={
            .url=f->routed,.common_name=f->origin,.crt_bundle_attach=esp_crt_bundle_attach,
            .timeout_ms=8000,.buffer_size=2048,.buffer_size_tx=1024,
            .disable_auto_redirect=true,.event_handler=image_event,.user_data=f,
        };
        f->http=esp_http_client_init(&cfg);
        if(!f->http || esp_http_client_set_header(f->http,"Host",f->origin)!=ESP_OK ||
           esp_http_client_open(f->http,0)!=ESP_OK) return false;
        esp_http_client_fetch_headers(f->http);
        int status=esp_http_client_get_status_code(f->http);
        if(status==200) {
            int64_t n=esp_http_client_get_content_length(f->http);
            return n<=IMAGE_BYTES_MAX;
        }
        bool redirect=status==301||status==302||status==303||status==307||status==308;
        if(!redirect || hop==3 || !redirect_url(f)) return false;
        esp_http_client_cleanup(f->http); f->http=NULL;
    }
    return false;
}
static UINT jpeg_input(JDEC *jd,BYTE *buf,UINT len) {
    reply_image_t *f=jd->device;
    if(buf) { int n=image_read(f,buf,len); return n<0?0:(UINT)n; }
    UINT have=0;
    while(have<len) {
        UINT take=len-have; if(take>sizeof(f->scratch)) take=sizeof(f->scratch);
        int n=image_read(f,f->scratch,take); if(n<=0) break; have+=n;
    }
    return have;
}
static UINT jpeg_output(JDEC *jd,void *bitmap,JRECT *r) {
    reply_image_t *f=jd->device; if(!image_current(f)) return 0;
    int bw=r->right-r->left+1;
    const uint8_t *rgb=bitmap;
    int yfirst=((int)r->top*f->dh+f->jh-1)/f->jh;
    int yend=((int)(r->bottom+1)*f->dh+f->jh-1)/f->jh;
    int xfirst=((int)r->left*f->dw+f->jw-1)/f->jw;
    int xend=((int)(r->right+1)*f->dw+f->jw-1)/f->jw;
    for(int y=yfirst;y<yend && y<f->dh;++y) {
        int sy=(int)((int64_t)y*f->jh/f->dh);
        if(sy<r->top || sy>r->bottom) continue;
        for(int x=xfirst;x<xend && x<f->dw;++x) {
            int sx=(int)((int64_t)x*f->jw/f->dw);
            if(sx<r->left || sx>r->right) continue;
            const uint8_t *p=rgb+((sy-r->top)*bw+sx-r->left)*3;
            uint16_t v=((p[0]&248)<<8)|((p[1]&252)<<3)|(p[2]>>3);
            f->frame[y*f->dw+x]=(uint16_t)((v>>8)|(v<<8));
        }
    }
    return 1;
}
static const char *image_jpeg(reply_image_t *f,int avail,int *scale) {
    void *pool=image_alloc(3100); if(!pool) return "out of memory";
    JDEC jd; JRESULT rc=jd_prepare(&jd,jpeg_input,pool,3100,f);
    const char *error=NULL;
    if(rc==JDR_OK && (!jd.width||!jd.height||jd.width>4096||jd.height>4096)) error="image dimensions exceed limit";
    if(rc==JDR_OK && f->inline_data && (jd.width!=128 || jd.height!=128)) error="inline image must be 128x128";
    if(rc==JDR_OK && !error) {
        uint8_t s=0;
        while(s<3 && (jd.width>>(s+1)) && (jd.height>>(s+1)) &&
              ((jd.width>>s)>f->sw || (jd.height>>s)>avail)) ++s;
        f->jw=jd.width>>s; f->jh=jd.height>>s; *scale=1<<s;
        f->dw=f->jw; f->dh=f->jh;
        if(f->dw>f->sw) { f->dh=(int)((int64_t)f->dh*f->sw/f->dw); f->dw=f->sw; }
        if(f->dh>avail) { f->dw=(int)((int64_t)f->dw*avail/f->dh); f->dh=avail; }
        if(f->dw<1) f->dw=1;
        if(f->dh<1) f->dh=1;
        f->frame=image_alloc(f->dw*f->dh*2);
        if(!f->frame) error="out of memory";
        else { memset(f->frame,0,f->dw*f->dh*2); rc=jd_decomp(&jd,jpeg_output,s); }
    }
    if(!error && rc!=JDR_OK) error=rc==JDR_FMT3?"progressive JPEG is unsupported":"JPEG decode/download failed";
    free(pool); return error;
}
static bool image_commit(reply_image_t *f) {
    int y0=f->row<0?(f->sh-f->dh)/2:f->row;
    uint16_t *screen=image_alloc(f->sw*f->sh*2); if(!screen) return false;
    memset(screen,0,f->sw*f->sh*2);
    int x0=(f->sw-f->dw)/2;
    for(int y=0;y<f->dh;++y) memcpy(screen+(y+y0)*f->sw+x0,f->frame+y*f->dw,f->dw*2);
    bool ok=image_current(f) && led_status_draw_rect(0,0,f->sw,f->sh,screen);
    if(ok) led_status_draw_done();
    free(screen); return ok;
}
static void image_task(void *arg) {
    reply_image_t *f=arg; int64_t start=esp_timer_get_time();
    f->deadline=start+60000000;
    image_fetch_result_t result={.code="download_failed",.message="image download failed",.scale=1};
    if(f->base64) { result.code="invalid_image"; result.message="invalid or oversized base64 image"; }
    if(image_open(f)) {
        int n=image_read(f,f->peek,8);
        f->peek_len=n>0?n:0; f->peek_pos=0;
        const char *error="unsupported image format (use PNG or baseline JPEG)";
        int avail=f->sh-(f->row<0?0:f->row);
        if(n==8 && !memcmp(f->peek,"\x89PNG\r\n\x1a\n",8)) {
            result.format="png";
            bool size_ok=!f->inline_data || (f->inline_len>=24 &&
                !memcmp(f->inline_data+16,"\0\0\0\x80\0\0\0\x80",8));
            error=!size_ok ? "inline image must be 128x128" :
                image_png_decode(png_reader,f,image_alloc,free,f->sw,avail,&f->frame,&f->dw,&f->dh)
                ? NULL : "PNG decode/download failed";
        } else if(n>=2 && f->peek[0]==255 && f->peek[1]==216) {
            result.format="jpeg"; error=image_jpeg(f,avail,&result.scale);
        } else if(!f->inline_data) {
            /* Preserve the display command's raw RGB565 format, bounded to the
             * full available rectangle and committed only after validation. */
            f->frame=image_alloc(f->sw*avail*2); f->dw=f->sw;
            int bytes=f->frame?image_read(f,f->frame,f->sw*avail*2):-1;
            if(bytes>0 && !(bytes%(f->sw*2))) {
                uint8_t tail; int extra=image_read(f,&tail,1);
                if(extra==0) { f->dh=bytes/(f->sw*2); error=NULL; result.format="rgb565"; }
            }
        }
        result.width=f->dw; result.height=f->dh;
        if(error) { result.code="invalid_image"; result.message=error; }
        else if(image_commit(f)) { result.ok=true; result.code=NULL; result.message=NULL; }
        else result.message="display cancelled or unavailable";
    }
    if(f->http) esp_http_client_cleanup(f->http);
    result.bytes=f->bytes; result.ms=(esp_timer_get_time()-start)/1000;
    ESP_LOGI(TAG,"image %s: %s %dx%d, %u bytes in %d ms",result.ok?"displayed":"failed",
        result.ok?result.format:result.message,result.width,result.height,(unsigned)result.bytes,result.ms);
    if(f->done) f->done(&result,f->user);
    free(f->base64); free(f->inline_data); free(f->frame); free(f); atomic_store(&image_busy,false);
    vTaskDeleteWithCaps(NULL);
}
bool image_fetch_base64_start(const char *data,image_fetch_done_cb done,void *user,
                              const char **code,const char **message) {
    int w,h;
    size_t n=data?strlen(data):0;
    if(!n || n>IMAGE_BASE64_MAX || n%4) {
        *code="invalid_params"; *message="base64 must contain 1..49152 characters, padded to a multiple of four"; return false;
    }
    if(!led_status_display_info(&w,&h)) { *code="unsupported"; *message="no display"; return false; }
    bool expected=false;
    if(!atomic_compare_exchange_strong(&image_busy,&expected,true)) {
        *code="busy"; *message="another image is being displayed"; return false;
    }
    reply_image_t *f=image_alloc(sizeof(*f));
    if(f) {
        memset(f,0,sizeof(*f)); f->base64=image_alloc(n+1);
        if(f->base64) {
            memcpy(f->base64,data,n+1); f->sw=w; f->sh=h; f->row=-1; f->done=done; f->user=user;
            f->generation=atomic_load(&image_generation);
            if(xTaskCreatePinnedToCoreWithCaps(image_task,"inline_image",16384,f,4,NULL,0,
                MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS) return true;
        }
        free(f->base64); free(f);
    }
    atomic_store(&image_busy,false); *code="out_of_memory"; *message="inline image allocation failed"; return false;
}
bool image_fetch_start(const char *url,int row,image_fetch_done_cb done,void *user,
                       const char **code,const char **message) {
    int w,h;
    if(!led_status_display_info(&w,&h)) { *code="unsupported"; *message="no display"; return false; }
    if(!url||strncasecmp(url,"https://",8)||strlen(url)>=IMAGE_URL_CAP||row< -1||row>=h) {
        *code="invalid_params"; *message="an HTTPS image URL and valid row are required"; return false;
    }
    bool expected=false;
    if(!atomic_compare_exchange_strong(&image_busy,&expected,true)) {
        *code="busy"; *message="another image is downloading"; return false;
    }
    reply_image_t *f=image_alloc(sizeof(*f));
    if(f) {
        memset(f,0,sizeof(*f)); strcpy(f->url,url);
        f->sw=w; f->sh=h; f->row=row; f->done=done; f->user=user;
        f->generation=atomic_load(&image_generation);
        if(xTaskCreatePinnedToCoreWithCaps(image_task,"reply_image",16384,f,4,NULL,0,
            MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS) return true;
        free(f);
    }
    atomic_store(&image_busy,false); *code="out_of_memory"; *message="image task allocation failed"; return false;
}
