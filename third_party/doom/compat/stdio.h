#ifndef SOLIS_DOOM_STDIO_H
#define SOLIS_DOOM_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct solis_doom_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *file);
size_t fread(void *ptr, size_t size, size_t count, FILE *file);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *file);
int fseek(FILE *file, long offset, int origin);
long ftell(FILE *file);
int feof(FILE *file);
int fflush(FILE *file);
int fgetc(FILE *file);
int fputc(int ch, FILE *file);
char *fgets(char *text, int size, FILE *file);
int puts(const char *text);
int getchar(void);
int putchar(int ch);
int printf(const char *format, ...);
int fprintf(FILE *file, const char *format, ...);
int sprintf(char *out, const char *format, ...);
int snprintf(char *out, size_t size, const char *format, ...);
int vprintf(const char *format, va_list args);
int vfprintf(FILE *file, const char *format, va_list args);
int vsprintf(char *out, const char *format, va_list args);
int vsnprintf(char *out, size_t size, const char *format, va_list args);
int sscanf(const char *text, const char *format, ...);
int fscanf(FILE *file, const char *format, ...);
int remove(const char *path);
int rename(const char *from, const char *to);
void perror(const char *text);

#endif
