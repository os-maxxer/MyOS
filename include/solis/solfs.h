#ifndef SOLIS_SOLFS_H
#define SOLIS_SOLFS_H

#include <solis/types.h>

#define SOLFS_MAX_FILES    32
#define SOLFS_MAX_NAME     24
#define SOLFS_BLOCK_SIZE   512
#define SOLFS_MAX_FILE_SIZE (8157 * SOLFS_BLOCK_SIZE)
#define SOLFS_ATTR_DIR     0x01

void solfs_init(void);
int  solfs_create(const char *name);
int  solfs_open(const char *name);
int  solfs_read(int fd, uint8_t *buf, uint32_t size);
int  solfs_write(int fd, const uint8_t *buf, uint32_t size);
int  solfs_delete(const char *name);
int  solfs_list(char names[][SOLFS_MAX_NAME], int max);
int  solfs_get_size(int fd);
int  solfs_mkdir(const char *name);
int  solfs_isdir(int fd);
uint64_t solfs_get_machine_id(void);

#endif
