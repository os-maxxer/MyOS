#include <nyx/nofs.h>
#include <nyx/ata.h>
#include <nyx/console.h>
#include <stdbool.h>

#define NOFS_MAGIC       0x53464F4E
#define NOFS_VERSION     1
#define NOFS_FAT_ENTRIES 8192
#define NOFS_DIR_ENTRIES 32
#define NOFS_FAT_SECTORS 32
#define NOFS_DIR_SECTORS 2
#define NOFS_DATA_START  35
#define NOFS_FAT_EOF     0xFFFF
#define NOFS_FAT_FREE    0x0000

struct nofs_superblock {
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

struct nofs_dirent {
    char     name[24];
    uint8_t  attrs;
    uint16_t first_cluster;
    uint32_t file_size;
    uint8_t  padding[1];
} __attribute__((packed));

static uint16_t fat_cache[NOFS_FAT_ENTRIES];
static uint8_t  dir_cache[NOFS_DIR_ENTRIES * sizeof(struct nofs_dirent)];
static bool     fat_dirty = false;
static bool     dir_dirty = false;
static uint64_t cached_machine_id = 0;

static struct nofs_dirent* dirent(int index) {
    return (struct nofs_dirent*)dir_cache + index;
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
    for (uint32_t i = 0; i < NOFS_FAT_SECTORS; i++) {
        uint8_t *sector = (uint8_t*)fat_cache + i * ATA_SECTOR_SIZE;
        if (ata_write_sector(1 + i, sector) != 0)
            return -1;
    }
    fat_dirty = false;
    return 0;
}

static int sync_dir(void) {
    if (!dir_dirty) return 0;
    for (uint32_t i = 0; i < NOFS_DIR_SECTORS; i++) {
        uint8_t *sector = dir_cache + i * ATA_SECTOR_SIZE;
        if (ata_write_sector(NOFS_DATA_START - NOFS_DIR_SECTORS + i, sector) != 0)
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
    while (cl >= 2 && cl < NOFS_FAT_EOF) {
        uint16_t next = fat_cache[cl];
        fat_cache[cl] = NOFS_FAT_FREE;
        cl = next;
    }
    fat_dirty = true;
}

static uint16_t alloc_cluster(void) {
    for (uint16_t i = 2; i < NOFS_FAT_ENTRIES; i++) {
        if (fat_cache[i] == NOFS_FAT_FREE) {
            fat_cache[i] = NOFS_FAT_EOF;
            fat_dirty = true;
            return i;
        }
    }
    return 0;
}

void nofs_init(void) {
    struct nofs_superblock sb;
    uint8_t *sector = (uint8_t*)&sb;

    if (!ata_present()) {
        console_write("NOFS: no disk present, using RAM-only mode.\n");
        for (int i = 0; i < NOFS_FAT_ENTRIES; i++)
            fat_cache[i] = 0;
        fat_cache[0] = 0x0001;
        fat_cache[1] = 0x0001;
        for (int i = 0; i < (int)sizeof(dir_cache); i++)
            dir_cache[i] = 0;
        cached_machine_id = 0x4E59584F; // "NYXO"
        return;
    }

    ata_read_sector(0, sector);

    if (sb.magic == NOFS_MAGIC && sb.version == NOFS_VERSION) {
        console_write("NOFS: mounted existing filesystem.\n");
        cached_machine_id = sb.machine_id;
        for (uint32_t i = 0; i < NOFS_FAT_SECTORS; i++)
            ata_read_sector(1 + i, (uint8_t*)fat_cache + i * ATA_SECTOR_SIZE);
        for (uint32_t i = 0; i < NOFS_DIR_SECTORS; i++)
            ata_read_sector(NOFS_DATA_START - NOFS_DIR_SECTORS + i, dir_cache + i * ATA_SECTOR_SIZE);
    } else {
        console_write("NOFS: formatting disk...\n");

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

        sb.magic = NOFS_MAGIC;
        sb.version = NOFS_VERSION;
        sb.block_size = ATA_SECTOR_SIZE;
        sb.total_sectors = 0;
        sb.fat_start = 1;
        sb.fat_sectors = NOFS_FAT_SECTORS;
        sb.root_dir_start = NOFS_DATA_START - NOFS_DIR_SECTORS;
        sb.root_dir_entries = NOFS_DIR_ENTRIES;
        sb.data_start = NOFS_DATA_START;
        sb.machine_id = cached_machine_id;
        ata_write_sector(0, sector);

        console_write("NOFS: format complete.\n");
    }
}

int nofs_create(const char *name) {
    if (!name || !name[0]) return -1;
    if (nofs_open(name) >= 0) return -1;

    for (int i = 0; i < NOFS_DIR_ENTRIES; i++) {
        struct nofs_dirent *d = dirent(i);
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

int nofs_open(const char *name) {
    for (int i = 0; i < NOFS_DIR_ENTRIES; i++) {
        struct nofs_dirent *d = dirent(i);
        if (d->name[0] != '\0' && str_eq(d->name, name))
            return i;
    }
    return -1;
}

int nofs_read(int fd, uint8_t *buf, uint32_t size) {
    if (fd < 0 || fd >= NOFS_DIR_ENTRIES) return -1;
    struct nofs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;

    uint32_t to_read = size;
    if (to_read > d->file_size) to_read = d->file_size;

    uint16_t cl = d->first_cluster;
    uint32_t pos = 0;

    while (cl >= 2 && cl < NOFS_FAT_EOF && pos < to_read) {
        uint32_t lba = NOFS_DATA_START + (cl - 2);
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

int nofs_write(int fd, const uint8_t *buf, uint32_t size) {
    if (fd < 0 || fd >= NOFS_DIR_ENTRIES) return -1;
    struct nofs_dirent *d = dirent(fd);
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
    for (uint16_t i = 2; i < NOFS_FAT_ENTRIES; i++) {
        if (fat_cache[i] == NOFS_FAT_FREE) free_count++;
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

        uint32_t lba = NOFS_DATA_START + (cl - 2);
        ata_write_sector(lba, sector);

        prev = cl;
    }

    if (prev)
        fat_cache[prev] = NOFS_FAT_EOF;

    d->first_cluster = first;
    d->file_size = written;
    dir_dirty = true;
    sync_all();
    return (int)written;
}

int nofs_delete(const char *name) {
    int fd = nofs_open(name);
    if (fd < 0) return -1;

    struct nofs_dirent *d = dirent(fd);
    free_chain(d->first_cluster);
    d->name[0] = '\0';
    d->first_cluster = 0;
    d->file_size = 0;
    dir_dirty = true;
    sync_all();
    return 0;
}

int nofs_list(char names[][NOFS_MAX_NAME], int max) {
    int count = 0;
    for (int i = 0; i < NOFS_DIR_ENTRIES && count < max; i++) {
        struct nofs_dirent *d = dirent(i);
        if (d->name[0] != '\0') {
            str_cpy(names[count], d->name, NOFS_MAX_NAME);
            count++;
        }
    }
    return count;
}

int nofs_get_size(int fd) {
    if (fd < 0 || fd >= NOFS_DIR_ENTRIES) return -1;
    struct nofs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;
    return (int)d->file_size;
}

int nofs_mkdir(const char *name) {
    if (!name || !name[0]) return -1;
    if (nofs_open(name) >= 0) return -1;

    for (int i = 0; i < NOFS_DIR_ENTRIES; i++) {
        struct nofs_dirent *d = dirent(i);
        if (d->name[0] == '\0') {
            str_cpy(d->name, name, sizeof(d->name));
            d->attrs = NOFS_ATTR_DIR;
            d->first_cluster = 0;
            d->file_size = 0;
            dir_dirty = true;
            sync_dir();
            return i;
        }
    }
    return -1;
}

int nofs_isdir(int fd) {
    if (fd < 0 || fd >= NOFS_DIR_ENTRIES) return -1;
    struct nofs_dirent *d = dirent(fd);
    if (d->name[0] == '\0') return -1;
    return (d->attrs & NOFS_ATTR_DIR) ? 1 : 0;
}

uint64_t nofs_get_machine_id(void) {
    return cached_machine_id;
}
