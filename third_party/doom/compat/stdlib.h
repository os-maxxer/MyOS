#ifndef SOLIS_DOOM_STDLIB_H
#define SOLIS_DOOM_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atexit(void (*fn)(void));

int atoi(const char *text);
long atol(const char *text);
double atof(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);

int abs(int value);
long labs(long value);
int rand(void);
void srand(unsigned int seed);
char *getenv(const char *name);
int system(const char *command);
void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));

#endif
