#ifndef NYX_NOFS_H
#define NYX_NOFS_H

#include <nyx/types.h>

#define NOFS_MAX_FILES    32
#define NOFS_MAX_NAME     24
#define NOFS_BLOCK_SIZE   512
#define NOFS_MAX_FILE_SIZE (8157 * NOFS_BLOCK_SIZE)

void nofs_init(void);
int  nofs_create(const char *name);
int  nofs_open(const char *name);
int  nofs_read(int fd, uint8_t *buf, uint32_t size);
int  nofs_write(int fd, const uint8_t *buf, uint32_t size);
int  nofs_delete(const char *name);
int  nofs_list(char names[][NOFS_MAX_NAME], int max);
int  nofs_get_size(int fd);
uint64_t nofs_get_machine_id(void);

#endif
