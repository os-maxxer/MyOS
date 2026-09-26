#include <solis/solfs.h>
#include <solis/ata.h>
#include <solis/console.h>
#include <stdbool.h>

#define SOLFS_MAGIC       0x53464F53
#define SOLFS_VERSION     1
#define SOLFS_FAT_ENTRIES 8192
#define SOLFS_DIR_ENTRIES 32
#define SOLFS_FAT_SECTORS 32
#define SOLFS_DIR_SECTORS 2
#define SOLFS_DATA_START  35
#define SOLFS_FAT_EOF     0xFFFF
#define SOLFS_FAT_FREE    0x0000

struct solfs_superblock {
    uint32_t magic;
    uint32_t version;
    uint16_t block_size;
    uint32_t total_sectors;
    uint32_t fat_start;
    uint32_t fat_sectors;
    uint32_t root_dir_start;
    uint32_t root_dir_entries;
    uint32_t data_start;
    uint64_t machine_id;
    uint8_t  padding[470];
} __attribute__((packed));

struct solfs_dirent {
    char     name[24];
    uint8_t  attrs;
    uint16_t first_cluster;
    uint32_t file_size;
    uint8_t  padding[1];
} __attribute__((packed));

static uint16_t fat_cache[SOLFS_FAT_ENTRIES];
static uint8_t  dir_cache[SOLFS_DIR_ENTRIES * sizeof(struct solfs_dirent)];
static bool     fat_dirty = false;
static bool     dir_dirty = false;
static uint64_t cached_machine_id = 0;

static struct solfs_dirent* dirent(int index) {
    return (struct solfs_dirent*)dir_cache + index;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int sync_fat(void) {
    if (!fat_dirty) return 0;
    for (uint32_t i = 0; i < SOLFS_FAT_SECTORS; i++) {
        uint8_t *sector = (uint8_t*)fat_cache + i * ATA_SECTOR_SIZE;
        if (ata_write_sector(1 + i, sector) != 0)
            return -1;
    }
    fat_dirty = false;
    return 0;
}

static int sync_dir(void) {
    if (!dir_dirty) return 0;
    for (uint32_t i = 0; i < SOLFS_DIR_SECTORS; i++) {
        uint8_t *sector = dir_cache + i * ATA_SECTOR_SIZE;
        if (ata_write_sector(SOLFS_DATA_START - SOLFS_DIR_SECTORS + i, sector) != 0)
            return -1;
    }
    dir_dirty = false;
    return 0;
}

static int sync_all(void) {
    if (sync_fat() != 0) return -1;
    if (sync_dir() != 0) return -1;
    return 0;
}

static void free_chain(uint16_t cl) {
    while (cl >= 2 && cl < SOLFS_FAT_EOF) {
        uint16_t next = fat_cache[cl];
        fat_cache[cl] = SOLFS_FAT_FREE;
        cl = next;
    }
    fat_dirty = true;
}

static uint16_t alloc_cluster(void) {
    for (uint16_t i = 2; i < SOLFS_FAT_ENTRIES; i++) {
        if (fat_cache[i] == SOLFS_FAT_FREE) {
            fat_cache[i] = SOLFS_FAT_EOF;
            fat_dirty = true;
            return i;
        }
    }
    return 0;
}

void solfs_init(void) {
    struct solfs_superblock sb;
    uint8_t *sector = (uint8_t*)&sb;

    if (!ata_present()) {
        for (int i = 0; i < SOLFS_FAT_ENTRIES; i++)
            fat_cache[i] = 0;
        fat_cache[0] = 0x0001;
        fat_cache[1] = 0x0001;
        for (int i = 0; i < (int)sizeof(dir_cache); i++)
            dir_cache[i] = 0;
        cached_machine_id = 0x494C4F53; // "SOLI"
        return;
    }

    ata_read_sector(0, sector);

    if (sb.magic == SOLFS_MAGIC && sb.version == SOLFS_VERSION) {
        cached_machine_id = sb.machine_id;
        for (uint32_t i = 0; i < SOLFS_FAT_SECTORS; i++)
            ata_read_sector(1 + i, (uint8_t*)fat_cache + i * ATA_SECTOR_SIZE);
        for (uint32_t i = 0; i < SOLFS_DIR_SECTORS; i++)
            ata_read_sector(SOLFS_DATA_START - SOLFS_DIR_SECTORS + i, dir_cache + i * ATA_SECTOR_SIZE);
    } else {
        for (int i = 0; i < (int)sizeof(fat_cache); i++)
            ((uint8_t*)fat_cache)[i] = 0;
        fat_cache[0] = 0x0001;
        fat_cache[1] = 0x0001;
        fat_dirty = true;
        sync_fat();

        for (int i = 0; i < (int)sizeof(dir_cache); i++)
            dir_cache[i] = 0;
        dir_dirty = true;
        sync_dir();

        cached_machine_id = 0;
        const char *sn = ata_get_serial();
        for (int i = 0; sn[i] && i < 8; i++)
            cached_machine_id = (cached_machine_id << 8) | (uint8_t)sn[i];

        sb.magic = SOLFS_MAGIC;
        sb.version = SOLFS_VERSION;
        sb.block_size = ATA_SECTOR_SIZE;
        sb.total_sectors = 0;
        sb.fat_start = 1;
        sb.fat_sectors = SOLFS_FAT_SECTORS;
        sb.root_dir_start = SOLFS_DATA_START - SOLFS_DIR_SECTORS;
        sb.root_dir_entries = SOLFS_DIR_ENTRIES;
        sb.data_start = SOLFS_DATA_START;
        sb.machine_id = cached_machine_id;
        ata_write_sector(0, sector);
    }
}

int solfs_create(const char *name) {
    if (!name || !name[0]) return -1;
    if (solfs_open(name) >= 0) return -1;

    for (int i = 0; i < SOLFS_DIR_ENTRIES; i++) {
        struct solfs_dirent *d = dirent(i);
        if (d->name[0] == '\0') {
            str_cpy(d->name, name, sizeof(d->name));
            d->attrs = 0;
            d->first_cluster = 0;
            d->file_size = 0;
            dir_dirty = true;
            sync_dir();
            return i;
        }
    }
    return -1;
}

int solfs_open(const char *name) {
    for (int i = 0; i < SOLFS_DIR_ENTRIES; i++) {
        struct solfs_dirent *d = dirent(i);
        if (d->name[0] != '\0' && str_eq(d->name, name))
            return i;
    }
    return -1;
}

int solfs_read(int fd, uint8_t *buf, uint32_t size) {
    if (fd < 0 || fd >= SOLFS_DIR_ENTRIES) return -1;
    struct solfs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;

    uint32_t to_read = size;
    if (to_read > d->file_size) to_read = d->file_size;

    uint16_t cl = d->first_cluster;
    uint32_t pos = 0;

    while (cl >= 2 && cl < SOLFS_FAT_EOF && pos < to_read) {
        uint32_t lba = SOLFS_DATA_START + (cl - 2);
        uint8_t sector[ATA_SECTOR_SIZE];
        if (ata_read_sector(lba, sector) != 0) break;

        uint32_t chunk = to_read - pos;
        if (chunk > ATA_SECTOR_SIZE) chunk = ATA_SECTOR_SIZE;
        for (uint32_t i = 0; i < chunk; i++)
            buf[pos++] = sector[i];

        cl = fat_cache[cl];
    }

    return (int)pos;
}

int solfs_write(int fd, const uint8_t *buf, uint32_t size) {
    if (fd < 0 || fd >= SOLFS_DIR_ENTRIES) return -1;
    struct solfs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;

    if (size == 0) {
        free_chain(d->first_cluster);
        d->first_cluster = 0;
        d->file_size = 0;
        dir_dirty = true;
        sync_dir();
        return 0;
    }

    uint32_t needed = (size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    uint32_t free_count = 0;
    for (uint16_t i = 2; i < SOLFS_FAT_ENTRIES; i++) {
        if (fat_cache[i] == SOLFS_FAT_FREE) free_count++;
    }
    if (free_count < needed) return -1;

    free_chain(d->first_cluster);
    d->first_cluster = 0;
    uint16_t first = 0;
    uint16_t prev = 0;
    uint32_t written = 0;

    for (uint32_t i = 0; i < needed; i++) {
        uint16_t cl = alloc_cluster();
        if (cl == 0) break;

        if (prev == 0)
            first = cl;
        else
            fat_cache[prev] = cl;

        uint8_t sector[ATA_SECTOR_SIZE];
        uint32_t chunk = size - written;
        if (chunk > ATA_SECTOR_SIZE) chunk = ATA_SECTOR_SIZE;
        for (uint32_t j = 0; j < chunk; j++)
            sector[j] = buf[written++];
        for (uint32_t j = chunk; j < ATA_SECTOR_SIZE; j++)
            sector[j] = 0;

        uint32_t lba = SOLFS_DATA_START + (cl - 2);
        ata_write_sector(lba, sector);

        prev = cl;
    }

    if (prev)
        fat_cache[prev] = SOLFS_FAT_EOF;

    d->first_cluster = first;
    d->file_size = written;
    dir_dirty = true;
    sync_all();
    return (int)written;
}

int solfs_delete(const char *name) {
    int fd = solfs_open(name);
    if (fd < 0) return -1;

    struct solfs_dirent *d = dirent(fd);
    free_chain(d->first_cluster);
    d->name[0] = '\0';
    d->first_cluster = 0;
    d->file_size = 0;
    dir_dirty = true;
    sync_all();
    return 0;
}

int solfs_list(char names[][SOLFS_MAX_NAME], int max) {
    int count = 0;
    for (int i = 0; i < SOLFS_DIR_ENTRIES && count < max; i++) {
        struct solfs_dirent *d = dirent(i);
        if (d->name[0] != '\0') {
            str_cpy(names[count], d->name, SOLFS_MAX_NAME);
            count++;
        }
    }
    return count;
}

int solfs_get_size(int fd) {
    if (fd < 0 || fd >= SOLFS_DIR_ENTRIES) return -1;
    struct solfs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;
    return (int)d->file_size;
}

int solfs_mkdir(const char *name) {
    if (!name || !name[0]) return -1;
    if (solfs_open(name) >= 0) return -1;

    for (int i = 0; i < SOLFS_DIR_ENTRIES; i++) {
        struct solfs_dirent *d = dirent(i);
        if (d->name[0] == '\0') {
            str_cpy(d->name, name, sizeof(d->name));
            d->attrs = SOLFS_ATTR_DIR;
            d->first_cluster = 0;
            d->file_size = 0;
            dir_dirty = true;
            sync_dir();
            return i;
        }
    }
    return -1;
}

int solfs_isdir(int fd) {
    if (fd < 0 || fd >= SOLFS_DIR_ENTRIES) return -1;
    struct solfs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;
    return (d->attrs & SOLFS_ATTR_DIR) ? 1 : 0;
}

uint64_t solfs_get_machine_id(void) {
    return cached_machine_id;
}
