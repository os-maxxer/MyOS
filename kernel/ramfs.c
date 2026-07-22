#include <nyx/ramfs.h>
#include <stdbool.h>

static struct {
    char name[RAMFS_MAX_NAME];
    uint8_t data[RAMFS_MAX_SIZE];
    uint32_t size;
    bool used;
} files[RAMFS_MAX_FILES];

void ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        files[i].used = false;
        files[i].size = 0;
        files[i].name[0] = '\0';
    }
}

static int ramfs_find(const char *name) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        const char *a = name;
        const char *b = files[i].name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') return i;
    }
    return -1;
}

int ramfs_create(const char *name) {
    if (!name || !name[0]) return -1;
    if (ramfs_find(name) >= 0) return -1;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used) {
            int j = 0;
            while (name[j] && j < RAMFS_MAX_NAME - 1) {
                files[i].name[j] = name[j];
                j++;
            }
            files[i].name[j] = '\0';
            files[i].size = 0;
            files[i].used = true;
            return i;
        }
    }
    return -1;
}

int ramfs_open(const char *name) {
    return ramfs_find(name);
}

int ramfs_read(int fd, uint8_t *buf, uint32_t size) {
    if (fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used) return -1;
    uint32_t to_read = size;
    if (to_read > files[fd].size) to_read = files[fd].size;
    for (uint32_t i = 0; i < to_read; i++)
        buf[i] = files[fd].data[i];
    return (int)to_read;
}

int ramfs_write(int fd, const uint8_t *buf, uint32_t size) {
    if (fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used) return -1;
    uint32_t to_write = size;
    if (to_write > RAMFS_MAX_SIZE) to_write = RAMFS_MAX_SIZE;
    for (uint32_t i = 0; i < to_write; i++)
        files[fd].data[i] = buf[i];
    files[fd].size = to_write;
    return (int)to_write;
}

int ramfs_delete(const char *name) {
    int fd = ramfs_find(name);
    if (fd < 0) return -1;
    files[fd].used = false;
    files[fd].size = 0;
    files[fd].name[0] = '\0';
    return 0;
}

int ramfs_list(char names[][RAMFS_MAX_NAME], int max) {
    int count = 0;
    for (int i = 0; i < RAMFS_MAX_FILES && count < max; i++) {
        if (files[i].used) {
            int j = 0;
            while (files[i].name[j]) {
                names[count][j] = files[i].name[j];
                j++;
            }
            names[count][j] = '\0';
            count++;
        }
    }
    return count;
}

int ramfs_get_size(int fd) {
    if (fd < 0 || fd >= RAMFS_MAX_FILES || !files[fd].used) return -1;
    return (int)files[fd].size;
}
