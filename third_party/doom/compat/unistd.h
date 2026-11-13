#ifndef SOLIS_DOOM_UNISTD_H
#define SOLIS_DOOM_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

int isatty(int fd);
int fileno(void *stream);
int usleep(unsigned int microseconds);
int close(int fd);
long read(int fd, void *buffer, size_t size);
long write(int fd, const void *buffer, size_t size);
int unlink(const char *path);
char *getcwd(char *buffer, size_t size);
int chdir(const char *path);

#endif
