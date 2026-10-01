#ifndef STRING_H
#define STRING_H
#include <stdint.h>
#include <stdbool.h>

void str_cpy(char* dest, const char* src);
void str_cat(char* dest, const char* src);
bool str_starts_with(const char* str, const char* prefix);
int str_cmp(const char* a, const char* b);
int mem_cmp(const void *s1, const void *s2, uint32_t n);

#endif
int str_len(const char *str);
