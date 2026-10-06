/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static inline bool muse_image_url_copy(const char *s,size_t n,char *out,size_t cap) {
    if(n<9||n>=cap||strncmp(s,"https://",8)) return false;
    for(size_t i=0;i<n;++i) if((unsigned char)s[i]<=32||s[i]=='"'||s[i]=='<'||s[i]=='>') return false;
    memcpy(out,s,n); out[n]=0; return true;
}
static inline bool muse_image_markdown(const char *text,char *out,size_t cap) {
    bool found=false;
    if(!text) return false;
    const char *p=text;
    while((p=strstr(p,"!["))) {
        const char *end=strchr(p+2,']'); if(!end) break;
        p=end+1; if(*p!='(') continue;
        ++p; while(*p==' '||*p=='\n') ++p;
        bool angle=*p=='<'; if(angle) ++p;
        const char *begin=p;
        while(*p && (angle?*p!='>':*p!=')'&&!isspace((unsigned char)*p))) ++p;
        if(muse_image_url_copy(begin,p-begin,out,cap)) found=true;
    }
    return found;
}
static inline const char *muse_image_string(const cJSON *o,const char *k) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(o,k));
}
static inline bool muse_image_hint(const char *s) {
    if(!s) return false;
    return !strncmp(s,"image",5)||strstr(s,".png")||strstr(s,".jpg")||strstr(s,".jpeg");
}
/* Traverse a bounded JSON tree. Only image-tagged objects/collections and
 * Markdown images supply URLs; ordinary hyperlinks are never drawn. */
static inline bool muse_image_node(const cJSON *node,bool image,int depth,int *budget,char *out,size_t cap) {
    if(!node||depth>12||--*budget<0) return false;
    bool found=false;
    if(cJSON_IsString(node)) {
        const char *s=cJSON_GetStringValue(node);
        return image?muse_image_url_copy(s,strlen(s),out,cap):muse_image_markdown(s,out,cap);
    }
    if(cJSON_IsObject(node)) {
        image=image||muse_image_hint(muse_image_string(node,"type"))||
            muse_image_hint(muse_image_string(node,"mime"))||muse_image_hint(muse_image_string(node,"mime_type"))||muse_image_hint(muse_image_string(node,"content_type"))||
            muse_image_hint(muse_image_string(node,"filename"))||muse_image_hint(muse_image_string(node,"name"));
    }
    for(const cJSON *c=node->child;c;c=c->next) {
        const char *key=c->string?c->string:"";
        bool tagged=!strcmp(key,"image_url")||!strcmp(key,"images")||!strcmp(key,"image")||!strcmp(key,"image_urls");
        bool url_key=!strcmp(key,"url")||!strcmp(key,"src")||!strcmp(key,"path")||
            !strcmp(key,"original")||!strcmp(key,"thumbnail")||!strcmp(key,"preview")||
            !strcmp(key,"download_url")||!strcmp(key,"public_url");
        if(cJSON_IsString(c) && url_key) {
            const char *s=cJSON_GetStringValue(c);
            if((image||muse_image_hint(s))&&muse_image_url_copy(s,strlen(s),out,cap)) found=true;
        } else if(muse_image_node(c,tagged||(image&&(cJSON_IsArray(node)||url_key||cJSON_IsObject(c)||cJSON_IsArray(c))),depth+1,budget,out,cap)) found=true;
    }
    return found;
}
static inline bool muse_reply_image(const cJSON *payload,char *out,size_t cap) {
    int budget=2048; return muse_image_node(payload,false,0,&budget,out,cap);
}
