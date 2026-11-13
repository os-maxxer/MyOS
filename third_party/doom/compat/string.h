#ifndef SOLIS_DOOM_STRING_H
#define SOLIS_DOOM_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t size);
void *memmove(void *dst, const void *src, size_t size);
void *memset(void *dst, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);

size_t strlen(const char *text);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t size);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t size);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t size);
char *strchr(const char *text, int ch);
char *strrchr(const char *text, int ch);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *text);
char *strerror(int error);

#endif
