#ifndef SOLIS_APPS_LUA_H
#define SOLIS_APPS_LUA_H

typedef void (*lua_putchar_fn)(char c);

/* Run a Lua 5.4-style source chunk.  The implementation is integer-only and
 * has a compact standard library; see apps/lua.c for the supported subset.
 * Returns 0 on success, -1 on a syntax or runtime error. */
int lua_run(const char *source, lua_putchar_fn putchar_fn);
const char *lua_last_error(void);

#endif
