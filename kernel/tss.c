#include <solis/tss.h>
#include <stddef.h>

#define TSS_STACK_SIZE 16384

_Static_assert(sizeof(struct x86_64_tss) == 104, "x86-64 TSS layout must be 104 bytes");

static struct x86_64_tss kernel_tss;
static uint8_t ring0_stack[TSS_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t double_fault_stack[TSS_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t nmi_stack[TSS_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t machine_check_stack[TSS_STACK_SIZE] __attribute__((aligned(16)));

void tss_init(void) {
    kernel_tss = (struct x86_64_tss){0};
    kernel_tss.rsp0 = (uintptr_t)(ring0_stack + sizeof(ring0_stack));
    kernel_tss.ist1 = (uintptr_t)(double_fault_stack + sizeof(double_fault_stack));
    kernel_tss.ist2 = (uintptr_t)(nmi_stack + sizeof(nmi_stack));
    kernel_tss.ist3 = (uintptr_t)(machine_check_stack + sizeof(machine_check_stack));
    kernel_tss.iomap_base = sizeof(kernel_tss);
}

const struct x86_64_tss *tss_get(void) {
    return &kernel_tss;
}
