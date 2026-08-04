#ifndef SOLIS_VFS_H
#define SOLIS_VFS_H

#include <solis/types.h>
#include <solis/solfs.h>

void vfs_init(void);
const char* vfs_get_cwd(void);
int vfs_cd(const char *path);
int vfs_mkdir(const char *path);
int vfs_ls(char names[][SOLFS_MAX_NAME], int max);
int vfs_ls_at(const char *path, char names[][SOLFS_MAX_NAME], int max);
int vfs_create(const char *path);
int vfs_open(const char *path);
int vfs_read(int fd, uint8_t *buf, uint32_t size);
int vfs_write(int fd, const uint8_t *buf, uint32_t size);
int vfs_delete(const char *path);
int vfs_get_size(int fd);

#endif