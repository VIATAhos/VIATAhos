#ifndef EXEC_H
#define EXEC_H
#include <stdint.h>
#include <stdbool.h>


typedef enum { EXT_UNKNOWN, EXT_KVBN, EXT_XEP, EXT_VLIB, EXT_DATA } file_type_t;

static inline int _ext_strlen(const char *s) { int l=0; while(s[l]) l++; return l; }
static inline int _ext_strcmp(const char *a, const char *b) {
    while (*a && (*a==*b)){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}

static inline bool ext_parse(const char *filename, file_type_t *rtype, bool *compressed) {
    int len = _ext_strlen(filename);
    *compressed = false; *rtype = EXT_UNKNOWN;
    char buf[280]; int bl=0;
    while (bl<279 && filename[bl]) { buf[bl]=filename[bl]; bl++; } buf[bl]=0;
    if (len>5 && _ext_strcmp(buf+len-5,".smol")==0) {
        *compressed=true; buf[len-5]=0; len-=5;
    }
    if (len>5 && _ext_strcmp(buf+len-5,".kvbn")==0) *rtype=EXT_KVBN;
    else if (len>4 && _ext_strcmp(buf+len-4,".xep")==0)  *rtype=EXT_XEP;
    else if (len>5 && _ext_strcmp(buf+len-5,".vlib")==0) *rtype=EXT_VLIB;
    else *rtype=EXT_DATA;
    return true;
}

#endif 
