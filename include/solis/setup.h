#ifndef SOLIS_SETUP_H
#define SOLIS_SETUP_H

#include <stdbool.h>

#define SETUP_USERNAME_MAX 24
#define SETUP_PASSWORD_MAX 64

#define SETUP_DISK_NONE   0
#define SETUP_DISK_READY  1
#define SETUP_DISK_BLANK  2

#define SETUP_INSTALL_OK         0
#define SETUP_INSTALL_WRITE_ERROR -1
#define SETUP_INSTALL_NO_DISK    -2

/* Installation editions. Extra is the release default because it is the only
 * edition that ships the full workshop (including Doom). */
#define SETUP_EDITION_MINIMAL  0
#define SETUP_EDITION_STANDARD 1
#define SETUP_EDITION_EXTRA    2
#define SETUP_EDITION_COUNT    3
#define SETUP_EDITION_DEFAULT  SETUP_EDITION_EXTRA

/* Load the saved account record from SolFS. When no valid record exists the
 * system is considered uninstalled and the first-boot wizard is shown. */
void setup_init(void);

bool setup_is_installed(void);
const char *setup_username(void);
int setup_theme(void);
int setup_timezone(void);
int setup_edition(void);
bool setup_is_guest(void);

/* Edition changes are staged so the running desktop is never unloaded from
 * under an open window. The requested edition is written to a small pending
 * record and applied by setup_apply_edition() on the next boot. */
int setup_pending_edition(void);
int setup_request_edition(int edition);

/* Returns true when password matches the saved salted hash. */
bool setup_authenticate(const char *password);

/* Creates the local account and saves the selected theme and timezone.
 * Returns 0 on success, -1 when the record cannot be written. */
int setup_install(const char *username, const char *password,
                  int theme, int timezone, int edition);

/* Create a passwordless local guest account. The account still records the
 * chosen edition so the same app pruning and Doom availability rules apply. */
int setup_install_guest(int theme, int timezone, int edition);

/* Apply a staged edition (if any), remove apps outside the selected edition,
 * and clear temporary installer state. Safe to call after setup_init() on
 * every boot. */
void setup_apply_edition(void);

/* 0 = no compatible disk, 1 = mounted Solis volume, 2 = blank disk that
 * setup_install() can format. */
int setup_disk_status(void);

/* Replace the saved account password. Keeps the same account, theme and
 * timezone. Returns 0 on success, -1 when the new password is invalid or
 * the account record cannot be written. */
int setup_change_password(const char *password);

/* Persist a theme selection without changing the account credentials. */
int setup_set_theme(int theme);

/* Full-screen first-boot setup wizard (gui/setup_wizard.c). */
void setup_wizard_run(void);

#endif
