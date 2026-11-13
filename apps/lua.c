#include <solis/apps/lua.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Solis Lua
 * ---------
 * A small, self-contained Lua 5.4-style interpreter.  It implements the
 * language core needed by real scripts: lexical scopes, functions and
 * closures, tables, numeric/generic loops, if/elseif/else, repeat, string
 * concatenation and comparison, and the print/type/tostring/tonumber,
 * math, string and table helpers.
 *
 * Solis currently builds without an FPU, so this implementation uses 64-bit
 * integers.  `/` is integer division.  There are no coroutines, metatables,
 * varargs, goto, or long-string literals.
 */

#define LUA_MAX_NODES   8192
#define LUA_MAX_NAMES   8192
#define LUA_MAX_STRINGS 32768
#define LUA_MAX_SYMS    128
#define LUA_MAX_TABLES  128
#define LUA_MAX_ENVS    192
#define LUA_MAX_FUNCS   128
#define LUA_MAX_ARGS    8
#define LUA_MAX_TABLE   64
#define LUA_MAX_SCOPE   24
#define LUA_MAX_STEPS   4000000

enum { LT_EOF = 0, LT_NAME, LT_NUM, LT_STR, LT_SYM, LT_KW };

enum {
    LK_NONE = 0, LK_AND, LK_BREAK, LK_DO, LK_ELSE, LK_ELSEIF, LK_END,
    LK_FALSE, LK_FOR, LK_FUNCTION, LK_IF, LK_IN, LK_LOCAL, LK_NIL, LK_NOT,
    LK_OR, LK_REPEAT, LK_RETURN, LK_THEN, LK_TRUE, LK_UNTIL, LK_WHILE
};

enum {
    LS_NONE = 0, LS_ADD, LS_SUB, LS_MUL, LS_DIV, LS_MOD, LS_POW,
    LS_CONCAT, LS_EQ, LS_NE, LS_LT, LS_LE, LS_GT, LS_GE, LS_AND, LS_OR,
    LS_NOT, LS_LEN, LS_NEG, LS_LPAREN, LS_RPAREN, LS_LBRACE, LS_RBRACE,
    LS_LBRACK, LS_RBRACK, LS_COMMA, LS_SEMI, LS_DOT, LS_ASSIGN, LS_CONCAT2,
    LS_VARARG
};

enum {
    N_NIL = 0, N_TRUE, N_FALSE, N_NUM, N_STR, N_VAR, N_INDEX, N_CALL,
    N_BIN, N_UN, N_TABLE, N_FUNC,
    N_LOCAL, N_ASSIGN, N_CALLSTMT, N_BLOCK, N_IF, N_WHILE, N_REPEAT,
    N_FOR, N_FORIN, N_RETURN, N_BREAK, N_DO, N_FUNCDEF, N_LOCALFUNC,
    N_FIELD
};

enum { V_NIL = 0, V_BOOL, V_INT, V_STR, V_TABLE, V_FUNC, V_BUILTIN };

enum {
    B_PRINT = 1, B_TYPE, B_TOSTRING, B_TONUMBER, B_PAIRS, B_IPAIRS,
    B_NEXT, B_ASSERT, B_ERROR, B_SELECT,
    B_MATH_ABS, B_MATH_FLOOR, B_MATH_MAX, B_MATH_MIN,
    B_STR_LEN, B_STR_SUB, B_STR_UPPER, B_STR_LOWER, B_STR_REP,
    B_TABLE_INSERT, B_TABLE_CONCAT, B_TABLE_REMOVE, B_IO_WRITE
};

struct lua_value;
struct lua_func;
struct lua_env;
struct lua_tree;

struct lua_value {
    uint8_t type;
    uint8_t boolean;
    int32_t builtin;
    int64_t integer;
    const char *string;
    struct lua_table *table;
    struct lua_func *func;
};

struct lua_entry {
    struct lua_value key;
    struct lua_value value;
};

struct lua_table {
    int count;
    struct lua_entry entries[LUA_MAX_TABLE];
};

struct lua_func {
    int nparams;
    const char *params[LUA_MAX_ARGS];
    struct lua_tree *body;
    struct lua_env *closure;
};

struct lua_var {
    const char *name;
    struct lua_value value;
};

struct lua_env {
    struct lua_env *parent;
    int count;
    struct lua_var vars[LUA_MAX_SCOPE];
};

struct lua_tree {
    uint8_t kind;
    uint8_t op;
    int32_t line;
    int64_t number;
    const char *text;
    struct lua_tree *a, *b, *c, *d;
    struct lua_tree *next;
    int32_t argc;
    struct lua_func *func;
};

struct lua_token {
    uint8_t kind;
    uint8_t op;
    int32_t line;
    int64_t number;
    const char *text;
};

struct lua_lexer {
    const char *p;
    int32_t line;
};

struct lua_parser {
    struct lua_lexer lx;
    struct lua_token tok;
    int32_t error;
};

static struct lua_tree lua_nodes[LUA_MAX_NODES];
static int32_t lua_node_used;
static char lua_names[LUA_MAX_NAMES];
static int32_t lua_name_used;
static char lua_strings[LUA_MAX_STRINGS];
static int32_t lua_string_used;
static struct lua_table lua_tables[LUA_MAX_TABLES];
static int32_t lua_table_used;
static struct lua_env lua_envs[LUA_MAX_ENVS];
static int32_t lua_env_used;
static struct lua_func lua_funcs[LUA_MAX_FUNCS];
static int32_t lua_func_used;
static char lua_error[192];
static int32_t lua_error_line;
static lua_putchar_fn lua_output;
static struct lua_env *lua_globals;
static uint32_t lua_steps;
static int32_t lua_call_depth;
static int32_t lua_current_line;
static const struct lua_value lua_nil_value = {V_NIL, 0, 0, 0, 0, 0, 0};

static int lua_int_to_string_buffer(char *buffer, int64_t value) {
    char digits[24];
    int n = 0;
    uint64_t magnitude;
    int negative = value < 0;
    if (negative) magnitude = (uint64_t)(-value); else magnitude = (uint64_t)value;
    if (magnitude == 0) digits[n++] = '0';
    while (magnitude > 0 && n < 23) {
        digits[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    }
    int pos = 0;
    if (negative) buffer[pos++] = '-';
    while (n > 0) buffer[pos++] = digits[--n];
    buffer[pos] = '\0';
    return pos;
}

static const char *lua_store_string(const char *text, int32_t length) {
    if (length < 0 || (uint32_t)(lua_string_used + length + 1) > LUA_MAX_STRINGS) {
        if (!lua_error[0]) {
            int i = 0;
            const char *message = "out of string space";
            while (message[i] && i < 150) { lua_error[i] = message[i]; i++; }
            lua_error[i] = '\0';
        }
        return "";
    }
    char *result = lua_strings + lua_string_used;
    for (int32_t i = 0; i < length; i++) result[i] = text[i];
    result[length] = '\0';
    lua_string_used += length + 1;
    return result;
}

static const char *lua_intern(const char *text, int32_t length) {
    if (length < 0 || (uint32_t)(lua_name_used + length + 1) > LUA_MAX_NAMES) {
        if (!lua_error[0]) {
            int i = 0;
            const char *message = "out of identifier space";
            while (message[i] && i < 150) { lua_error[i] = message[i]; i++; }
            lua_error[i] = '\0';
        }
        return "";
    }
    char *result = lua_names + lua_name_used;
    for (int32_t i = 0; i < length; i++) result[i] = text[i];
    result[length] = '\0';
    lua_name_used += length + 1;
    return result;
}

static int lua_strlen(const char *text) {
    int n = 0;
    while (text[n]) n++;
    return n;
}

static int lua_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int lua_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int lua_is_digit(char c) { return c >= '0' && c <= '9'; }
static int lua_is_ident(char c) { return lua_is_ident_start(c) || lua_is_digit(c); }

static int lua_keyword(const char *text, int32_t length) {
    static const struct { const char *name; int32_t id; } kw[] = {
        {"and", LK_AND}, {"break", LK_BREAK}, {"do", LK_DO},
        {"else", LK_ELSE}, {"elseif", LK_ELSEIF}, {"end", LK_END},
        {"false", LK_FALSE}, {"for", LK_FOR}, {"function", LK_FUNCTION},
        {"if", LK_IF}, {"in", LK_IN}, {"local", LK_LOCAL},
        {"nil", LK_NIL}, {"not", LK_NOT}, {"or", LK_OR},
        {"repeat", LK_REPEAT}, {"return", LK_RETURN}, {"then", LK_THEN},
        {"true", LK_TRUE}, {"until", LK_UNTIL}, {"while", LK_WHILE}
    };
    for (int i = 0; i < (int)(sizeof(kw) / sizeof(kw[0])); i++) {
        int32_t n = lua_strlen(kw[i].name);
        if (n != length) continue;
        int match = 1;
        for (int32_t j = 0; j < length; j++)
            if (text[j] != kw[i].name[j]) { match = 0; break; }
        if (match) return kw[i].id;
    }
    return LK_NONE;
}

static void lua_parse_error(int32_t line, const char *message) {
    if (lua_error[0]) return;
    lua_error_line = line;
    int n = 0;
    while (message[n] && n < 150) { lua_error[n] = message[n]; n++; }
    lua_error[n++] = ' ';
    lua_error[n++] = '(';
    lua_error[n++] = 'l';
    lua_error[n++] = 'i';
    lua_error[n++] = 'n';
    lua_error[n++] = 'e';
    lua_error[n++] = ' ';
    char digits[12];
    int dn = 0;
    int32_t v = line;
    if (v == 0) digits[dn++] = '0';
    while (v > 0 && dn < 11) { digits[dn++] = (char)('0' + v % 10); v /= 10; }
    while (dn > 0) lua_error[n++] = digits[--dn];
    lua_error[n++] = ')';
    lua_error[n] = '\0';
}

const char *lua_last_error(void) {
    return lua_error[0] ? lua_error : "unknown Lua error";
}

static void lua_out_char(char c) {
    if (lua_output) lua_output(c);
}
static void lua_out_text(const char *text) {
    if (!text) return;
    while (*text) lua_out_char(*text++);
}

static int lua_read_escape(const char **pp) {
    const char *p = *pp;
    char c = *p++;
    int value;
    switch (c) {
        case 'a': value = 7; break;
        case 'b': value = 8; break;
        case 'f': value = 12; break;
        case 'n': value = '\n'; break;
        case 'r': value = '\r'; break;
        case 't': value = '\t'; break;
        case 'v': value = 11; break;
        case '\\': value = '\\'; break;
        case '\'': value = '\''; break;
        case '"': value = '"'; break;
        default: value = (unsigned char)c; break;
    }
    *pp = p;
    return value;
}

static void lua_skip_space(struct lua_lexer *lx) {
    for (;;) {
        char c = *lx->p;
        if (c == '\n') { lx->line++; lx->p++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { lx->p++; continue; }
        if (c == '-' && lx->p[1] == '-') {
            lx->p += 2;
            if (lx->p[0] == '[' && lx->p[1] == '[') {
                lx->p += 2;
                while (*lx->p && !(lx->p[0] == ']' && lx->p[1] == ']')) {
                    if (*lx->p == '\n') lx->line++;
                    lx->p++;
                }
                if (*lx->p) lx->p += 2;
            } else {
                while (*lx->p && *lx->p != '\n') lx->p++;
            }
            continue;
        }
        return;
    }
}

static int lua_next_token(struct lua_lexer *lx, struct lua_token *t) {
    lua_skip_space(lx);
    t->line = lx->line;
    t->number = 0;
    t->text = 0;
    t->op = LS_NONE;
    char c = *lx->p;
    if (!c) { t->kind = LT_EOF; return 1; }

    if (lua_is_ident_start(c)) {
        const char *start = lx->p;
        while (lua_is_ident(*lx->p)) lx->p++;
        int32_t length = (int32_t)(lx->p - start);
        int kw = lua_keyword(start, length);
        t->kind = kw ? LT_KW : LT_NAME;
        t->op = (uint8_t)kw;
        t->text = kw ? 0 : lua_intern(start, length);
        return 1;
    }

    if (lua_is_digit(c)) {
        int64_t value = 0;
        while (lua_is_digit(*lx->p)) {
            value = value * 10 + (*lx->p - '0');
            lx->p++;
        }
        if (*lx->p == '.' && lua_is_digit(lx->p[1])) {
            lua_parse_error(lx->line, "floating-point numbers are not supported");
            return 0;
        }
        t->kind = LT_NUM;
        t->number = value;
        return 1;
    }

    if (c == '"' || c == '\'') {
        char quote = c;
        lx->p++;
        char buffer[512];
        int32_t used = 0;
        while (*lx->p && *lx->p != quote && used < 510) {
            int value;
            if (*lx->p == '\\') { lx->p++; value = lua_read_escape(&lx->p); }
            else value = (unsigned char)*lx->p++;
            if (value == '\n') lx->line++;
            buffer[used++] = (char)value;
        }
        if (*lx->p == quote) lx->p++;
        buffer[used] = '\0';
        t->kind = LT_STR;
        t->text = lua_store_string(buffer, used);
        t->number = used;
        return 1;
    }

    if (c == '.' && lx->p[1] == '.' && lx->p[2] == '.') {
        t->kind = LT_SYM;
        t->op = LS_VARARG;
        lx->p += 3;
        return 1;
    }
    if (c == '.' && lx->p[1] == '.') {
        t->kind = LT_SYM;
        t->op = LS_CONCAT2;
        lx->p += 2;
        return 1;
    }

    static const struct { const char *text; uint8_t op; } puncts[] = {
        {"==", LS_EQ}, {"~=", LS_NE}, {"<=", LS_LE}, {">=", LS_GE},
        {"+", LS_ADD}, {"-", LS_SUB}, {"*", LS_MUL}, {"/", LS_DIV},
        {"%", LS_MOD}, {"^", LS_POW}, {"#", LS_LEN}, {"<", LS_LT},
        {">", LS_GT}, {"=", LS_ASSIGN}, {"(", LS_LPAREN},
        {")", LS_RPAREN}, {"{", LS_LBRACE}, {"}", LS_RBRACE},
        {"[", LS_LBRACK}, {"]", LS_RBRACK}, {",", LS_COMMA},
        {";", LS_SEMI}, {".", LS_DOT}
    };
    for (int i = 0; i < (int)(sizeof(puncts) / sizeof(puncts[0])); i++) {
        int32_t n = lua_strlen(puncts[i].text);
        int match = 1;
        for (int32_t j = 0; j < n; j++)
            if (lx->p[j] != puncts[i].text[j]) { match = 0; break; }
        if (match) {
            t->kind = LT_SYM;
            t->op = puncts[i].op;
            lx->p += n;
            return 1;
        }
    }
    lua_parse_error(lx->line, "unexpected character");
    return 0;
}

static int lua_advance(struct lua_parser *p) {
    return lua_next_token(&p->lx, &p->tok);
}
static int lua_is_sym(struct lua_parser *p, int op) {
    return p->tok.kind == LT_SYM && p->tok.op == op;
}
static int lua_is_kw(struct lua_parser *p, int kw) {
    return p->tok.kind == LT_KW && p->tok.op == kw;
}
static int lua_eat_sym(struct lua_parser *p, int op) {
    if (!lua_is_sym(p, op)) return 0;
    return lua_advance(p);
}
static int lua_expect_sym(struct lua_parser *p, int op) {
    if (lua_eat_sym(p, op)) return 1;
    lua_parse_error(p->tok.line, "expected punctuation");
    return 0;
}
static int lua_expect_kw(struct lua_parser *p, int kw) {
    if (!lua_is_kw(p, kw)) {
        lua_parse_error(p->tok.line, "expected keyword");
        return 0;
    }
    return lua_advance(p);
}
static const char *lua_expect_name(struct lua_parser *p) {
    if (p->tok.kind != LT_NAME) {
        lua_parse_error(p->tok.line, "expected a name");
        return 0;
    }
    const char *name = p->tok.text;
    if (!lua_advance(p)) return 0;
    return name;
}

static struct lua_tree *lua_new_node(int kind, int32_t line) {
    if (lua_node_used >= LUA_MAX_NODES) {
        lua_parse_error(line, "script has too many syntax nodes");
        return 0;
    }
    struct lua_tree *n = &lua_nodes[lua_node_used++];
    n->kind = (uint8_t)kind;
    n->op = 0;
    n->line = line;
    n->number = 0;
    n->text = 0;
    n->a = n->b = n->c = n->d = 0;
    n->next = 0;
    n->argc = 0;
    n->func = 0;
    return n;
}

static struct lua_func *lua_new_func(void) {
    if (lua_func_used >= LUA_MAX_FUNCS) {
        lua_parse_error(0, "too many functions");
        return 0;
    }
    struct lua_func *f = &lua_funcs[lua_func_used++];
    f->nparams = 0;
    f->body = 0;
    f->closure = 0;
    return f;
}

static struct lua_tree *lua_parse_expr(struct lua_parser *p);
static struct lua_tree *lua_parse_stmt(struct lua_parser *p);
static struct lua_tree *lua_parse_block(struct lua_parser *p);

static struct lua_tree *lua_parse_funcbody(struct lua_parser *p) {
    int32_t line = p->tok.line;
    struct lua_func *f = lua_new_func();
    if (!f) return 0;
    if (!lua_expect_sym(p, LS_LPAREN)) return 0;
    if (!lua_is_sym(p, LS_RPAREN)) {
        for (;;) {
            const char *name = lua_expect_name(p);
            if (!name) return 0;
            if (f->nparams >= LUA_MAX_ARGS) {
                lua_parse_error(p->tok.line, "too many parameters");
                return 0;
            }
            f->params[f->nparams++] = name;
            if (!lua_eat_sym(p, LS_COMMA)) break;
        }
    }
    if (!lua_expect_sym(p, LS_RPAREN)) return 0;
    f->body = lua_parse_block(p);
    if (!f->body || !lua_expect_kw(p, LK_END)) return 0;
    struct lua_tree *n = lua_new_node(N_FUNC, line);
    if (!n) return 0;
    n->func = f;
    return n;
}

static struct lua_tree *lua_parse_table(struct lua_parser *p) {
    int32_t line = p->tok.line;
    if (!lua_advance(p)) return 0;
    struct lua_tree *first = 0;
    struct lua_tree *last = 0;
    while (!lua_is_sym(p, LS_RBRACE)) {
        if (p->tok.kind == LT_EOF) {
            lua_parse_error(p->tok.line, "unterminated table");
            return 0;
        }
        struct lua_tree *field = lua_new_node(N_FIELD, p->tok.line);
        if (!field) return 0;
        if (lua_eat_sym(p, LS_LBRACK)) {
            field->op = 1;
            field->a = lua_parse_expr(p);
            if (!field->a || !lua_expect_sym(p, LS_RBRACK) ||
                !lua_expect_sym(p, LS_ASSIGN)) return 0;
            field->b = lua_parse_expr(p);
            if (!field->b) return 0;
        } else {
            field->b = lua_parse_expr(p);
            if (!field->b) return 0;
            if (lua_eat_sym(p, LS_ASSIGN)) {
                if (field->b->kind != N_VAR) {
                    lua_parse_error(field->b->line, "table name field expected");
                    return 0;
                }
                field->op = 2;
                field->text = field->b->text;
                field->b = lua_parse_expr(p);
                if (!field->b) return 0;
            }
        }
        if (!first) first = field; else last->next = field;
        last = field;
        if (!lua_eat_sym(p, LS_COMMA) && !lua_eat_sym(p, LS_SEMI)) break;
    }
    if (!lua_expect_sym(p, LS_RBRACE)) return 0;
    struct lua_tree *n = lua_new_node(N_TABLE, line);
    if (!n) return 0;
    n->a = first;
    return n;
}

static struct lua_tree *lua_parse_primary(struct lua_parser *p) {
    int32_t line = p->tok.line;
    if (p->tok.kind == LT_NUM) {
        int64_t value = p->tok.number;
        if (!lua_advance(p)) return 0;
        struct lua_tree *n = lua_new_node(N_NUM, line);
        if (!n) return 0;
        n->number = value;
        return n;
    }
    if (p->tok.kind == LT_STR) {
        const char *text = p->tok.text;
        if (!lua_advance(p)) return 0;
        struct lua_tree *n = lua_new_node(N_STR, line);
        if (!n) return 0;
        n->text = text;
        return n;
    }
    if (lua_is_kw(p, LK_NIL)) {
        if (!lua_advance(p)) return 0;
        return lua_new_node(N_NIL, line);
    }
    if (lua_is_kw(p, LK_TRUE)) {
        if (!lua_advance(p)) return 0;
        return lua_new_node(N_TRUE, line);
    }
    if (lua_is_kw(p, LK_FALSE)) {
        if (!lua_advance(p)) return 0;
        return lua_new_node(N_FALSE, line);
    }
    if (lua_is_kw(p, LK_FUNCTION)) {
        if (!lua_advance(p)) return 0;
        return lua_parse_funcbody(p);
    }
    if (lua_is_sym(p, LS_LBRACE)) return lua_parse_table(p);
    if (lua_is_sym(p, LS_LPAREN)) {
        if (!lua_advance(p)) return 0;
        struct lua_tree *e = lua_parse_expr(p);
        if (!e || !lua_expect_sym(p, LS_RPAREN)) return 0;
        return e;
    }
    if (p->tok.kind == LT_NAME) {
        const char *name = p->tok.text;
        if (!lua_advance(p)) return 0;
        struct lua_tree *n = lua_new_node(N_VAR, line);
        if (!n) return 0;
        n->text = name;
        return n;
    }
    lua_parse_error(line, "expected an expression");
    return 0;
}

static struct lua_tree *lua_parse_suffixed(struct lua_parser *p) {
    struct lua_tree *e = lua_parse_primary(p);
    if (!e) return 0;
    for (;;) {
        if (lua_eat_sym(p, LS_DOT)) {
            const char *name = lua_expect_name(p);
            if (!name) return 0;
            struct lua_tree *n = lua_new_node(N_INDEX, e->line);
            if (!n) return 0;
            n->a = e;
            n->b = lua_new_node(N_STR, e->line);
            if (!n->b) return 0;
            n->b->text = name;
            e = n;
            continue;
        }
        if (lua_eat_sym(p, LS_LBRACK)) {
            struct lua_tree *key = lua_parse_expr(p);
            if (!key || !lua_expect_sym(p, LS_RBRACK)) return 0;
            struct lua_tree *n = lua_new_node(N_INDEX, e->line);
            if (!n) return 0;
            n->a = e;
            n->b = key;
            e = n;
            continue;
        }
        if (lua_eat_sym(p, LS_LPAREN)) {
            struct lua_tree *first = 0;
            struct lua_tree *last = 0;
            int32_t argc = 0;
            if (!lua_is_sym(p, LS_RPAREN)) {
                for (;;) {
                    struct lua_tree *arg = lua_parse_expr(p);
                    if (!arg) return 0;
                    if (!first) first = arg; else last->next = arg;
                    last = arg;
                    argc++;
                    if (argc > LUA_MAX_ARGS) {
                        lua_parse_error(p->tok.line, "too many call arguments");
                        return 0;
                    }
                    if (!lua_eat_sym(p, LS_COMMA)) break;
                }
            }
            if (!lua_expect_sym(p, LS_RPAREN)) return 0;
            struct lua_tree *n = lua_new_node(N_CALL, e->line);
            if (!n) return 0;
            n->a = e;
            n->b = first;
            n->argc = argc;
            e = n;
            continue;
        }
        return e;
    }
}

static struct lua_tree *lua_parse_unary(struct lua_parser *p) {
    int32_t line = p->tok.line;
    if (lua_is_kw(p, LK_NOT) || lua_is_sym(p, LS_SUB) || lua_is_sym(p, LS_LEN)) {
        int op = lua_is_kw(p, LK_NOT) ? LS_NOT : p->tok.op;
        if (!lua_advance(p)) return 0;
        struct lua_tree *operand = lua_parse_unary(p);
        if (!operand) return 0;
        struct lua_tree *n = lua_new_node(N_UN, line);
        if (!n) return 0;
        n->op = (uint8_t)op;
        n->a = operand;
        return n;
    }
    return lua_parse_suffixed(p);
}

static int lua_precedence(struct lua_parser *p, int *right_assoc) {
    *right_assoc = 0;
    if (lua_is_kw(p, LK_OR)) return 1;
    if (lua_is_kw(p, LK_AND)) return 2;
    if (lua_is_sym(p, LS_EQ) || lua_is_sym(p, LS_NE) ||
        lua_is_sym(p, LS_LT) || lua_is_sym(p, LS_LE) ||
        lua_is_sym(p, LS_GT) || lua_is_sym(p, LS_GE)) return 3;
    if (lua_is_sym(p, LS_CONCAT2)) { *right_assoc = 1; return 4; }
    if (lua_is_sym(p, LS_ADD) || lua_is_sym(p, LS_SUB)) return 5;
    if (lua_is_sym(p, LS_MUL) || lua_is_sym(p, LS_DIV) ||
        lua_is_sym(p, LS_MOD)) return 6;
    if (lua_is_sym(p, LS_POW)) { *right_assoc = 1; return 7; }
    return 0;
}

static struct lua_tree *lua_parse_binary(struct lua_parser *p, int min_prec) {
    struct lua_tree *left = lua_parse_unary(p);
    if (!left) return 0;
    for (;;) {
        int right_assoc = 0;
        int prec = lua_precedence(p, &right_assoc);
        if (prec == 0 || prec < min_prec) return left;
        int op = lua_is_kw(p, LK_AND) ? LS_AND :
                 (lua_is_kw(p, LK_OR) ? LS_OR : p->tok.op);
        int32_t line = p->tok.line;
        if (!lua_advance(p)) return 0;
        struct lua_tree *right = lua_parse_binary(p, right_assoc ? prec : prec + 1);
        if (!right) return 0;
        struct lua_tree *n = lua_new_node(N_BIN, line);
        if (!n) return 0;
        n->op = (uint8_t)op;
        n->a = left;
        n->b = right;
        left = n;
    }
}

static struct lua_tree *lua_parse_expr(struct lua_parser *p) {
    return lua_parse_binary(p, 1);
}

static struct lua_tree *lua_parse_block(struct lua_parser *p) {
    int32_t line = p->tok.line;
    struct lua_tree *first = 0;
    struct lua_tree *last = 0;
    while (!lua_is_kw(p, LK_END) && !lua_is_kw(p, LK_ELSE) &&
           !lua_is_kw(p, LK_ELSEIF) && !lua_is_kw(p, LK_UNTIL) &&
           p->tok.kind != LT_EOF) {
        struct lua_tree *stmt = lua_parse_stmt(p);
        if (!stmt) return 0;
        if (!first) first = stmt; else last->next = stmt;
        last = stmt;
    }
    struct lua_tree *block = lua_new_node(N_BLOCK, line);
    if (!block) return 0;
    block->a = first;
    return block;
}

static struct lua_tree *lua_parse_local(struct lua_parser *p) {
    int32_t line = p->tok.line;
    if (!lua_advance(p)) return 0;
    if (lua_is_kw(p, LK_FUNCTION)) {
        if (!lua_advance(p)) return 0;
        const char *name = lua_expect_name(p);
        if (!name) return 0;
        struct lua_tree *fn = lua_parse_funcbody(p);
        if (!fn) return 0;
        struct lua_tree *n = lua_new_node(N_LOCALFUNC, line);
        if (!n) return 0;
        n->text = name;
        n->a = fn;
        return n;
    }
    const char *names[LUA_MAX_ARGS];
    int32_t count = 0;
    for (;;) {
        const char *name = lua_expect_name(p);
        if (!name) return 0;
        if (count >= LUA_MAX_ARGS) {
            lua_parse_error(p->tok.line, "too many local names");
            return 0;
        }
        names[count++] = name;
        if (!lua_eat_sym(p, LS_COMMA)) break;
    }
    struct lua_tree *values[LUA_MAX_ARGS];
    int32_t value_count = 0;
    if (lua_eat_sym(p, LS_ASSIGN)) {
        for (;;) {
            struct lua_tree *value = lua_parse_expr(p);
            if (!value) return 0;
            if (value_count >= LUA_MAX_ARGS) {
                lua_parse_error(p->tok.line, "too many local values");
                return 0;
            }
            values[value_count++] = value;
            if (!lua_eat_sym(p, LS_COMMA)) break;
        }
    }
    struct lua_tree *first = 0;
    struct lua_tree *last = 0;
    for (int32_t i = 0; i < count; i++) {
        struct lua_tree *n = lua_new_node(N_LOCAL, line);
        if (!n) return 0;
        n->text = names[i];
        n->a = i < value_count ? values[i] : 0;
        if (!first) first = n; else last->next = n;
        last = n;
    }
    return first;
}

static struct lua_tree *lua_parse_stmt(struct lua_parser *p) {
    int32_t line = p->tok.line;
    if (lua_eat_sym(p, LS_SEMI)) return lua_new_node(N_BLOCK, line);
    if (lua_is_kw(p, LK_LOCAL)) return lua_parse_local(p);
    if (lua_is_kw(p, LK_IF)) {
        if (!lua_advance(p)) return 0;
        struct lua_tree *root = 0;
        struct lua_tree *last = 0;
        for (;;) {
            struct lua_tree *cond = lua_parse_expr(p);
            if (!cond || !lua_expect_kw(p, LK_THEN)) return 0;
            struct lua_tree *yes = lua_parse_block(p);
            if (!yes) return 0;
            struct lua_tree *n = lua_new_node(N_IF, line);
            if (!n) return 0;
            n->a = cond;
            n->b = yes;
            if (!root) root = n; else last->c = n;
            last = n;
            if (lua_is_kw(p, LK_ELSEIF)) {
                if (!lua_advance(p)) return 0;
                continue;
            }
            if (lua_is_kw(p, LK_ELSE)) {
                if (!lua_advance(p)) return 0;
                last->c = lua_parse_block(p);
                if (!last->c) return 0;
            }
            if (!lua_expect_kw(p, LK_END)) return 0;
            return root;
        }
    }
    if (lua_is_kw(p, LK_WHILE)) {
        if (!lua_advance(p)) return 0;
        struct lua_tree *cond = lua_parse_expr(p);
        if (!cond || !lua_expect_kw(p, LK_DO)) return 0;
        struct lua_tree *body = lua_parse_block(p);
        if (!body || !lua_expect_kw(p, LK_END)) return 0;
        struct lua_tree *n = lua_new_node(N_WHILE, line);
        if (!n) return 0;
        n->a = cond; n->b = body;
        return n;
    }
    if (lua_is_kw(p, LK_REPEAT)) {
        if (!lua_advance(p)) return 0;
        struct lua_tree *body = lua_parse_block(p);
        if (!body || !lua_expect_kw(p, LK_UNTIL)) return 0;
        struct lua_tree *cond = lua_parse_expr(p);
        if (!cond) return 0;
        struct lua_tree *n = lua_new_node(N_REPEAT, line);
        if (!n) return 0;
        n->a = body; n->b = cond;
        return n;
    }
    if (lua_is_kw(p, LK_FOR)) {
        if (!lua_advance(p)) return 0;
        const char *name = lua_expect_name(p);
        if (!name) return 0;
        if (lua_eat_sym(p, LS_ASSIGN)) {
            struct lua_tree *start = lua_parse_expr(p);
            if (!start || !lua_expect_sym(p, LS_COMMA)) return 0;
            struct lua_tree *limit = lua_parse_expr(p);
            if (!limit) return 0;
            struct lua_tree *step = 0;
            if (lua_eat_sym(p, LS_COMMA)) {
                step = lua_parse_expr(p);
                if (!step) return 0;
            }
            if (!lua_expect_kw(p, LK_DO)) return 0;
            struct lua_tree *body = lua_parse_block(p);
            if (!body || !lua_expect_kw(p, LK_END)) return 0;
            struct lua_tree *n = lua_new_node(N_FOR, line);
            if (!n) return 0;
            n->text = name; n->a = start; n->b = limit; n->c = step; n->d = body;
            return n;
        }
        const char *name2 = 0;
        if (lua_eat_sym(p, LS_COMMA)) {
            name2 = lua_expect_name(p);
            if (!name2) return 0;
        }
        if (!lua_expect_kw(p, LK_IN)) return 0;
        struct lua_tree *iter = lua_parse_expr(p);
        if (!iter || !lua_expect_kw(p, LK_DO)) return 0;
        struct lua_tree *body = lua_parse_block(p);
        if (!body || !lua_expect_kw(p, LK_END)) return 0;
        struct lua_tree *n = lua_new_node(N_FORIN, line);
        if (!n) return 0;
        n->text = name; n->a = iter; n->b = body;
        if (name2) {
            n->c = lua_new_node(N_VAR, line);
            if (!n->c) return 0;
            n->c->text = name2;
        }
        return n;
    }
    if (lua_is_kw(p, LK_FUNCTION)) {
        if (!lua_advance(p)) return 0;
        const char *name = lua_expect_name(p);
        if (!name) return 0;
        struct lua_tree *fn = lua_parse_funcbody(p);
        if (!fn) return 0;
        struct lua_tree *n = lua_new_node(N_FUNCDEF, line);
        if (!n) return 0;
        n->text = name; n->a = fn;
        return n;
    }
    if (lua_is_kw(p, LK_RETURN)) {
        if (!lua_advance(p)) return 0;
        struct lua_tree *n = lua_new_node(N_RETURN, line);
        if (!n) return 0;
        if (!lua_is_kw(p, LK_END) && !lua_is_kw(p, LK_ELSE) &&
            !lua_is_kw(p, LK_ELSEIF) && !lua_is_kw(p, LK_UNTIL) &&
            p->tok.kind != LT_EOF) {
            n->a = lua_parse_expr(p);
            if (!n->a) return 0;
            while (lua_eat_sym(p, LS_COMMA)) {
                if (!lua_parse_expr(p)) return 0;
            }
        }
        lua_eat_sym(p, LS_SEMI);
        return n;
    }
    if (lua_is_kw(p, LK_BREAK)) {
        if (!lua_advance(p)) return 0;
        lua_eat_sym(p, LS_SEMI);
        return lua_new_node(N_BREAK, line);
    }
    if (lua_is_kw(p, LK_DO)) {
        if (!lua_advance(p)) return 0;
        struct lua_tree *body = lua_parse_block(p);
        if (!body || !lua_expect_kw(p, LK_END)) return 0;
        struct lua_tree *n = lua_new_node(N_DO, line);
        if (!n) return 0;
        n->a = body;
        return n;
    }
    struct lua_tree *lhs = lua_parse_suffixed(p);
    if (!lhs) return 0;
    if (lua_eat_sym(p, LS_ASSIGN)) {
        if (lhs->kind != N_VAR && lhs->kind != N_INDEX) {
            lua_parse_error(line, "assignment target must be a variable or table field");
            return 0;
        }
        struct lua_tree *rhs = lua_parse_expr(p);
        if (!rhs) return 0;
        struct lua_tree *n = lua_new_node(N_ASSIGN, line);
        if (!n) return 0;
        n->a = lhs; n->b = rhs;
        return n;
    }
    if (lhs->kind != N_CALL) {
        lua_parse_error(line, "statement is not an assignment or call");
        return 0;
    }
    struct lua_tree *n = lua_new_node(N_CALLSTMT, line);
    if (!n) return 0;
    n->a = lhs;
    return n;
}

static struct lua_tree *lua_parse_chunk(const char *source) {
    static struct lua_parser parser;
    parser.lx.p = source;
    parser.lx.line = 1;
    parser.error = 0;
    if (!lua_next_token(&parser.lx, &parser.tok)) return 0;
    struct lua_tree *block = lua_parse_block(&parser);
    if (!block) return 0;
    if (parser.tok.kind != LT_EOF) {
        lua_parse_error(parser.tok.line, "unexpected token after chunk");
        return 0;
    }
    return block;
}

/* ------------------------------------------------------------------ */
/* Values and environments                                             */
/* ------------------------------------------------------------------ */

static struct lua_value lua_make_nil(void) {
    struct lua_value v = {V_NIL, 0, 0, 0, 0, 0, 0};
    return v;
}
static struct lua_value lua_make_bool(int b) {
    struct lua_value v = {V_BOOL, (uint8_t)(b != 0), 0, 0, 0, 0, 0};
    return v;
}
static struct lua_value lua_make_int(int64_t n) {
    struct lua_value v = {V_INT, 0, 0, n, 0, 0, 0};
    return v;
}
static struct lua_value lua_make_str(const char *s) {
    struct lua_value v = {V_STR, 0, 0, 0, s ? s : "", 0, 0};
    return v;
}
static struct lua_value lua_make_table(struct lua_table *t) {
    struct lua_value v = {V_TABLE, 0, 0, 0, 0, t, 0};
    return v;
}
static struct lua_value lua_make_func(struct lua_func *f) {
    struct lua_value v = {V_FUNC, 0, 0, 0, 0, 0, f};
    return v;
}
static struct lua_value lua_make_builtin(int id) {
    struct lua_value v = {V_BUILTIN, 0, (int32_t)id, 0, 0, 0, 0};
    return v;
}

static int lua_truthy(struct lua_value v) {
    if (v.type == V_NIL) return 0;
    if (v.type == V_BOOL) return v.boolean != 0;
    return 1;
}

static const char *lua_type_name(struct lua_value v) {
    switch (v.type) {
        case V_NIL: return "nil";
        case V_BOOL: return "boolean";
        case V_INT: return "number";
        case V_STR: return "string";
        case V_TABLE: return "table";
        case V_FUNC:
        case V_BUILTIN: return "function";
        default: return "nil";
    }
}

static int lua_values_equal(struct lua_value a, struct lua_value b) {
    if (a.type != b.type) return 0;
    switch (a.type) {
        case V_NIL: return 1;
        case V_BOOL: return a.boolean == b.boolean;
        case V_INT: return a.integer == b.integer;
        case V_STR: return lua_streq(a.string, b.string);
        case V_TABLE: return a.table == b.table;
        case V_FUNC: return a.func == b.func;
        case V_BUILTIN: return a.builtin == b.builtin;
        default: return 0;
    }
}

static struct lua_env *lua_new_env(struct lua_env *parent) {
    if (lua_env_used >= LUA_MAX_ENVS) {
        lua_parse_error(lua_current_line, "too many lexical scopes");
        return 0;
    }
    struct lua_env *env = &lua_envs[lua_env_used++];
    env->parent = parent;
    env->count = 0;
    return env;
}

static struct lua_value *lua_env_find(struct lua_env *env, const char *name) {
    for (struct lua_env *e = env; e; e = e->parent) {
        for (int i = e->count - 1; i >= 0; i--)
            if (lua_streq(e->vars[i].name, name)) return &e->vars[i].value;
    }
    return 0;
}

static int lua_env_define(struct lua_env *env, const char *name, struct lua_value value) {
    if (!env || env->count >= LUA_MAX_SCOPE) {
        lua_parse_error(lua_current_line, "too many local variables");
        return 0;
    }
    env->vars[env->count].name = name;
    env->vars[env->count].value = value;
    env->count++;
    return 1;
}

static int lua_env_set(struct lua_env *env, const char *name, struct lua_value value) {
    for (struct lua_env *e = env; e; e = e->parent) {
        for (int i = e->count - 1; i >= 0; i--) {
            if (lua_streq(e->vars[i].name, name)) {
                e->vars[i].value = value;
                return 1;
            }
        }
    }
    return lua_env_define(lua_globals, name, value);
}

static struct lua_table *lua_new_table(void) {
    if (lua_table_used >= LUA_MAX_TABLES) {
        lua_parse_error(lua_current_line, "too many tables");
        return 0;
    }
    struct lua_table *t = &lua_tables[lua_table_used++];
    t->count = 0;
    return t;
}

static struct lua_value *lua_table_get(struct lua_table *t, struct lua_value key) {
    if (!t) return (struct lua_value *)&lua_nil_value;
    for (int i = 0; i < t->count; i++)
        if (lua_values_equal(t->entries[i].key, key)) return &t->entries[i].value;
    return (struct lua_value *)&lua_nil_value;
}

static struct lua_value *lua_table_get_int(struct lua_table *t, int64_t key) {
    struct lua_value k = lua_make_int(key);
    return lua_table_get(t, k);
}

static int lua_table_set(struct lua_table *t, struct lua_value key, struct lua_value value) {
    if (!t) return 0;
    if (key.type == V_NIL) return 0;
    for (int i = 0; i < t->count; i++) {
        if (lua_values_equal(t->entries[i].key, key)) {
            t->entries[i].value = value;
            return 1;
        }
    }
    if (t->count >= LUA_MAX_TABLE) {
        lua_parse_error(lua_current_line, "table is full");
        return 0;
    }
    t->entries[t->count].key = key;
    t->entries[t->count].value = value;
    t->count++;
    return 1;
}

static int64_t lua_table_length(struct lua_table *t) {
    if (!t) return 0;
    int64_t n = 0;
    while (lua_table_get_int(t, n + 1)->type != V_NIL) n++;
    return n;
}

static void lua_table_add_builtin(struct lua_table *t, const char *name, int id) {
    lua_table_set(t, lua_make_str(name), lua_make_builtin(id));
}

static struct lua_table *lua_make_math_table(void) {
    struct lua_table *t = lua_new_table();
    if (!t) return 0;
    lua_table_add_builtin(t, "abs", B_MATH_ABS);
    lua_table_add_builtin(t, "floor", B_MATH_FLOOR);
    lua_table_add_builtin(t, "max", B_MATH_MAX);
    lua_table_add_builtin(t, "min", B_MATH_MIN);
    return t;
}

static struct lua_table *lua_make_string_table(void) {
    struct lua_table *t = lua_new_table();
    if (!t) return 0;
    lua_table_add_builtin(t, "len", B_STR_LEN);
    lua_table_add_builtin(t, "sub", B_STR_SUB);
    lua_table_add_builtin(t, "upper", B_STR_UPPER);
    lua_table_add_builtin(t, "lower", B_STR_LOWER);
    lua_table_add_builtin(t, "rep", B_STR_REP);
    return t;
}

static struct lua_table *lua_make_library_table(void) {
    struct lua_table *t = lua_new_table();
    if (!t) return 0;
    lua_table_add_builtin(t, "insert", B_TABLE_INSERT);
    lua_table_add_builtin(t, "concat", B_TABLE_CONCAT);
    lua_table_add_builtin(t, "remove", B_TABLE_REMOVE);
    return t;
}

static int lua_init_globals(void) {
    lua_globals = lua_new_env(0);
    if (!lua_globals) return 0;
    lua_env_define(lua_globals, "print", lua_make_builtin(B_PRINT));
    lua_env_define(lua_globals, "type", lua_make_builtin(B_TYPE));
    lua_env_define(lua_globals, "tostring", lua_make_builtin(B_TOSTRING));
    lua_env_define(lua_globals, "tonumber", lua_make_builtin(B_TONUMBER));
    lua_env_define(lua_globals, "pairs", lua_make_builtin(B_PAIRS));
    lua_env_define(lua_globals, "ipairs", lua_make_builtin(B_IPAIRS));
    lua_env_define(lua_globals, "next", lua_make_builtin(B_NEXT));
    lua_env_define(lua_globals, "assert", lua_make_builtin(B_ASSERT));
    lua_env_define(lua_globals, "error", lua_make_builtin(B_ERROR));
    lua_env_define(lua_globals, "select", lua_make_builtin(B_SELECT));
    struct lua_table *math = lua_make_math_table();
    struct lua_table *str = lua_make_string_table();
    struct lua_table *tbl = lua_make_library_table();
    if (!math || !str || !tbl) return 0;
    lua_env_define(lua_globals, "math", lua_make_table(math));
    lua_env_define(lua_globals, "string", lua_make_table(str));
    lua_env_define(lua_globals, "table", lua_make_table(tbl));
    return 1;
}

/* ------------------------------------------------------------------ */
/* Runtime                                                             */
/* ------------------------------------------------------------------ */

enum { FLOW_OK = 0, FLOW_BREAK, FLOW_RETURN, FLOW_ERROR };
static int lua_flow;
static struct lua_value lua_return_value;

static int lua_runtime_error(const char *message) {
    if (!lua_error[0]) lua_parse_error(lua_current_line, message);
    lua_flow = FLOW_ERROR;
    return FLOW_ERROR;
}

static void lua_value_to_text(struct lua_value v, char *buffer, int max) {
    int n = 0;
    if (v.type == V_NIL) {
        const char *s = "nil";
        while (s[n] && n < max - 1) { buffer[n] = s[n]; n++; }
    } else if (v.type == V_BOOL) {
        const char *s = v.boolean ? "true" : "false";
        while (s[n] && n < max - 1) { buffer[n] = s[n]; n++; }
    } else if (v.type == V_INT) {
        char number[24];
        lua_int_to_string_buffer(number, v.integer);
        while (number[n] && n < max - 1) { buffer[n] = number[n]; n++; }
    } else if (v.type == V_STR) {
        const char *s = v.string ? v.string : "";
        while (s[n] && n < max - 1) { buffer[n] = s[n]; n++; }
    } else if (v.type == V_TABLE) {
        const char *s = "table";
        while (s[n] && n < max - 1) { buffer[n] = s[n]; n++; }
    } else {
        const char *s = "function";
        while (s[n] && n < max - 1) { buffer[n] = s[n]; n++; }
    }
    buffer[n] = '\0';
}

static int lua_eval(struct lua_tree *n, struct lua_env *env, struct lua_value *out);
static int lua_exec_block(struct lua_tree *n, struct lua_env *env);

static int lua_call_value(struct lua_value callee, struct lua_tree *args,
                           struct lua_env *env, struct lua_value *out) {
    struct lua_value values[LUA_MAX_ARGS];
    int count = 0;
    while (args && count < LUA_MAX_ARGS) {
        if (!lua_eval(args, env, &values[count])) return 0;
        count++;
        args = args->next;
    }
    *out = lua_make_nil();
    if (callee.type == V_FUNC) {
        if (lua_call_depth >= 64) {
            lua_runtime_error("function recursion limit reached");
            return 0;
        }
        struct lua_func *f = callee.func;
        lua_call_depth++;
        struct lua_env *call_env = lua_new_env(f->closure ? f->closure : lua_globals);
        if (!call_env) { lua_call_depth--; return 0; }
        for (int i = 0; i < f->nparams; i++)
            lua_env_define(call_env, f->params[i],
                           i < count ? values[i] : lua_make_nil());
        int flow = lua_flow;
        struct lua_value saved = lua_return_value;
        lua_flow = FLOW_OK;
        lua_return_value = lua_make_nil();
        if (!lua_exec_block(f->body, call_env)) {
            lua_call_depth--;
            return 0;
        }
        if (lua_flow == FLOW_RETURN) *out = lua_return_value;
        lua_flow = flow;
        lua_return_value = saved;
        lua_call_depth--;
        return 1;
    }
    if (callee.type == V_BUILTIN) {
        int id = callee.builtin;
        if (id == B_PRINT) {
            for (int i = 0; i < count; i++) {
                if (i) lua_out_char('\t');
                if (values[i].type == V_STR) lua_out_text(values[i].string);
                else {
                    char buffer[64];
                    lua_value_to_text(values[i], buffer, sizeof(buffer));
                    lua_out_text(buffer);
                }
            }
            lua_out_char('\n');
            return 1;
        }
        if (id == B_TYPE) {
            *out = lua_make_str(count ? lua_type_name(values[0]) : "nil");
            return 1;
        }
        if (id == B_TOSTRING) {
            char buffer[64];
            lua_value_to_text(count ? values[0] : lua_make_nil(), buffer, sizeof(buffer));
            *out = lua_make_str(lua_store_string(buffer, lua_strlen(buffer)));
            return 1;
        }
        if (id == B_TONUMBER) {
            if (!count) { *out = lua_make_nil(); return 1; }
            if (values[0].type == V_INT) { *out = values[0]; return 1; }
            if (values[0].type == V_STR) {
                const char *s = values[0].string;
                int sign = 1;
                if (*s == '-') { sign = -1; s++; }
                int64_t value = 0;
                int digits = 0;
                while (*s >= '0' && *s <= '9') {
                    value = value * 10 + (*s - '0');
                    s++;
                    digits++;
                }
                *out = digits ? lua_make_int(value * sign) : lua_make_nil();
                return 1;
            }
            *out = lua_make_nil();
            return 1;
        }
        if (id == B_ASSERT) {
            if (!count || !lua_truthy(values[0])) {
                if (count > 1 && values[1].type == V_STR) {
                    lua_runtime_error(values[1].string);
                } else {
                    lua_runtime_error("assertion failed");
                }
                return 0;
            }
            *out = count ? values[0] : lua_make_nil();
            return 1;
        }
        if (id == B_ERROR) {
            lua_runtime_error(count && values[0].type == V_STR ? values[0].string : "script error");
            return 0;
        }
        if (id == B_SELECT) {
            if (!count) return 0;
            int index = (int)values[0].integer;
            if (index < 1 || index >= count) { *out = lua_make_nil(); return 1; }
            *out = values[index];
            return 1;
        }
        if (id == B_MATH_ABS) {
            if (count && values[0].type == V_INT) {
                *out = lua_make_int(values[0].integer < 0 ? -values[0].integer : values[0].integer);
            }
            return 1;
        }
        if (id == B_MATH_FLOOR) {
            if (count) *out = values[0];
            return 1;
        }
        if (id == B_MATH_MAX || id == B_MATH_MIN) {
            if (!count) { *out = lua_make_nil(); return 1; }
            int64_t result = values[0].integer;
            for (int i = 1; i < count; i++) {
                if (id == B_MATH_MAX && values[i].integer > result) result = values[i].integer;
                if (id == B_MATH_MIN && values[i].integer < result) result = values[i].integer;
            }
            *out = lua_make_int(result);
            return 1;
        }
        if (id == B_STR_LEN) {
            *out = lua_make_int(count && values[0].type == V_STR ? lua_strlen(values[0].string) : 0);
            return 1;
        }
        if (id == B_STR_SUB) {
            const char *s = count && values[0].type == V_STR ? values[0].string : "";
            int len = lua_strlen(s);
            int start = count > 1 ? (int)values[1].integer : 1;
            int end = count > 2 ? (int)values[2].integer : len;
            if (start < 1) start = 1;
            if (end > len) end = len;
            if (end < start) { *out = lua_make_str(""); return 1; }
            *out = lua_make_str(lua_store_string(s + start - 1, end - start + 1));
            return 1;
        }
        if (id == B_STR_UPPER || id == B_STR_LOWER) {
            const char *s = count && values[0].type == V_STR ? values[0].string : "";
            int len = lua_strlen(s);
            char *buffer = (char *)lua_store_string(s, len);
            for (int i = 0; i < len; i++) {
                if (id == B_STR_UPPER && buffer[i] >= 'a' && buffer[i] <= 'z')
                    ((char *)buffer)[i] = (char)(buffer[i] - 32);
                if (id == B_STR_LOWER && buffer[i] >= 'A' && buffer[i] <= 'Z')
                    ((char *)buffer)[i] = (char)(buffer[i] + 32);
            }
            *out = lua_make_str(buffer);
            return 1;
        }
        if (id == B_STR_REP) {
            const char *s = count && values[0].type == V_STR ? values[0].string : "";
            int n = count > 1 ? (int)values[1].integer : 1;
            int len = lua_strlen(s);
            int total = len * n;
            if (n < 0 || total > 1024) { lua_runtime_error("string.rep is too large"); return 0; }
            char buffer[1024];
            int pos = 0;
            for (int i = 0; i < n; i++)
                for (int j = 0; j < len; j++) buffer[pos++] = s[j];
            *out = lua_make_str(lua_store_string(buffer, pos));
            return 1;
        }
        if (id == B_TABLE_INSERT) {
            if (count >= 2 && values[0].type == V_TABLE) {
                lua_table_set(values[0].table, lua_make_int(lua_table_length(values[0].table) + 1), values[1]);
            }
            return 1;
        }
        if (id == B_TABLE_CONCAT) {
            if (count && values[0].type == V_TABLE) {
                lua_out_text("");
                char buffer[512];
                int pos = 0;
                int64_t len = lua_table_length(values[0].table);
                const char *sep = count > 1 && values[1].type == V_STR ? values[1].string : "";
                for (int64_t i = 1; i <= len && pos < 480; i++) {
                    struct lua_value *v = lua_table_get_int(values[0].table, i);
                    if (v->type == V_STR) {
                        for (int j = 0; v->string[j] && pos < 500; j++) buffer[pos++] = v->string[j];
                    }
                    if (i < len && sep[0] && pos < 500) buffer[pos++] = sep[0];
                }
                *out = lua_make_str(lua_store_string(buffer, pos));
            }
            return 1;
        }
        if (id == B_TABLE_REMOVE) {
            if (count && values[0].type == V_TABLE) {
                int64_t len = lua_table_length(values[0].table);
                *out = *lua_table_get_int(values[0].table, len);
                lua_table_get_int(values[0].table, len)->type = V_NIL;
            }
            return 1;
        }
        if (id == B_IO_WRITE) {
            for (int i = 0; i < count; i++) {
                if (values[i].type == V_STR) lua_out_text(values[i].string);
                else {
                    char buffer[64];
                    lua_value_to_text(values[i], buffer, sizeof(buffer));
                    lua_out_text(buffer);
                }
            }
            return 1;
        }
        if (id == B_PAIRS || id == B_IPAIRS || id == B_NEXT) {
            return 1;
        }
    }
    lua_runtime_error("attempt to call a non-function value");
    return 0;
}

static int lua_exec_stmt(struct lua_tree *n, struct lua_env *env);
static int lua_exec_block(struct lua_tree *n, struct lua_env *env);

static int lua_exec_block(struct lua_tree *n, struct lua_env *env) {
    if (!n) return 1;
    struct lua_env *block_env = lua_new_env(env);
    if (!block_env) return 0;
    for (struct lua_tree *s = n->a; s; s = s->next) {
        if (!lua_exec_stmt(s, block_env)) return 0;
        if (lua_flow == FLOW_BREAK || lua_flow == FLOW_RETURN) return 1;
    }
    return 1;
}

static int lua_eval_index(struct lua_tree *n, struct lua_env *env, struct lua_value *out) {
    struct lua_value base;
    struct lua_value key;
    if (!lua_eval(n->a, env, &base) || !lua_eval(n->b, env, &key)) return 0;
    if (base.type != V_TABLE) {
        lua_runtime_error("attempt to index a non-table value");
        return 0;
    }
    *out = *lua_table_get(base.table, key);
    return 1;
}

static int lua_eval_binary(struct lua_tree *n, struct lua_env *env, struct lua_value *out) {
    if (n->op == LS_AND) {
        struct lua_value left;
        if (!lua_eval(n->a, env, &left)) return 0;
        if (!lua_truthy(left)) { *out = left; return 1; }
        return lua_eval(n->b, env, out);
    }
    if (n->op == LS_OR) {
        struct lua_value left;
        if (!lua_eval(n->a, env, &left)) return 0;
        if (lua_truthy(left)) { *out = left; return 1; }
        return lua_eval(n->b, env, out);
    }
    struct lua_value left;
    struct lua_value right;
    if (!lua_eval(n->a, env, &left) || !lua_eval(n->b, env, &right)) return 0;
    if (n->op == LS_CONCAT2) {
        char buffer[1024];
        int pos = 0;
        char left_text[64];
        char right_text[64];
        lua_value_to_text(left, left_text, sizeof(left_text));
        lua_value_to_text(right, right_text, sizeof(right_text));
        for (int i = 0; left_text[i] && pos < 1000; i++) buffer[pos++] = left_text[i];
        for (int i = 0; right_text[i] && pos < 1000; i++) buffer[pos++] = right_text[i];
        *out = lua_make_str(lua_store_string(buffer, pos));
        return 1;
    }
    if (n->op == LS_EQ) { *out = lua_make_bool(lua_values_equal(left, right)); return 1; }
    if (n->op == LS_NE) { *out = lua_make_bool(!lua_values_equal(left, right)); return 1; }
    if (n->op == LS_LT || n->op == LS_LE || n->op == LS_GT || n->op == LS_GE) {
        int cmp = 0;
        if (left.type == V_INT && right.type == V_INT) {
            cmp = left.integer < right.integer ? -1 : (left.integer > right.integer ? 1 : 0);
        } else if (left.type == V_STR && right.type == V_STR) {
            const char *a = left.string;
            const char *b = right.string;
            while (*a && *a == *b) { a++; b++; }
            cmp = (unsigned char)*a < (unsigned char)*b ? -1 : ((unsigned char)*a > (unsigned char)*b ? 1 : 0);
        } else {
            lua_runtime_error("comparison needs two numbers or two strings");
            return 0;
        }
        int result = 0;
        if (n->op == LS_LT) result = cmp < 0;
        else if (n->op == LS_LE) result = cmp <= 0;
        else if (n->op == LS_GT) result = cmp > 0;
        else result = cmp >= 0;
        *out = lua_make_bool(result);
        return 1;
    }
    if (left.type != V_INT || right.type != V_INT) {
        lua_runtime_error("arithmetic needs two numbers");
        return 0;
    }
    switch (n->op) {
        case LS_ADD: *out = lua_make_int(left.integer + right.integer); break;
        case LS_SUB: *out = lua_make_int(left.integer - right.integer); break;
        case LS_MUL: *out = lua_make_int(left.integer * right.integer); break;
        case LS_DIV:
            if (right.integer == 0) { lua_runtime_error("division by zero"); return 0; }
            *out = lua_make_int(left.integer / right.integer); break;
        case LS_MOD:
            if (right.integer == 0) { lua_runtime_error("division by zero"); return 0; }
            *out = lua_make_int(left.integer % right.integer); break;
        case LS_POW: {
            int64_t result = 1;
            int64_t base = left.integer;
            int64_t power = right.integer;
            if (power < 0) { *out = lua_make_int(0); break; }
            while (power > 0) {
                if (power & 1) result *= base;
                base *= base;
                power >>= 1;
            }
            *out = lua_make_int(result);
            break;
        }
        default: *out = lua_make_nil(); break;
    }
    return 1;
}

static int lua_eval(struct lua_tree *n, struct lua_env *env, struct lua_value *out) {
    if (!n) { *out = lua_make_nil(); return 1; }
    if (++lua_steps > LUA_MAX_STEPS) {
        lua_runtime_error("execution step limit reached");
        return 0;
    }
    lua_current_line = n->line;
    switch (n->kind) {
        case N_NIL: *out = lua_make_nil(); return 1;
        case N_TRUE: *out = lua_make_bool(1); return 1;
        case N_FALSE: *out = lua_make_bool(0); return 1;
        case N_NUM: *out = lua_make_int(n->number); return 1;
        case N_STR: *out = lua_make_str(n->text); return 1;
        case N_VAR: {
            struct lua_value *v = lua_env_find(env, n->text);
            *out = v ? *v : lua_make_nil();
            return 1;
        }
        case N_INDEX: return lua_eval_index(n, env, out);
        case N_CALL: {
            struct lua_value callee;
            if (!lua_eval(n->a, env, &callee)) return 0;
            return lua_call_value(callee, n->b, env, out);
        }
        case N_BIN: return lua_eval_binary(n, env, out);
        case N_UN: {
            struct lua_value v;
            if (!lua_eval(n->a, env, &v)) return 0;
            if (n->op == LS_NOT) { *out = lua_make_bool(!lua_truthy(v)); return 1; }
            if (n->op == LS_NEG) {
                if (v.type != V_INT) { lua_runtime_error("unary minus needs a number"); return 0; }
                *out = lua_make_int(-v.integer);
                return 1;
            }
            if (n->op == LS_LEN) {
                if (v.type == V_STR) { *out = lua_make_int(lua_strlen(v.string)); return 1; }
                if (v.type == V_TABLE) { *out = lua_make_int(lua_table_length(v.table)); return 1; }
                lua_runtime_error("length needs a string or table");
                return 0;
            }
            *out = lua_make_nil();
            return 1;
        }
        case N_TABLE: {
            struct lua_table *t = lua_new_table();
            if (!t) return 0;
            int64_t index = 1;
            for (struct lua_tree *field = n->a; field; field = field->next) {
                struct lua_value value;
                if (!lua_eval(field->b, env, &value)) return 0;
                struct lua_value key;
                if (field->op == 1) {
                    if (!lua_eval(field->a, env, &key)) return 0;
                } else if (field->op == 2) {
                    key = lua_make_str(field->text);
                } else {
                    key = lua_make_int(index++);
                }
                lua_table_set(t, key, value);
            }
            *out = lua_make_table(t);
            return 1;
        }
        case N_FUNC: n->func->closure = env; *out = lua_make_func(n->func); return 1;
        default: *out = lua_make_nil(); return 1;
    }
}

static int lua_exec_stmt(struct lua_tree *n, struct lua_env *env) {
    if (!n) return 1;
    if (++lua_steps > LUA_MAX_STEPS) {
        lua_runtime_error("execution step limit reached");
        return 0;
    }
    lua_current_line = n->line;
    switch (n->kind) {
        case N_BLOCK: return lua_exec_block(n, env);
        case N_LOCAL: {
            struct lua_value value = lua_make_nil();
            if (n->a && !lua_eval(n->a, env, &value)) return 0;
            return lua_env_define(env, n->text, value);
        }
        case N_LOCALFUNC: {
            if (!n->a || n->a->kind != N_FUNC) return 1;
            struct lua_func *f = n->a->func;
            f->closure = env;
            if (!lua_env_define(env, n->text, lua_make_func(f))) return 0;
            return 1;
        }
        case N_FUNCDEF: {
            if (!n->a || n->a->kind != N_FUNC) return 1;
            struct lua_func *f = n->a->func;
            f->closure = env;
            return lua_env_set(env, n->text, lua_make_func(f));
        }
        case N_ASSIGN: {
            struct lua_value value;
            if (!lua_eval(n->b, env, &value)) return 0;
            if (n->a->kind == N_VAR) return lua_env_set(env, n->a->text, value);
            struct lua_value table;
            struct lua_value key;
            if (!lua_eval(n->a->a, env, &table) || !lua_eval(n->a->b, env, &key)) return 0;
            if (table.type != V_TABLE) {
                lua_runtime_error("assignment target is not a table");
                return 0;
            }
            return lua_table_set(table.table, key, value);
        }
        case N_CALLSTMT: {
            struct lua_value unused;
            return lua_eval(n->a, env, &unused);
        }
        case N_IF: {
            struct lua_value cond;
            if (!lua_eval(n->a, env, &cond)) return 0;
            if (lua_truthy(cond)) return lua_exec_block(n->b, env);
            if (n->c) return lua_exec_stmt(n->c, env);
            return 1;
        }
        case N_WHILE: {
            for (;;) {
                struct lua_value cond;
                if (!lua_eval(n->a, env, &cond)) return 0;
                if (!lua_truthy(cond)) return 1;
                if (!lua_exec_block(n->b, env)) return 0;
                if (lua_flow == FLOW_BREAK) { lua_flow = FLOW_OK; return 1; }
                if (lua_flow == FLOW_RETURN || lua_flow == FLOW_ERROR) return 1;
                if (++lua_steps > LUA_MAX_STEPS) {
                    lua_runtime_error("execution step limit reached");
                    return 0;
                }
            }
        }
        case N_REPEAT: {
            for (;;) {
                struct lua_env *repeat_env = lua_new_env(env);
                if (!repeat_env) return 0;
                if (!lua_exec_block(n->a, repeat_env)) return 0;
                if (lua_flow == FLOW_BREAK) { lua_flow = FLOW_OK; return 1; }
                if (lua_flow == FLOW_RETURN || lua_flow == FLOW_ERROR) return 1;
                struct lua_value cond;
                if (!lua_eval(n->b, repeat_env, &cond)) return 0;
                if (lua_truthy(cond)) return 1;
            }
        }
        case N_FOR: {
            struct lua_value start;
            struct lua_value limit;
            struct lua_value step;
            if (!lua_eval(n->a, env, &start) || !lua_eval(n->b, env, &limit)) return 0;
            if (n->c) {
                if (!lua_eval(n->c, env, &step)) return 0;
            } else {
                step = lua_make_int(1);
            }
            if (start.type != V_INT || limit.type != V_INT || step.type != V_INT ||
                step.integer == 0) {
                lua_runtime_error("numeric for needs integer bounds and a non-zero step");
                return 0;
            }
            for (int64_t i = start.integer;
                 step.integer > 0 ? i <= limit.integer : i >= limit.integer;
                 i += step.integer) {
                struct lua_env *loop_env = lua_new_env(env);
                if (!loop_env) return 0;
                lua_env_define(loop_env, n->text, lua_make_int(i));
                if (!lua_exec_block(n->d, loop_env)) return 0;
                if (lua_flow == FLOW_BREAK) { lua_flow = FLOW_OK; return 1; }
                if (lua_flow == FLOW_RETURN || lua_flow == FLOW_ERROR) return 1;
            }
            return 1;
        }
        case N_FORIN: {
            if (!n->a || n->a->kind != N_CALL || !n->a->a ||
                n->a->a->kind != N_VAR) {
                lua_runtime_error("generic for needs pairs() or ipairs()");
                return 0;
            }
            const char *iterator = n->a->a->text;
            int is_pairs = lua_streq(iterator, "pairs");
            int is_ipairs = lua_streq(iterator, "ipairs");
            if (!is_pairs && !is_ipairs) {
                lua_runtime_error("generic for needs pairs() or ipairs()");
                return 0;
            }
            struct lua_value table;
            if (!lua_eval(n->a->b, env, &table) || table.type != V_TABLE) {
                lua_runtime_error("pairs() needs a table");
                return 0;
            }
            int64_t index = 1;
            for (;;) {
                struct lua_value key;
                struct lua_value value;
                if (is_ipairs) {
                    value = *lua_table_get_int(table.table, index);
                    if (value.type == V_NIL) break;
                    key = lua_make_int(index++);
                } else {
                    if (index - 1 >= table.table->count) break;
                    key = table.table->entries[index - 1].key;
                    value = table.table->entries[index - 1].value;
                    index++;
                }
                struct lua_env *loop_env = lua_new_env(env);
                if (!loop_env) return 0;
                lua_env_define(loop_env, n->text, key);
                if (n->c) lua_env_define(loop_env, n->c->text, value);
                if (!lua_exec_block(n->b, loop_env)) return 0;
                if (lua_flow == FLOW_BREAK) { lua_flow = FLOW_OK; return 1; }
                if (lua_flow == FLOW_RETURN || lua_flow == FLOW_ERROR) return 1;
            }
            return 1;
        }
        case N_RETURN:
            lua_return_value = lua_make_nil();
            if (n->a && !lua_eval(n->a, env, &lua_return_value)) return 0;
            lua_flow = FLOW_RETURN;
            return 1;
        case N_BREAK:
            lua_flow = FLOW_BREAK;
            return 1;
        case N_DO: return lua_exec_block(n->a, env);
        default:
            return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

int lua_run(const char *source, lua_putchar_fn putchar_fn) {
    if (!source) {
        lua_parse_error(0, "no Lua source");
        return -1;
    }
    lua_node_used = 0;
    lua_name_used = 0;
    lua_string_used = 0;
    lua_table_used = 0;
    lua_env_used = 0;
    lua_func_used = 0;
    lua_error[0] = '\0';
    lua_output = putchar_fn;
    lua_globals = 0;
    lua_steps = 0;
    lua_call_depth = 0;
    lua_current_line = 0;
    lua_flow = FLOW_OK;
    lua_return_value = lua_make_nil();

    struct lua_tree *chunk = lua_parse_chunk(source);
    if (!chunk) return -1;
    if (!lua_init_globals()) return -1;
    lua_flow = FLOW_OK;
    struct lua_env *chunk_env = lua_new_env(lua_globals);
    if (!chunk_env) return -1;
    if (!lua_exec_block(chunk, chunk_env)) return -1;
    if (lua_flow == FLOW_ERROR) return -1;
    return 0;
}
