/*
 * Interrupt descriptor table setup and common interrupt handling.
 */

#include <solis/idt.h>
#include <solis/console.h>
#include <solis/interrupts.h>
#include <solis/dbg.h>

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t base_middle;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void load_idt(const struct idt_ptr *idt_ptr);
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_middle = (base >> 16) & 0xFFFF;
    idt[num].base_high = (uint32_t)(base >> 32);
    idt[num].selector = selector;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

static void idt_set_ist_gate(uint8_t num, uint64_t base, uint8_t ist) {
    idt_set_gate(num, base, 0x08, 0x8E);
    idt[num].ist = ist & 0x07;
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)(uintptr_t)&idt;

    for (int i = 0; i < 256; ++i) {
        idt_set_gate(i, 0, 0x08, 0);
    }

    idt_set_gate(0, (uintptr_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uintptr_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uintptr_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uintptr_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uintptr_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uintptr_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uintptr_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uintptr_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uintptr_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uintptr_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uintptr_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uintptr_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uintptr_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uintptr_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uintptr_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uintptr_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uintptr_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uintptr_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uintptr_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uintptr_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uintptr_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uintptr_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uintptr_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uintptr_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uintptr_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uintptr_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uintptr_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uintptr_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uintptr_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uintptr_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uintptr_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uintptr_t)isr31, 0x08, 0x8E);
    idt_set_ist_gate(2, (uintptr_t)isr2, 2);
    idt_set_ist_gate(8, (uintptr_t)isr8, 1);
    idt_set_ist_gate(18, (uintptr_t)isr18, 3);

    idt_set_gate(32, (uintptr_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uintptr_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uintptr_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uintptr_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uintptr_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uintptr_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uintptr_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uintptr_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uintptr_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uintptr_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uintptr_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uintptr_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uintptr_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uintptr_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uintptr_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uintptr_t)irq15, 0x08, 0x8E);

    load_idt(&idtp);
}

static void (*irq_handlers[16])(void) = {0};

extern void kernel_panic(const char *message) __attribute__((noreturn));

static const char *exception_name(uint64_t vector) {
    static const char *const names[32] = {
        "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
        "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
        "Segment Not Present", "Stack-Segment Fault", "General Protection",
        "Page Fault", "Reserved", "x87 Floating-Point", "Alignment Check",
        "Machine Check", "SIMD Floating-Point", "Virtualization",
        "Control Protection", "Reserved", "Reserved", "Reserved", "Reserved",
        "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
        "Security", "Reserved"
    };
    return vector < 32 ? names[vector] : "Unknown";
}

void isr_handler_register(uint8_t vector, void (*handler)(void)) {
    uint8_t ist = idt[vector].ist;
    idt_set_gate(vector, (uintptr_t)handler, 0x08, 0x8E);
    idt[vector].ist = ist;
}

void irq_register_handler(uint8_t irq, void (*handler)(void)) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void isr_common_handler(struct registers *regs) {
    if (regs->int_no == 3) return;

    console_write("[FAULT] ");
    console_write(exception_name(regs->int_no));
    console_write(" vector=");
    console_write_dec(regs->int_no);
    console_write("\n");
    console_write("  rip=");
    console_write_hex64(regs->rip);
    console_write(" err=");
    console_write_hex64(regs->err_code);
    console_write(" rbp=");
    console_write_hex64(regs->rbp);
    console_write(" rax=");
    console_write_hex64(regs->rax);
    console_write(" rbx=");
    console_write_hex64(regs->rbx);
    console_write(" rcx=");
    console_write_hex64(regs->rcx);
    console_write(" rdx=");
    console_write_hex64(regs->rdx);
    console_write(" rsi=");
    console_write_hex64(regs->rsi);
    console_write(" rdi=");
    console_write_hex64(regs->rdi);
    console_write("\n");

    if (regs->int_no == 14) {
        uint64_t fault_address;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_address));
        console_write("  cr2=");
        console_write_hex64(fault_address);
        console_write(" access=");
        console_write((regs->err_code & 1) ? "protection" : "not-present");
        console_write((regs->err_code & 2) ? ",write" : ",read");
        console_write((regs->err_code & 4) ? ",user" : ",supervisor");
        if (regs->err_code & 8) console_write(",reserved-bit");
        if (regs->err_code & 16) console_write(",instruction-fetch");
        console_write("\n");
    }

    kernel_panic("Unhandled CPU exception");
}

void irq_common_handler(struct registers *regs) {
    if (regs->int_no < 32 || regs->int_no >= 48) return;
    uint8_t irq = (uint8_t)(regs->int_no - 32);
    dbg_note_irq(irq);
    if (irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq]();
    }
}
