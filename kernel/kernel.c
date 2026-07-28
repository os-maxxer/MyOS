#include <nyx/console.h>
#include <nyx/gdt.h>
#include <nyx/idt.h>
#include <nyx/pic.h>
#include <nyx/keyboard.h>
#include <nyx/mouse.h>
#include <nyx/graphics.h>
#include <nyx/window_manager.h>
#include <nyx/desktop.h>
#include <nyx/gui.h>
#include <nyx/login.h>
#include <nyx/timer.h>
#include <nyx/ata.h>
#include <nyx/nofs.h>
#include <nyx/vfs.h>
#include <nyx/npx.h>
#include <nyx/net.h>
#include <nyx/dbg.h>
#include <nyx/i2c.h>
#include <nyx/touchpad.h>

extern char _binary_build_npx_terminal_npx_start[];
extern char _binary_build_npx_terminal_npx_end[];
extern char _binary_build_npx_notepad_npx_start[];
extern char _binary_build_npx_notepad_npx_end[];
extern char _binary_build_npx_paint_npx_start[];
extern char _binary_build_npx_paint_npx_end[];
extern char _binary_build_npx_settings_npx_start[];
extern char _binary_build_npx_settings_npx_end[];
extern char _binary_build_npx_filebrowser_npx_start[];
extern char _binary_build_npx_filebrowser_npx_end[];
extern char _binary_build_npx_taskmanager_npx_start[];
extern char _binary_build_npx_taskmanager_npx_end[];
extern char _binary_build_npx_pkg_npx_start[];
extern char _binary_build_npx_pkg_npx_end[];

void __attribute__((noreturn)) kernel_panic(const char *message) {
    console_clear();
    console_set_color(0x0F, 0x04);
    console_write("NYX: kernel panic\n");
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
    console_set_color(0x0A, 0x00);
    console_write("Nyx OS booting...\n");

    gdt_init();
    idt_init();

    pic_remap();

    graphics_init(multiboot_info);
    if (graphics_is_ready()) {
        console_write("Framebuffer ready.\n");
        graphics_clear(0xFF000000);
    } else {
        console_write("Framebuffer unavailable.\n");
    }

    timer_init();

    ata_init();
    if (ata_present()) {
        console_write("Disk detected.\n");
    }

    nofs_init();
    vfs_init();

    sys_init_ram(multiboot_info);
    sys_init_cpu();
    console_write("Detected ");
    console_write_dec(sys_get_total_ram());
    console_write(" MB RAM\n");

    struct embedded_npx npx_apps[] = {
        {"terminal.npx",
         (const uint8_t *)_binary_build_npx_terminal_npx_start,
         (uint32_t)(_binary_build_npx_terminal_npx_end - _binary_build_npx_terminal_npx_start)},
        {"notepad.npx",
         (const uint8_t *)_binary_build_npx_notepad_npx_start,
         (uint32_t)(_binary_build_npx_notepad_npx_end - _binary_build_npx_notepad_npx_start)},
        {"paint.npx",
         (const uint8_t *)_binary_build_npx_paint_npx_start,
         (uint32_t)(_binary_build_npx_paint_npx_end - _binary_build_npx_paint_npx_start)},
        {"settings.npx",
         (const uint8_t *)_binary_build_npx_settings_npx_start,
         (uint32_t)(_binary_build_npx_settings_npx_end - _binary_build_npx_settings_npx_start)},
        {"filebrowser.npx",
         (const uint8_t *)_binary_build_npx_filebrowser_npx_start,
         (uint32_t)(_binary_build_npx_filebrowser_npx_end - _binary_build_npx_filebrowser_npx_start)},
        {"taskmanager.npx",
         (const uint8_t *)_binary_build_npx_taskmanager_npx_start,
         (uint32_t)(_binary_build_npx_taskmanager_npx_end - _binary_build_npx_taskmanager_npx_start)},
        {"pkg.npx",
         (const uint8_t *)_binary_build_npx_pkg_npx_start,
         (uint32_t)(_binary_build_npx_pkg_npx_end - _binary_build_npx_pkg_npx_start)},
    };
    npx_init(npx_apps, 7);

    keyboard_init();
    mouse_init();
    i2c_init();
    touchpad_init();
    console_write("Input devices ready.\n");

    net_init();

	__asm__ volatile("sti");

	dbg_print("[BOOT] Network tests...\n");

	/* 1. Local regression: TCP SYN to host Python server */
	uint8_t host_ip[4] = {10, 0, 2, 2};
	dbg_print("[BOOT] Local: SYN to 10.0.2.2:8000...\n");
	int syn_result = net_tcp_syn_test(host_ip, 8000, 3000);
	if (syn_result == 0)
		console_write("[BOOT] Local TCP SYN OK!\n");
	else
		console_write("[BOOT] Local TCP SYN FAILED\n");

	/* 2. DNS + Internet: resolve hostname, then TCP SYN */
	uint8_t www_ip[4];
	dbg_print("[BOOT] DNS: resolving httpbin.org...\n");
	int dns_result = net_dns_resolve("httpbin.org", www_ip);
	if (dns_result == 0) {
		console_write("[BOOT] DNS OK! httpbin.org = ");
		console_write_dec(www_ip[0]); console_write(".");
		console_write_dec(www_ip[1]); console_write(".");
		console_write_dec(www_ip[2]); console_write(".");
		console_write_dec(www_ip[3]); console_write("\n");

		dbg_print("[BOOT] Internet: SYN to httpbin.org:80...\n");
		int www_syn = net_tcp_syn_test(www_ip, 80, 15000);
		if (www_syn == 0)
			console_write("[BOOT] Internet TCP SYN OK! - SLiRP NAT + DNS work\n");
		else
			console_write("[BOOT] Internet TCP SYN FAILED\n");
	} else {
		console_write("[BOOT] DNS FAILED - cannot resolve httpbin.org\n");
	}

	/* 3. HTTP GET /repo.json from local server */
	{
		dbg_print("[BOOT] HTTP GET /repo.json from local server...\n");
		uint8_t http_buf[4096];
		int http_len = net_http_get(host_ip, 8000, "10.0.2.2", "/repo.json", http_buf, 4096);
		if (http_len > 0) {
			console_write("[BOOT] HTTP GET /repo.json OK! ");
			console_write_dec(http_len);
			console_write(" bytes\n");
			if (http_len > 0 && http_buf[0] == '{')
				console_write("[BOOT] Body starts with '{' - valid JSON\n");
			else
				console_write("[BOOT] Warning: body does not start with '{'\n");
		} else {
			console_write("[BOOT] HTTP GET /repo.json FAILED\n");
		}
	}

	/* 4. HTTP GET /large (20KB) */
	{
		dbg_print("[BOOT] HTTP GET /large from local server...\n");
		net_flush_rx();
		uint8_t large_buf[8192];
		int large_len = net_http_get(host_ip, 8000, "10.0.2.2", "/large", large_buf, 8192);
		if (large_len > 0) {
			console_write("[BOOT] HTTP GET /large OK! ");
			console_write_dec(large_len);
			console_write(" bytes\n");
		} else {
			console_write("[BOOT] HTTP GET /large FAILED\n");
		}
	}

	/* 5. HTTP redirect test */
	{
		dbg_print("[BOOT] HTTP redirect test...\n");
		net_flush_rx();
		uint8_t redir_buf[4096];
		int redir_len = net_http_get(host_ip, 8000, "10.0.2.2", "/redirect", redir_buf, 4096);
		if (redir_len > 0) {
			console_write("[BOOT] HTTP redirect OK! ");
			console_write_dec(redir_len);
			console_write(" bytes\n");
			if (redir_len > 0 && redir_buf[0] == '{')
				console_write("[BOOT] Redirect body starts with '{' - valid JSON\n");
		} else {
			console_write("[BOOT] HTTP redirect FAILED\n");
		}
	}

	/* 6. HTTP chunked encoding test */
	{
		dbg_print("[BOOT] HTTP chunked test...\n");
		net_flush_rx();
		uint8_t chunk_buf[4096];
		int chunk_len = net_http_get(host_ip, 8000, "10.0.2.2", "/chunked", chunk_buf, 4096);
		if (chunk_len > 0) {
			console_write("[BOOT] HTTP chunked OK! ");
			console_write_dec(chunk_len);
			console_write(" bytes\n");
		} else {
			console_write("[BOOT] HTTP chunked FAILED\n");
		}
	}

	console_write("Boot complete. Starting GUI...\n");
    console_clear();

    if (graphics_is_ready()) {
        login_screen();
        desktop_init();
        desktop_redraw();
    } else {
        console_write("No framebuffer - system halted.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    uint32_t last_redraw = 0;

    for (;;) {
        bool mouse_moved = false;

        struct mouse_state ms;
        mouse_get_state(&ms);
        if (ms.dx || ms.dy || ms.buttons) {
            gui_handle_mouse(ms.dx, ms.dy, ms.buttons);
            if (ms.dx || ms.dy) mouse_moved = true;
        }

        struct touchpad_state ts;
        if (touchpad_poll(&ts) && (ts.dx || ts.dy || ts.buttons)) {
            gui_handle_mouse(ts.dx, ts.dy, ts.buttons);
            if (ts.dx || ts.dy) mouse_moved = true;
        }
        if (keyboard_has_input()) {
            char key = 0;
            if (keyboard_read_char(&key)) {
                gui_handle_key(key);
            }
        }

        if (keyboard_was_start_menu_pressed()) {
            gui_toggle_start_menu();
        }

        if (gui_needs_redraw()) {
            if (mouse_moved) {
                desktop_redraw();
                last_redraw = timer_get_ticks();
            } else {
                uint32_t now = timer_get_ticks();
                if (now - last_redraw >= 1) {
                    desktop_redraw();
                    last_redraw = now;
                }
            }
        }

        __asm__ volatile("hlt");
    }
}
