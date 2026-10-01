#include "string.h"

void str_cpy(char* dest, const char* src) {
    while (*src) { *dest++ = *src++; }
    *dest = '\0';
}

void str_cat(char* dest, const char* src) {
    while (*dest) dest++;
    while (*src) { *dest++ = *src++; }
    *dest = '\0';
}

bool str_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return false;
    }
    return true;
}

int str_cmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

int mem_cmp(const void *s1, const void *s2, uint32_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

int str_len(const char *str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}
