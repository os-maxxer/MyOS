#ifndef SOLIS_DOOM_SYS_STAT_H
#define SOLIS_DOOM_SYS_STAT_H

#include <sys/types.h>

struct stat {
    unsigned int st_mode;
    long st_size;
};

#define S_IFDIR 0040000
#define S_ISDIR(mode) (((mode) & S_IFDIR) != 0)

int stat(const char *path, struct stat *out);
int mkdir(const char *path, mode_t mode);

#endif
