#ifndef SOLIS_DBG_H
#define SOLIS_DBG_H

#include <stdbool.h>
#include <solis/types.h>

#define DBG_DRIVER_NAME_LEN 16
#define DBG_PIPELINE_LEN 24
#define DBG_MAX_DRIVERS 8
#define DBG_MAX_IRQS 16

enum dbg_driver_state {
    DBG_DRIVER_UNINITIALIZED = 0,
    DBG_DRIVER_ACTIVE = 1,
    DBG_DRIVER_FAILED = 2,
    DBG_DRIVER_STOPPED = 3
};

struct dbg_driver_entry {
    char name[DBG_DRIVER_NAME_LEN];
    uint8_t state;
    uint32_t last_tick;
};

struct dbg_health {
    uint32_t text_crc;
    uint32_t rodata_crc;
    uint32_t total_irqs;
    uint32_t irq_counts[DBG_MAX_IRQS];
    uint32_t memory_used_kb;
    uint32_t memory_total_kb;
    char pipeline[DBG_PIPELINE_LEN];
    uint8_t driver_count;
    struct dbg_driver_entry drivers[DBG_MAX_DRIVERS];
};

void dbg_init(void);
void dbg_print(const char *s);
void dbg_print_hex(uint32_t val);
void dbg_print_dec(uint32_t val);
int  dbg_read(char *buf, int max);
void dbg_set_driver_state(const char *name, uint8_t state);
void dbg_set_pipeline_stage(const char *stage);
void dbg_note_irq(uint8_t irq);
void dbg_set_memory_metrics(uint32_t used_kb, uint32_t total_kb);
void dbg_get_health(struct dbg_health *out);

#endif
