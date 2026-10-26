#include <solis/user_elf.h>
#include <solis/pmm.h>

extern void kernel_panic(const char *message) __attribute__((noreturn));

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_LOAD 1
#define PF_R 4
#define PF_X 1
#define PF_W 2
#define ELF_PAGE_MASK (PMM_PAGE_SIZE - 1)

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_names;
} __attribute__((packed));

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

static bool elf_range_valid(uint64_t start, uint64_t size, uint64_t limit) {
    return start <= limit && size <= limit - start;
}

static bool elf_is_power_of_two(uint64_t value) {
    return value && !(value & (value - 1));
}

static bool elf_header_valid(const uint8_t *image, size_t image_size,
                             struct elf64_header *header) {
    if (!image || !header || image_size < sizeof(*header)) return false;
    for (size_t i = 0; i < sizeof(header->ident); i++)
        header->ident[i] = image[i];
    if (header->ident[0] != 0x7F || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != ELFCLASS64 ||
        header->ident[5] != ELFDATA2LSB ||
        header->ident[6] != EV_CURRENT)
        return false;
    const uint8_t *source = image + sizeof(header->ident);
    uint8_t *destination = (uint8_t *)header + sizeof(header->ident);
    for (size_t i = sizeof(header->ident); i < sizeof(*header); i++)
        destination[i - sizeof(header->ident)] = source[i - sizeof(header->ident)];

    if (header->type != ET_EXEC || header->machine != EM_X86_64 ||
        header->version != EV_CURRENT || header->header_size != sizeof(*header) ||
        header->program_entry_size != sizeof(struct elf64_program_header) ||
        header->program_count == 0 ||
        !elf_range_valid(header->program_offset,
                         (uint64_t)header->program_count *
                             header->program_entry_size,
                         image_size))
        return false;
    return true;
}

static bool elf_read_program_header(const uint8_t *image,
                                    const struct elf64_header *header,
                                    uint16_t index,
                                    struct elf64_program_header *program) {
    uint64_t offset = header->program_offset +
                      (uint64_t)index * header->program_entry_size;
    const uint8_t *source = image + offset;
    uint8_t *destination = (uint8_t *)program;
    for (size_t i = 0; i < sizeof(*program); i++) destination[i] = source[i];
    return true;
}

static bool elf_load_segment(const uint8_t *image, size_t image_size,
                            struct vmm_address_space *space,
                            const struct elf64_program_header *program) {
    if (program->memory_size == 0 ||
        program->file_size > program->memory_size ||
        !(program->flags & PF_R) ||
        ((program->flags & (PF_W | PF_X)) == (PF_W | PF_X)) ||
        !elf_range_valid(program->offset, program->file_size, image_size) ||
        program->virtual_address < VMM_USER_SPACE_START ||
        !elf_range_valid(program->virtual_address, program->memory_size,
                         VMM_USER_SPACE_END))
        return false;

    if (program->alignment > 1 &&
        (!elf_is_power_of_two(program->alignment) ||
         (program->virtual_address & (program->alignment - 1)) !=
             (program->offset & (program->alignment - 1))))
        return false;
    if ((program->virtual_address & ELF_PAGE_MASK) !=
        (program->offset & ELF_PAGE_MASK))
        return false;

    uint64_t page_start = program->virtual_address & ~ELF_PAGE_MASK;
    uint64_t segment_end = program->virtual_address + program->memory_size;
    if (segment_end > UINT64_MAX - ELF_PAGE_MASK) return false;
    uint64_t page_end = (segment_end + ELF_PAGE_MASK) & ~ELF_PAGE_MASK;
    if (page_start < VMM_USER_SPACE_START || page_end > VMM_USER_SPACE_END)
        return false;

    uint64_t flags = VMM_PAGE_USER;
    if (program->flags & PF_W) flags |= VMM_PAGE_WRITABLE;
    if (!(program->flags & PF_X) && vmm_nx_enabled())
        flags |= VMM_PAGE_NO_EXECUTE;

    for (uint64_t page_address = page_start; page_address < page_end;
         page_address += PMM_PAGE_SIZE) {
        uint8_t *page = (uint8_t *)vmm_address_space_map_alloc(
            space, (uintptr_t)page_address, flags);
        if (!page) return false;

        uint64_t copy_start = page_address > program->virtual_address
                                  ? page_address : program->virtual_address;
        uint64_t file_end = program->virtual_address + program->file_size;
        uint64_t copy_end = page_address + PMM_PAGE_SIZE;
        if (copy_end > file_end) copy_end = file_end;
        if (copy_start < copy_end) {
            uint64_t source_offset = program->offset +
                                     (copy_start - program->virtual_address);
            uint64_t destination_offset = copy_start - page_address;
            uint64_t copy_size = copy_end - copy_start;
            for (uint64_t i = 0; i < copy_size; i++)
                page[destination_offset + i] = image[source_offset + i];
        }
    }
    return true;
}

static bool elf_entry_is_executable(const uint8_t *image,
                                    const struct elf64_header *header) {
    for (uint16_t i = 0; i < header->program_count; i++) {
        struct elf64_program_header program;
        elf_read_program_header(image, header, i, &program);
        if (program.type != PT_LOAD || !(program.flags & PF_X) ||
            program.memory_size == 0 ||
            program.virtual_address > header->entry)
            continue;
        if (header->entry - program.virtual_address < program.memory_size)
            return true;
    }
    return false;
}

static void elf_cleanup(struct vmm_address_space *space) {
    if (!vmm_address_space_destroy(space))
        kernel_panic("Unable to clean up rejected ELF64 address space");
}

bool user_elf_load(const uint8_t *image, size_t image_size,
                   struct vmm_address_space *space, uintptr_t *entry) {
    if (!space || !entry || space->root) return false;
    struct elf64_header header;
    if (!elf_header_valid(image, image_size, &header) ||
        header.entry < VMM_USER_SPACE_START ||
        header.entry >= VMM_USER_SPACE_END ||
        !elf_entry_is_executable(image, &header))
        return false;
    if (!vmm_address_space_create(space)) return false;

    for (uint16_t i = 0; i < header.program_count; i++) {
        struct elf64_program_header program;
        elf_read_program_header(image, &header, i, &program);
        if (program.type == PT_DYNAMIC || program.type == PT_INTERP) {
            elf_cleanup(space);
            return false;
        }
        if (program.type == PT_LOAD &&
            !elf_load_segment(image, image_size, space, &program)) {
            elf_cleanup(space);
            return false;
        }
    }
    *entry = (uintptr_t)header.entry;
    return true;
}
