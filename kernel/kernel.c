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
#include <solis/setup.h>
#include <solis/timer.h>
#include <solis/ata.h>
#include <solis/solfs.h>
#include <solis/vfs.h>
#include <solis/spx.h>
#include <solis/net.h>
#include <solis/dbg.h>
#include <solis/rtc.h>
#include <solis/acpi.h>
#include <solis/pmm.h>
#include <solis/vmm.h>
#include <solis/usermode.h>
#include <solis/usb.h>
#include <solis/boot.h>
#include <solis/doom.h>
#include <solis/audio.h>

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
extern char _binary_build_spx_blocks_spx_start[];
extern char _binary_build_spx_blocks_spx_end[];
extern char _binary_build_spx_calculator_spx_start[];
extern char _binary_build_spx_calculator_spx_end[];
extern char _binary_build_spx_doom_spx_start[];
extern char _binary_build_spx_doom_spx_end[];
extern char _binary_build_spx_browser_spx_start[];
extern char _binary_build_spx_ck3_spx_start[];
extern char _binary_build_spx_browser_spx_end[];
extern char _binary_build_spx_ck3_spx_end[];

void __attribute__((noreturn)) kernel_panic(const char *message) {
    console_set_color(0x0F, 0x04);
    console_write("SOLIS: kernel panic\n");
    console_write(message);
    console_write("\nSystem halted.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

void kernel_main(uint32_t multiboot_magic, uintptr_t multiboot_info) {
    console_init();
    if (multiboot_magic != 0x36D76289) {
        kernel_panic("Invalid Multiboot2 boot magic");
    }

    dbg_init();

    if (!pmm_init(multiboot_info))
        kernel_panic("Physical memory map initialization failed");
    if (!pmm_reserve(SOLIS_DOOM_HEAP_BASE, SOLIS_DOOM_HEAP_END))
        kernel_panic("Could not reserve the Doom working set");
    if (!pmm_reserve(0x08000000ULL, 0x09000000ULL))
        kernel_panic("Could not reserve the desktop cache region");
    if (!vmm_init())
        kernel_panic("Virtual memory initialization failed");
    console_write("[MEM] 4 KiB allocator and kernel page protections ready; free=");
    console_write_dec((uint32_t)(pmm_free_bytes() / (1024 * 1024)));
    console_write(" MiB\n");

    gdt_init();
    idt_init();
    pic_remap();
    graphics_init(multiboot_info);
    graphics_vbe_init();
    if (graphics_is_ready()) {
        graphics_clear(0xFF000000);
        boot_report_begin();
        boot_report_step("Physical memory and paging ready");
    }

    timer_init();
    rtc_init();
    boot_report_step("Timer and real-time clock online");

    ata_init();
    solfs_init();
    vfs_init();
    rtc_load_settings();
    setup_init();
    boot_report_step("ATA/AHCI storage and SolFS mounted");

    sys_init_ram(multiboot_info);
    sys_init_cpu();
    acpi_init();
    dbg_set_memory_metrics(sys_get_total_ram() * 1024, sys_get_total_ram() * 1024);
    boot_report_step("ACPI and system services ready");

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
           {"blocks.spx",
            (const uint8_t *)_binary_build_spx_blocks_spx_start,
            (uint32_t)(_binary_build_spx_blocks_spx_end - _binary_build_spx_blocks_spx_start)},
           {"calculator.spx",
            (const uint8_t *)_binary_build_spx_calculator_spx_start,
            (uint32_t)(_binary_build_spx_calculator_spx_end - _binary_build_spx_calculator_spx_start)},
           {"doom.spx",
            (const uint8_t *)_binary_build_spx_doom_spx_start,
            (uint32_t)(_binary_build_spx_doom_spx_end - _binary_build_spx_doom_spx_start)},
           {"browser.spx",
            (const uint8_t *)_binary_build_spx_browser_spx_start,
            (uint32_t)(_binary_build_spx_browser_spx_end - _binary_build_spx_browser_spx_start)},
           {"ck3.spx",
            (const uint8_t *)_binary_build_spx_ck3_spx_start,
            (uint32_t)(_binary_build_spx_ck3_spx_end - _binary_build_spx_ck3_spx_start)},
    };
        spx_init(spx_apps, (int)(sizeof(spx_apps) / sizeof(spx_apps[0])));
    if (setup_is_installed()) setup_apply_edition();
    boot_report_step("SPX application runtime loaded");

    keyboard_init();
    pointer_init();
    boot_report_step("Keyboard and pointer ready");

    if (audio_init()) boot_report_step("AC'97 audio initialized");

    int network_device_ready = net_init();
    boot_report_step("Network device initialized");

	__asm__ volatile("sti");
    usb_init();
    boot_report_step("USB input initialized");

    if (network_device_ready == 0 && net_configure_dhcp() != 0)
        dbg_print("[NET] DHCP configuration failed; network unavailable\n");
    boot_report_step("Network stack configured");

    usermode_run_self_test();
    console_clear();
    boot_report_finish();

    if (graphics_is_ready()) {
        if (!setup_is_installed()) {
            console_write("[SETUP] no account found; starting installer\n");
            setup_wizard_run();
        }

        for (;;) {
            console_write("[TRACE] login start\n");
            bool guest = login_screen();
            gui_set_guest_session(guest);
            console_write("[TRACE] login done\n");
            desktop_init();
            gui_set_theme(setup_theme());
            console_write("[TRACE] desktop init done\n");
            desktop_redraw();
            console_write("[TRACE] desktop redraw done\n");

            uint32_t last_redraw = 0;
            uint32_t last_clock_ticks = 0;

            for (;;) {
                usb_poll();
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
                if (fk >= 1 && fk <= 10) {
                    console_write("[TRACE] FKEY ");
                    console_write_dec(fk);
                    console_write("\n");
                    gui_launch_app(fk - 1);
                } else if (fk == 11) {
                    /* Browser is slot 12, past the F1-F10 app range. */
                    console_write("[TRACE] FKEY 11 Browser\n");
                    gui_launch_app(SPX_BROWSER);
                } else if (fk == 12) {
                    console_write("[TRACE] FKEY 12 Settings\n");
                    gui_launch_app(SPX_SETTINGS);
                }

                gui_tick();

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
