#include <solis/console.h>
#include <solis/gdt.h>
#include <solis/idt.h>
#include <solis/pic.h>
#include <solis/keyboard.h>
#include <solis/mouse.h>
#include <solis/pointer.h>
#include <solis/graphics.h>
#include <solis/window_manager.h>
#include <solis/desktop.h>
#include <solis/gui.h>
#include <solis/login.h>
#include <solis/timer.h>
#include <solis/ata.h>
#include <solis/solfs.h>
#include <solis/vfs.h>
#include <solis/spx.h>
#include <solis/net.h>
#include <solis/dbg.h>
#include <solis/rtc.h>
#include <solis/acpi.h>

extern char _binary_build_spx_terminal_spx_start[];
extern char _binary_build_spx_terminal_spx_end[];
extern char _binary_build_spx_notepad_spx_start[];
extern char _binary_build_spx_notepad_spx_end[];
extern char _binary_build_spx_paint_spx_start[];
extern char _binary_build_spx_paint_spx_end[];
extern char _binary_build_spx_settings_spx_start[];
extern char _binary_build_spx_settings_spx_end[];
extern char _binary_build_spx_filebrowser_spx_start[];
extern char _binary_build_spx_filebrowser_spx_end[];
extern char _binary_build_spx_taskmanager_spx_start[];
extern char _binary_build_spx_taskmanager_spx_end[];
extern char _binary_build_spx_pkg_spx_start[];
extern char _binary_build_spx_pkg_spx_end[];
extern char _binary_build_spx_editor_spx_start[];
extern char _binary_build_spx_editor_spx_end[];
extern char _binary_build_spx_tetris_spx_start[];
extern char _binary_build_spx_tetris_spx_end[];

void __attribute__((noreturn)) kernel_panic(const char *message) {
    console_clear();
    console_set_color(0x0F, 0x04);
    console_write("SOLIS: kernel panic\n");
    console_write(message);
    console_write("\nSystem halted.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    (void)multiboot_magic;

    console_init();
    dbg_init();

    gdt_init();
    idt_init();
    pic_remap();
    graphics_init(multiboot_info);
    graphics_vbe_init();
    if (graphics_is_ready()) {
        graphics_clear(0xFF000000);
    }

    timer_init();
    rtc_init();

    ata_init();
    solfs_init();
    vfs_init();
    rtc_load_settings();

    sys_init_ram(multiboot_info);
    sys_init_cpu();
    acpi_init();
    dbg_set_memory_metrics(sys_get_total_ram() * 1024, sys_get_total_ram() * 1024);

    struct embedded_spx spx_apps[] = {
        {"terminal.spx",
         (const uint8_t *)_binary_build_spx_terminal_spx_start,
         (uint32_t)(_binary_build_spx_terminal_spx_end - _binary_build_spx_terminal_spx_start)},
        {"notepad.spx",
         (const uint8_t *)_binary_build_spx_notepad_spx_start,
         (uint32_t)(_binary_build_spx_notepad_spx_end - _binary_build_spx_notepad_spx_start)},
        {"paint.spx",
         (const uint8_t *)_binary_build_spx_paint_spx_start,
         (uint32_t)(_binary_build_spx_paint_spx_end - _binary_build_spx_paint_spx_start)},
        {"settings.spx",
         (const uint8_t *)_binary_build_spx_settings_spx_start,
         (uint32_t)(_binary_build_spx_settings_spx_end - _binary_build_spx_settings_spx_start)},
        {"filebrowser.spx",
         (const uint8_t *)_binary_build_spx_filebrowser_spx_start,
         (uint32_t)(_binary_build_spx_filebrowser_spx_end - _binary_build_spx_filebrowser_spx_start)},
        {"taskmanager.spx",
         (const uint8_t *)_binary_build_spx_taskmanager_spx_start,
         (uint32_t)(_binary_build_spx_taskmanager_spx_end - _binary_build_spx_taskmanager_spx_start)},
        {"pkg.spx",
         (const uint8_t *)_binary_build_spx_pkg_spx_start,
         (uint32_t)(_binary_build_spx_pkg_spx_end - _binary_build_spx_pkg_spx_start)},
        {"editor.spx",
         (const uint8_t *)_binary_build_spx_editor_spx_start,
         (uint32_t)(_binary_build_spx_editor_spx_end - _binary_build_spx_editor_spx_start)},
        {"tetris.spx",
         (const uint8_t *)_binary_build_spx_tetris_spx_start,
         (uint32_t)(_binary_build_spx_tetris_spx_end - _binary_build_spx_tetris_spx_start)},
    };
    spx_init(spx_apps, 9);

    keyboard_init();
    pointer_init();

    net_init();

	__asm__ volatile("sti");

    console_clear();

    if (graphics_is_ready()) {
        for (;;) {
            console_write("[TRACE] login start\n");
            login_screen();
            console_write("[TRACE] login done\n");
            desktop_init();
            console_write("[TRACE] desktop init done\n");
            desktop_redraw();
            console_write("[TRACE] desktop redraw done\n");

            uint32_t last_redraw = 0;
            uint32_t last_clock_ticks = 0;

            for (;;) {
                struct pointer_state ps;
                if (pointer_poll(&ps)) {
                    gui_handle_mouse(ps.dx, ps.dy, ps.buttons);
                }
                gui_handle_scroll(mouse_take_wheel());
                if (keyboard_has_input()) {
                    char key = 0;
                    if (keyboard_read_char(&key)) {
                        gui_handle_key(key);
                    }
                }

                if (keyboard_was_start_menu_pressed()) {
                    gui_toggle_start_menu();
                }

                int fk = keyboard_consume_func_key();
                if (fk >= 1 && fk <= 9) {
                    console_write("[TRACE] FKEY ");
                    console_write_dec(fk);
                    console_write("\n");
                    gui_launch_app(fk - 1);
                }

                if (gui_needs_redraw()) {
                    /* Rate-limit, but never drop the frame: skipping the
                     * redraw and falling through to hlt leaves the UI stale
                     * until some unrelated interrupt arrives, which is what
                     * makes the whole shell feel laggy to drag a window. */
                    while ((uint32_t)(timer_get_ticks() - last_redraw) < 1) {
                        __asm__ volatile("hlt");
                    }
                    desktop_redraw();
                    last_redraw = timer_get_ticks();
                }

                uint32_t now_ticks = timer_get_ticks();
                if (now_ticks - last_clock_ticks >= 100) {
                    last_clock_ticks = now_ticks;
                    gui_update_clock();
                }

                if (gui_take_logout()) break;

                __asm__ volatile("hlt");
            }
        }
    } else {
        console_write("No framebuffer - system halted.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }
}
