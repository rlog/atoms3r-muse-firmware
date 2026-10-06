/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONTROL_MESSAGE_MAX 65536
typedef struct {
    uint8_t prefix[4]; size_t prefix_len;
    uint32_t wanted; size_t have;
    uint8_t *frame;
    bool failed;
} control_message_t;
typedef void (*control_message_cb)(void*,const uint8_t*,size_t);
static inline void control_message_reset(control_message_t *s,void (*release)(void*)) {
    release(s->frame); memset(s,0,sizeof(*s));
}
/* Reassemble u32-LE framed JSON across arbitrary BodyChunk boundaries.
 * The callback receives the complete prefix + message. No partial command
 * may run, and oversized frames fail closed until the session is reset. */
static inline bool control_message_feed(control_message_t *s,const uint8_t *data,size_t len,
        void *(*alloc)(size_t),void (*release)(void*),control_message_cb cb,void *user) {
    if(s->failed) return false;
    while(len) {
        if(s->prefix_len<4) {
            size_t n=4-s->prefix_len; if(n>len) n=len;
            memcpy(s->prefix+s->prefix_len,data,n); s->prefix_len+=n; data+=n; len-=n;
            if(s->prefix_len<4) continue;
            s->wanted=(uint32_t)s->prefix[0]|((uint32_t)s->prefix[1]<<8)|
                ((uint32_t)s->prefix[2]<<16)|((uint32_t)s->prefix[3]<<24);
            if(s->wanted>CONTROL_MESSAGE_MAX) { s->failed=true; return false; }
            s->frame=(uint8_t*)alloc((size_t)s->wanted+4);
            if(!s->frame) { s->failed=true; return false; }
            memcpy(s->frame,s->prefix,4);
        }
        size_t n=s->wanted-s->have; if(n>len) n=len;
        memcpy(s->frame+4+s->have,data,n); s->have+=n; data+=n; len-=n;
        if(s->have==s->wanted) {
            cb(user,s->frame,(size_t)s->wanted+4);
            control_message_reset(s,release);
        }
    }
    return true;
}
