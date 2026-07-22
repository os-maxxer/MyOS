#ifndef NYX_RAMFS_H
#define NYX_RAMFS_H

#include <nyx/types.h>

#define RAMFS_MAX_FILES 16
#define RAMFS_MAX_NAME 32
#define RAMFS_MAX_SIZE 4096

void ramfs_init(void);
int ramfs_create(const char *name);
int ramfs_open(const char *name);
int ramfs_read(int fd, uint8_t *buf, uint32_t size);
int ramfs_write(int fd, const uint8_t *buf, uint32_t size);
int ramfs_delete(const char *name);
int ramfs_list(char names[][RAMFS_MAX_NAME], int max);
int ramfs_get_size(int fd);

#endif
