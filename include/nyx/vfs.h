#ifndef NYX_VFS_H
#define NYX_VFS_H

#include <nyx/types.h>
#include <nyx/nofs.h>

void vfs_init(void);
const char* vfs_get_cwd(void);
int vfs_cd(const char *path);
int vfs_mkdir(const char *path);
int vfs_ls(char names[][NOFS_MAX_NAME], int max);
int vfs_ls_at(const char *path, char names[][NOFS_MAX_NAME], int max);
int vfs_create(const char *path);
int vfs_open(const char *path);
int vfs_read(int fd, uint8_t *buf, uint32_t size);
int vfs_write(int fd, const uint8_t *buf, uint32_t size);
int vfs_delete(const char *path);
int vfs_get_size(int fd);

#endif