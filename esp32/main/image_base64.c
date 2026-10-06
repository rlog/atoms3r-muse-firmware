/* SPDX-License-Identifier: Apache-2.0 */
#include "image_base64.h"
#include <string.h>
#include "mbedtls/base64.h"
bool image_base64_decode(const char *text,void *out,size_t cap,size_t *len) {
    *len=0;
    if(!text || !out) return false;
    size_t n=strlen(text);
    if(!n || n>IMAGE_BASE64_MAX || n%4) return false;
    size_t padding=text[n-1]=='='?1:0;
    if(n>1 && text[n-2]=='=') ++padding;
    for(size_t i=0;i<n-padding;++i) {
        unsigned char c=text[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/')) return false;
    }
    size_t need=n/4*3-padding;
    if(need>cap || need>IMAGE_INLINE_MAX) return false;
    return mbedtls_base64_decode(out,cap,len,(const unsigned char*)text,n)==0 && *len==need;
}
