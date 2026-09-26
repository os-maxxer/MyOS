#ifndef SOLIS_APPS_CINTERP_H
#define SOLIS_APPS_CINTERP_H

/* A small tree-walking C interpreter, embedded in the Terminal app.
 *
 * Supported subset:
 *   types      void, char, int, pointers, arrays, structs, typedef, const,
 *              unsigned/signed (sign is parsed but arithmetic stays signed)
 *   statements compound, if/else, while, do/while, for, return, break,
 *              continue, expression, declaration, empty
 *   operators  the usual arithmetic/bitwise/logical set, compound
 *              assignment, ++/-- (prefix and postfix), ?:, sizeof, casts
 *   builtins   printf, puts, putchar, strlen, strcmp, strcpy, memset, abs,
 *              exit
 *
 * Not supported: floats, enums, unions, function pointers, the preprocessor
 * (lines starting with '#' are skipped), variable initialisers that are not
 * constant, and the heap.
 */

typedef void (*ci_putchar_fn)(char c);

void ci_set_putchar(ci_putchar_fn fn);

/* Run a program. Returns the exit code from main(), or -1 if the program
 * could not be parsed or aborted. Call ci_last_error() for the message. */
int ci_run(const char *source);

/* Parse and validate a source file without executing it. */
int ci_check(const char *source);

const char *ci_last_error(void);

#endif
