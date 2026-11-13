#include <solis/apps/ccompiler.h>
#include <solis/jit.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Solis native C compiler
 * -----------------------
 * This is a small, self-contained compiler rather than an interpreter.  It
 * parses a useful C subset, emits x86-64 instructions, and places string
 * literals and globals in a separate data window.  The generated image is an
 * SCX file consumed by the kernel's jit_execute service.
 *
 * Supported: int/char/void, pointers as opaque 64-bit values, globals, locals,
 * functions with up to six parameters, if/else, while, for, break, continue,
 * return, arithmetic/bitwise/logical operators, assignments, ++/--, strings,
 * and printf/puts/putchar.
 *
 * Not supported: structs, arrays, floating point, preprocessor macros,
 * function pointers, variadic user functions, and dereferencing pointers.
 */

#define CC_MAX_NODES   4096
#define CC_MAX_SYMS    256
#define CC_MAX_FUNCS   64
#define CC_MAX_NAMES   8192
#define CC_MAX_STRINGS 8192
#define CC_MAX_CODE    SOLIS_JIT_MAX_CODE
#define CC_MAX_DATA    SOLIS_JIT_MAX_DATA
#define CC_MAX_ARGS    6
#define CC_MAX_LOOPS   8
#define CC_MAX_LOOP_JUMPS 32
#define CC_MAX_FIXUPS  256

#define CC_DATA_BASE ((uintptr_t)SOLIS_JIT_DATA_BASE)

typedef int32_t cc_value;

enum {
    TO_EOF = 0, TO_IDENT, TO_NUM, TO_STR, TO_CHAR, TO_KW, TO_OP
};

enum {
    K_NONE = 0, K_INT, K_CHAR, K_VOID, K_UNSIGNED, K_SIGNED, K_CONST,
    K_STATIC, K_EXTERN, K_IF, K_ELSE, K_WHILE, K_FOR, K_RETURN, K_BREAK,
    K_CONTINUE, K_SIZEOF, K_DO
};

enum {
    OP_NONE = 0, OP_LPAREN, OP_RPAREN, OP_LBRACE, OP_RBRACE, OP_SEMI,
    OP_COMMA, OP_PLUS, OP_MINUS, OP_STAR, OP_SLASH, OP_PERCENT, OP_AMP,
    OP_PIPE, OP_CARET, OP_TILDE, OP_BANG, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_EQ, OP_NE, OP_ANDAND, OP_OROR, OP_SHL, OP_SHR, OP_ASSIGN,
    OP_ADD_ASSIGN, OP_SUB_ASSIGN, OP_MUL_ASSIGN, OP_DIV_ASSIGN,
    OP_MOD_ASSIGN, OP_AND_ASSIGN, OP_OR_ASSIGN, OP_XOR_ASSIGN,
    OP_SHL_ASSIGN, OP_SHR_ASSIGN, OP_INC, OP_DEC, OP_QUESTION, OP_COLON,
    OP_LBRACK, OP_RBRACK, OP_DOT
};

enum {
    N_NUM = 0, N_STR, N_VAR, N_CALL, N_BIN, N_UN, N_ASSIGN, N_COND,
    N_PRE, N_POST, N_COMMA, N_DECL, N_BLOCK, N_IF, N_WHILE, N_FOR,
    N_RETURN, N_BREAK, N_CONTINUE, N_EXPR, N_EMPTY, N_DO
};

enum { BI_NONE = 0, BI_PRINTF, BI_PUTS, BI_PUTCHAR };

struct cc_node {
    uint8_t kind;
    uint8_t op;
    int32_t line;
    int64_t num;             /* literal, string data offset, or operator */
    const char *name;        /* identifier/call name */
    int32_t sym;             /* variable symbol index */
    int32_t argc;
    uint8_t builtin;
    struct cc_node *a, *b, *c, *d;
    struct cc_node *next;
};

struct cc_sym {
    const char *name;
    int32_t offset;
    int32_t func;
    uint8_t global;
    uint8_t active;
    uint8_t is_param;
};

struct cc_func {
    const char *name;
    int32_t nparams;
    int32_t param_syms[CC_MAX_ARGS];
    struct cc_node *body;
    int32_t frame_size;
    int32_t offset;
    uint8_t defined;
};

struct cc_token {
    uint8_t kind;
    uint8_t op;
    int32_t line;
    int64_t num;
    const char *text;
};

struct cc_lexer {
    const char *p;
    int32_t line;
};

struct cc_parser {
    struct cc_lexer lx;
    struct cc_token tok;
    int32_t current_func;
    int32_t frame_cursor;
    int32_t scope_mark;
};

static struct cc_node cc_nodes[CC_MAX_NODES];
static int32_t cc_node_used;
static char cc_names[CC_MAX_NAMES];
static int32_t cc_name_used;
static uint8_t cc_data[CC_MAX_DATA];
static int32_t cc_data_used;
static uint8_t cc_code[CC_MAX_CODE];
static int32_t cc_code_used;
static struct cc_sym cc_syms[CC_MAX_SYMS];
static int32_t cc_sym_used;
static struct cc_func cc_funcs[CC_MAX_FUNCS];
static int32_t cc_func_used;
static char cc_error[192];
static int32_t cc_error_line;
static int32_t cc_main_func;
static struct cc_parser *cc_active_parser;

struct cc_loop_ctx {
    int32_t breaks[CC_MAX_LOOP_JUMPS];
    int32_t break_count;
    int32_t continues[CC_MAX_LOOP_JUMPS];
    int32_t continue_count;
    int32_t continue_target;
    int32_t break_target;
};
static struct cc_loop_ctx cc_loops[CC_MAX_LOOPS];
static int32_t cc_loop_sp;

struct cc_call_fix {
    int32_t position;
    int32_t function;
};
static struct cc_call_fix cc_fixups[CC_MAX_FIXUPS];
static int32_t cc_fixup_used;

static void cc_set_error(int32_t line, const char *message) {
    if (cc_error[0]) return;
    cc_error_line = line;
    int i = 0;
    while (message[i] && i < 150) {
        cc_error[i] = message[i];
        i++;
    }
    if (line > 0) {
        cc_error[i++] = ' ';
        cc_error[i++] = '(';
        cc_error[i++] = 'l';
        cc_error[i++] = 'i';
        cc_error[i++] = 'n';
        cc_error[i++] = 'e';
        cc_error[i++] = ' ';
        char digits[12];
        int n = 0;
        int32_t v = line;
        if (v == 0) digits[n++] = '0';
        while (v > 0 && n < 11) {
            digits[n++] = (char)('0' + v % 10);
            v /= 10;
        }
        while (n > 0) cc_error[i++] = digits[--n];
        cc_error[i++] = ')';
    }
    cc_error[i] = '\0';
}

const char *cc_last_error(void) {
    return cc_error[0] ? cc_error : "unknown compiler error";
}

static const char *cc_intern(const char *text, int32_t length) {
    if (cc_name_used + length + 1 > CC_MAX_NAMES) {
        cc_set_error(0, "compiler name table is full");
        return "";
    }
    char *result = cc_names + cc_name_used;
    for (int32_t i = 0; i < length; i++) result[i] = text[i];
    result[length] = '\0';
    cc_name_used += length + 1;
    return result;
}

static int32_t cc_add_data(const uint8_t *bytes, int32_t length) {
    if (length < 0 || (uint32_t)(cc_data_used + length) > CC_MAX_DATA) {
        cc_set_error(0, "over 64 KiB of program data");
        return -1;
    }
    int32_t offset = cc_data_used;
    for (int32_t i = 0; i < length; i++) cc_data[cc_data_used++] = bytes[i];
    return offset;
}

static int32_t cc_namelen(const char *text) {
    int32_t n = 0;
    while (text[n]) n++;
    return n;
}

static int cc_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int cc_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int cc_is_digit(char c) { return c >= '0' && c <= '9'; }
static int cc_is_ident(char c) { return cc_is_ident_start(c) || cc_is_digit(c); }

static int cc_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void cc_skip_space(struct cc_lexer *lx) {
    for (;;) {
        char c = *lx->p;
        if (c == '\n') { lx->line++; lx->p++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { lx->p++; continue; }
        if (c == '#') {
            while (*lx->p && *lx->p != '\n') lx->p++;
            continue;
        }
        if (c == '/' && lx->p[1] == '/') {
            while (*lx->p && *lx->p != '\n') lx->p++;
            continue;
        }
        if (c == '/' && lx->p[1] == '*') {
            lx->p += 2;
            while (*lx->p && !(lx->p[0] == '*' && lx->p[1] == '/')) {
                if (*lx->p == '\n') lx->line++;
                lx->p++;
            }
            if (*lx->p) lx->p += 2;
            continue;
        }
        return;
    }
}

static int cc_keyword(const char *text, int32_t length) {
    static const struct { const char *name; int32_t id; } kw[] = {
        {"int", K_INT}, {"char", K_CHAR}, {"void", K_VOID},
        {"unsigned", K_UNSIGNED}, {"signed", K_SIGNED}, {"const", K_CONST},
        {"static", K_STATIC}, {"extern", K_EXTERN}, {"if", K_IF},
        {"else", K_ELSE}, {"while", K_WHILE}, {"for", K_FOR},
        {"return", K_RETURN}, {"break", K_BREAK}, {"continue", K_CONTINUE},
        {"sizeof", K_SIZEOF}, {"do", K_DO}
    };
    for (int i = 0; i < (int)(sizeof(kw) / sizeof(kw[0])); i++) {
        int32_t n = cc_namelen(kw[i].name);
        if (n != length) continue;
        int match = 1;
        for (int32_t j = 0; j < length; j++) {
            if (text[j] != kw[i].name[j]) { match = 0; break; }
        }
        if (match) return kw[i].id;
    }
    return K_NONE;
}

static int cc_read_escape(const char **pp) {
    const char *p = *pp;
    char c = *p++;
    int value;
    switch (c) {
        case 'n': value = '\n'; break;
        case 't': value = '\t'; break;
        case 'r': value = '\r'; break;
        case '0': value = 0; break;
        case 'a': value = 7; break;
        case 'b': value = 8; break;
        case 'f': value = 12; break;
        case 'v': value = 11; break;
        case '\\': value = '\\'; break;
        case '\'': value = '\''; break;
        case '"': value = '"'; break;
        case 'x': {
            value = 0;
            int digits = 0;
            while (cc_hex_value(*p) >= 0 && digits < 2) {
                value = value * 16 + cc_hex_value(*p);
                p++;
                digits++;
            }
            break;
        }
        default: value = (unsigned char)c; break;
    }
    *pp = p;
    return value;
}

static int cc_next_token(struct cc_lexer *lx, struct cc_token *t) {
    cc_skip_space(lx);
    t->line = lx->line;
    t->num = 0;
    t->text = 0;
    t->op = OP_NONE;
    char c = *lx->p;
    if (!c) { t->kind = TO_EOF; return 1; }

    if (cc_is_ident_start(c)) {
        const char *start = lx->p;
        while (cc_is_ident(*lx->p)) lx->p++;
        int32_t length = (int32_t)(lx->p - start);
        int kw = cc_keyword(start, length);
        t->kind = kw ? TO_KW : TO_IDENT;
        t->op = (uint8_t)kw;
        t->text = cc_intern(start, length);
        if (!t->text[0] && length > 0) return 0;
        return 1;
    }

    if (cc_is_digit(c)) {
        int64_t value = 0;
        if (c == '0' && (lx->p[1] == 'x' || lx->p[1] == 'X')) {
            lx->p += 2;
            while (cc_hex_value(*lx->p) >= 0) {
                value = value * 16 + cc_hex_value(*lx->p);
                lx->p++;
            }
        } else {
            while (cc_is_digit(*lx->p)) {
                value = value * 10 + (*lx->p - '0');
                lx->p++;
            }
        }
        t->kind = TO_NUM;
        t->num = value;
        return 1;
    }

    if (c == '\'') {
        lx->p++;
        int value;
        if (*lx->p == '\\') { lx->p++; value = cc_read_escape(&lx->p); }
        else value = (unsigned char)*lx->p++;
        if (*lx->p == '\'') lx->p++;
        t->kind = TO_CHAR;
        t->num = value;
        return 1;
    }

    if (c == '"') {
        lx->p++;
        uint8_t bytes[512];
        int32_t used = 0;
        while (*lx->p && *lx->p != '"' && used < 510) {
            int value;
            if (*lx->p == '\\') { lx->p++; value = cc_read_escape(&lx->p); }
            else value = (unsigned char)*lx->p++;
            bytes[used++] = (uint8_t)value;
        }
        if (*lx->p == '"') lx->p++;
        bytes[used++] = 0;
        int32_t offset = cc_add_data(bytes, used);
        if (offset < 0) return 0;
        t->kind = TO_STR;
        t->num = offset;
        return 1;
    }

    static const struct { const char *text; uint8_t op; } puncts[] = {
        {"<<=", OP_SHL_ASSIGN}, {">>=", OP_SHR_ASSIGN},
        {"==", OP_EQ}, {"!=", OP_NE}, {"<=", OP_LE}, {">=", OP_GE},
        {"&&", OP_ANDAND}, {"||", OP_OROR}, {"<<", OP_SHL}, {">>", OP_SHR},
        {"++", OP_INC}, {"--", OP_DEC}, {"+=", OP_ADD_ASSIGN},
        {"-=", OP_SUB_ASSIGN}, {"*=", OP_MUL_ASSIGN}, {"/=", OP_DIV_ASSIGN},
        {"%=", OP_MOD_ASSIGN}, {"&=", OP_AND_ASSIGN}, {"|=", OP_OR_ASSIGN},
        {"^=", OP_XOR_ASSIGN}, {"(", OP_LPAREN}, {")", OP_RPAREN},
        {"{", OP_LBRACE}, {"}", OP_RBRACE}, {"[", OP_LBRACK},
        {"]", OP_RBRACK}, {";", OP_SEMI}, {",", OP_COMMA}, {"+", OP_PLUS},
        {"-", OP_MINUS}, {"*", OP_STAR}, {"/", OP_SLASH}, {"%", OP_PERCENT},
        {"&", OP_AMP}, {"|", OP_PIPE}, {"^", OP_CARET}, {"~", OP_TILDE},
        {"!", OP_BANG}, {"<", OP_LT}, {">", OP_GT}, {"=", OP_ASSIGN},
        {"?", OP_QUESTION}, {":", OP_COLON}, {".", OP_DOT}
    };
    for (int i = 0; i < (int)(sizeof(puncts) / sizeof(puncts[0])); i++) {
        int32_t n = cc_namelen(puncts[i].text);
        int match = 1;
        for (int32_t j = 0; j < n; j++) {
            if (lx->p[j] != puncts[i].text[j]) { match = 0; break; }
        }
        if (match) {
            t->kind = TO_OP;
            t->op = puncts[i].op;
            lx->p += n;
            return 1;
        }
    }
    cc_set_error(lx->line, "unexpected character");
    return 0;
}

static struct cc_node *cc_new_node(int kind, int32_t line) {
    if (cc_node_used >= CC_MAX_NODES) {
        cc_set_error(line, "program has too many syntax nodes");
        return 0;
    }
    struct cc_node *n = &cc_nodes[cc_node_used++];
    n->kind = (uint8_t)kind;
    n->op = 0;
    n->line = line;
    n->num = 0;
    n->name = 0;
    n->sym = -1;
    n->argc = 0;
    n->builtin = BI_NONE;
    n->a = n->b = n->c = n->d = 0;
    n->next = 0;
    return n;
}

static int cc_advance(struct cc_parser *p) {
    return cc_next_token(&p->lx, &p->tok);
}
static int cc_is_op(struct cc_parser *p, int op) {
    return p->tok.kind == TO_OP && p->tok.op == op;
}
static int cc_is_kw(struct cc_parser *p, int kw) {
    return p->tok.kind == TO_KW && p->tok.op == kw;
}
static int cc_eat_op(struct cc_parser *p, int op) {
    if (!cc_is_op(p, op)) return 0;
    return cc_advance(p);
}
static int cc_expect_op(struct cc_parser *p, int op) {
    if (cc_eat_op(p, op)) return 1;
    cc_set_error(p->tok.line, "expected punctuation");
    return 0;
}
static const char *cc_expect_ident(struct cc_parser *p) {
    if (p->tok.kind != TO_IDENT) {
        cc_set_error(p->tok.line, "expected an identifier");
        return 0;
    }
    const char *name = p->tok.text;
    if (!cc_advance(p)) return 0;
    return name;
}

static int cc_type_start(struct cc_parser *p) {
    if (p->tok.kind != TO_KW) return 0;
    switch (p->tok.op) {
        case K_INT: case K_CHAR: case K_VOID: case K_UNSIGNED:
        case K_SIGNED: case K_CONST: case K_STATIC: case K_EXTERN:
            return 1;
        default: return 0;
    }
}

static int cc_parse_type(struct cc_parser *p) {
    int saw_base = 0;
    for (;;) {
        if (p->tok.kind == TO_KW &&
            (p->tok.op == K_CONST || p->tok.op == K_STATIC ||
             p->tok.op == K_EXTERN || p->tok.op == K_UNSIGNED ||
             p->tok.op == K_SIGNED)) {
            if (!cc_advance(p)) return 0;
            continue;
        }
        if (p->tok.kind == TO_KW &&
            (p->tok.op == K_INT || p->tok.op == K_CHAR || p->tok.op == K_VOID)) {
            saw_base = 1;
            if (!cc_advance(p)) return 0;
            continue;
        }
        break;
    }
    if (!saw_base) {
        cc_set_error(p->tok.line, "expected int, char, or void");
        return 0;
    }
    while (cc_is_op(p, OP_STAR)) {
        if (!cc_advance(p)) return 0;
    }
    return 1;
}

static struct cc_node *cc_parse_expr(struct cc_parser *p);
static struct cc_node *cc_parse_assign(struct cc_parser *p);
static struct cc_node *cc_parse_statement(struct cc_parser *p);
static struct cc_node *cc_parse_local_decl(struct cc_parser *p);
static struct cc_node *cc_parse_block(struct cc_parser *p);

static struct cc_sym *cc_find_global(const char *name) {
    for (int32_t i = 0; i < cc_sym_used; i++)
        if (cc_syms[i].global && cc_syms[i].active && cc_streq(cc_syms[i].name, name))
            return &cc_syms[i];
    return 0;
}

static struct cc_sym *cc_find_local(const char *name) {
    for (int32_t i = cc_sym_used - 1; i >= 0; i--)
        if (!cc_syms[i].global && cc_syms[i].active &&
            cc_syms[i].func == cc_active_parser->current_func &&
            cc_streq(cc_syms[i].name, name))
            return &cc_syms[i];
    return 0;
}

static struct cc_sym *cc_add_sym(const char *name, int32_t offset, int global,
                                 int is_param) {
    if (cc_sym_used >= CC_MAX_SYMS) {
        cc_set_error(0, "too many variables");
        return 0;
    }
    struct cc_sym *s = &cc_syms[cc_sym_used++];
    s->name = name;
    s->offset = offset;
    s->func = global ? -1 : cc_active_parser->current_func;
    s->global = (uint8_t)(global != 0);
    s->active = 1;
    s->is_param = (uint8_t)(is_param != 0);
    return s;
}

static struct cc_func *cc_find_func(const char *name) {
    for (int32_t i = 0; i < cc_func_used; i++)
        if (cc_streq(cc_funcs[i].name, name)) return &cc_funcs[i];
    return 0;
}

static struct cc_func *cc_add_func(const char *name) {
    struct cc_func *f = cc_find_func(name);
    if (f) return f;
    if (cc_func_used >= CC_MAX_FUNCS) {
        cc_set_error(0, "too many functions");
        return 0;
    }
    f = &cc_funcs[cc_func_used++];
    f->name = name;
    f->nparams = 0;
    f->body = 0;
    f->frame_size = 0;
    f->offset = -1;
    f->defined = 0;
    return f;
}

static struct cc_node *cc_parse_call(struct cc_parser *p, const char *name) {
    int32_t line = p->tok.line;
    if (!cc_expect_op(p, OP_LPAREN)) return 0;
    int32_t count = 0;
    struct cc_node *first = 0;
    struct cc_node *last = 0;
    if (!cc_is_op(p, OP_RPAREN)) {
        for (;;) {
            struct cc_node *arg = cc_parse_assign(p);
            if (!arg) return 0;
            if (!first) first = arg; else last->next = arg;
            last = arg;
            count++;
            if (count > CC_MAX_ARGS) {
                cc_set_error(p->tok.line, "more than six call arguments");
                return 0;
            }
            if (!cc_eat_op(p, OP_COMMA)) break;
        }
    }
    if (!cc_expect_op(p, OP_RPAREN)) return 0;
    struct cc_node *n = cc_new_node(N_CALL, line);
    if (!n) return 0;
    n->name = name;
    n->a = first;
    n->argc = count;
    if (cc_streq(name, "printf")) n->builtin = BI_PRINTF;
    else if (cc_streq(name, "puts")) n->builtin = BI_PUTS;
    else if (cc_streq(name, "putchar")) n->builtin = BI_PUTCHAR;
    return n;
}

static struct cc_node *cc_parse_primary(struct cc_parser *p) {
    int32_t line = p->tok.line;
    if (p->tok.kind == TO_NUM || p->tok.kind == TO_CHAR) {
        int64_t value = p->tok.num;
        if (!cc_advance(p)) return 0;
        struct cc_node *n = cc_new_node(N_NUM, line);
        if (!n) return 0;
        n->num = value;
        return n;
    }
    if (p->tok.kind == TO_STR) {
        int64_t offset = p->tok.num;
        if (!cc_advance(p)) return 0;
        struct cc_node *n = cc_new_node(N_STR, line);
        if (!n) return 0;
        n->num = offset;
        return n;
    }
    if (p->tok.kind == TO_IDENT) {
        const char *name = p->tok.text;
        if (!cc_advance(p)) return 0;
        if (cc_is_op(p, OP_LPAREN)) return cc_parse_call(p, name);
        struct cc_sym *s = cc_find_local(name);
        if (!s) s = cc_find_global(name);
        if (!s) {
            cc_set_error(line, "undefined variable");
            return 0;
        }
        struct cc_node *n = cc_new_node(N_VAR, line);
        if (!n) return 0;
        n->sym = (int32_t)(s - cc_syms);
        n->name = name;
        return n;
    }
    if (cc_is_op(p, OP_LPAREN)) {
        if (!cc_advance(p)) return 0;
        struct cc_node *e = cc_parse_expr(p);
        if (!e) return 0;
        if (!cc_expect_op(p, OP_RPAREN)) return 0;
        return e;
    }
    cc_set_error(line, "expected an expression");
    return 0;
}

static struct cc_node *cc_parse_postfix(struct cc_parser *p) {
    struct cc_node *e = cc_parse_primary(p);
    if (!e) return 0;
    for (;;) {
        if (cc_is_op(p, OP_INC) || cc_is_op(p, OP_DEC)) {
            int op = p->tok.op;
            int32_t line = p->tok.line;
            if (e->kind != N_VAR) {
                cc_set_error(line, "++/-- needs a variable");
                return 0;
            }
            if (!cc_advance(p)) return 0;
            struct cc_node *n = cc_new_node(N_POST, line);
            if (!n) return 0;
            n->op = (uint8_t)op;
            n->a = e;
            e = n;
            continue;
        }
        return e;
    }
}

static struct cc_node *cc_parse_unary(struct cc_parser *p) {
    int32_t line = p->tok.line;
    if (cc_is_op(p, OP_PLUS) || cc_is_op(p, OP_MINUS) ||
        cc_is_op(p, OP_BANG) || cc_is_op(p, OP_TILDE)) {
        int op = p->tok.op;
        if (!cc_advance(p)) return 0;
        struct cc_node *operand = cc_parse_unary(p);
        if (!operand) return 0;
        struct cc_node *n = cc_new_node(N_UN, line);
        if (!n) return 0;
        n->op = (uint8_t)op;
        n->a = operand;
        return n;
    }
    if (cc_is_op(p, OP_INC) || cc_is_op(p, OP_DEC)) {
        int op = p->tok.op;
        if (!cc_advance(p)) return 0;
        struct cc_node *operand = cc_parse_unary(p);
        if (!operand) return 0;
        if (operand->kind != N_VAR) {
            cc_set_error(line, "++/-- needs a variable");
            return 0;
        }
        struct cc_node *n = cc_new_node(N_PRE, line);
        if (!n) return 0;
        n->op = (uint8_t)op;
        n->a = operand;
        return n;
    }
    return cc_parse_postfix(p);
}

static int cc_bin_prec(int op) {
    switch (op) {
        case OP_OROR: return 1;
        case OP_ANDAND: return 2;
        case OP_PIPE: return 3;
        case OP_CARET: return 4;
        case OP_AMP: return 5;
        case OP_EQ: case OP_NE: return 6;
        case OP_LT: case OP_GT: case OP_LE: case OP_GE: return 7;
        case OP_SHL: case OP_SHR: return 8;
        case OP_PLUS: case OP_MINUS: return 9;
        case OP_STAR: case OP_SLASH: case OP_PERCENT: return 10;
        default: return 0;
    }
}

static struct cc_node *cc_parse_binary(struct cc_parser *p, int min_prec) {
    struct cc_node *left = cc_parse_unary(p);
    if (!left) return 0;
    for (;;) {
        if (p->tok.kind != TO_OP) return left;
        int prec = cc_bin_prec(p->tok.op);
        if (prec == 0 || prec < min_prec) return left;
        int op = p->tok.op;
        int32_t line = p->tok.line;
        if (!cc_advance(p)) return 0;
        struct cc_node *right = cc_parse_binary(p, prec + 1);
        if (!right) return 0;
        struct cc_node *n = cc_new_node(N_BIN, line);
        if (!n) return 0;
        n->op = (uint8_t)op;
        n->a = left;
        n->b = right;
        left = n;
    }
}

static struct cc_node *cc_parse_conditional(struct cc_parser *p) {
    struct cc_node *cond = cc_parse_binary(p, 1);
    if (!cond) return 0;
    if (!cc_is_op(p, OP_QUESTION)) return cond;
    int32_t line = p->tok.line;
    if (!cc_advance(p)) return 0;
    struct cc_node *yes = cc_parse_expr(p);
    if (!yes) return 0;
    if (!cc_expect_op(p, OP_COLON)) return 0;
    struct cc_node *no = cc_parse_conditional(p);
    if (!no) return 0;
    struct cc_node *n = cc_new_node(N_COND, line);
    if (!n) return 0;
    n->a = cond; n->b = yes; n->c = no;
    return n;
}

static struct cc_node *cc_parse_assign(struct cc_parser *p) {
    struct cc_node *left = cc_parse_conditional(p);
    if (!left) return 0;
    if (p->tok.kind != TO_OP) return left;
    int op = p->tok.op;
    if (op != OP_ASSIGN && op != OP_ADD_ASSIGN && op != OP_SUB_ASSIGN &&
        op != OP_MUL_ASSIGN && op != OP_DIV_ASSIGN && op != OP_MOD_ASSIGN &&
        op != OP_AND_ASSIGN && op != OP_OR_ASSIGN && op != OP_XOR_ASSIGN &&
        op != OP_SHL_ASSIGN && op != OP_SHR_ASSIGN)
        return left;
    if (left->kind != N_VAR) {
        cc_set_error(p->tok.line, "assignment target must be a variable");
        return 0;
    }
    int32_t line = p->tok.line;
    if (!cc_advance(p)) return 0;
    struct cc_node *right = cc_parse_assign(p);
    if (!right) return 0;
    struct cc_node *n = cc_new_node(N_ASSIGN, line);
    if (!n) return 0;
    n->op = (uint8_t)op;
    n->a = left;
    n->b = right;
    return n;
}

static struct cc_node *cc_parse_expr(struct cc_parser *p) {
    struct cc_node *e = cc_parse_assign(p);
    if (!e) return 0;
    while (cc_is_op(p, OP_COMMA)) {
        int32_t line = p->tok.line;
        if (!cc_advance(p)) return 0;
        struct cc_node *r = cc_parse_assign(p);
        if (!r) return 0;
        struct cc_node *n = cc_new_node(N_COMMA, line);
        if (!n) return 0;
        n->a = e; n->b = r;
        e = n;
    }
    return e;
}

static int cc_is_type_start(struct cc_parser *p) {
    return cc_type_start(p);
}

static struct cc_node *cc_parse_local_decl(struct cc_parser *p) {
    if (!cc_parse_type(p)) return 0;
    struct cc_node *first = 0;
    struct cc_node *last = 0;
    for (;;) {
        const char *name = cc_expect_ident(p);
        if (!name) return 0;
        int32_t offset = cc_active_parser->frame_cursor + 8;
        cc_active_parser->frame_cursor = offset;
        struct cc_sym *s = cc_add_sym(name, offset, 0, 0);
        if (!s) return 0;
        struct cc_node *init = 0;
        if (cc_eat_op(p, OP_ASSIGN)) {
            init = cc_parse_assign(p);
            if (!init) return 0;
        }
        struct cc_node *decl = cc_new_node(N_DECL, p->tok.line);
        if (!decl) return 0;
        decl->sym = (int32_t)(s - cc_syms);
        decl->a = init;
        if (!first) first = decl; else last->next = decl;
        last = decl;
        if (!cc_eat_op(p, OP_COMMA)) break;
    }
    if (!cc_expect_op(p, OP_SEMI)) return 0;
    return first;
}

static struct cc_node *cc_parse_block(struct cc_parser *p) {
    int32_t mark = cc_sym_used;
    if (!cc_expect_op(p, OP_LBRACE)) return 0;
    struct cc_node *first = 0;
    struct cc_node *last = 0;
    while (!cc_is_op(p, OP_RBRACE)) {
        if (p->tok.kind == TO_EOF) {
            cc_set_error(p->tok.line, "unterminated block");
            return 0;
        }
        struct cc_node *st = cc_parse_statement(p);
        if (!st) return 0;
        if (st->kind != N_EMPTY || !first) {
            if (!first) first = st; else last->next = st;
            last = st;
        }
    }
    if (!cc_advance(p)) return 0;
    for (int32_t i = mark; i < cc_sym_used; i++) cc_syms[i].active = 0;
    struct cc_node *block = cc_new_node(N_BLOCK, p->tok.line);
    if (!block) return 0;
    block->a = first;
    return block;
}

static struct cc_node *cc_parse_for(struct cc_parser *p) {
    int32_t line = p->tok.line;
    if (!cc_advance(p) || !cc_expect_op(p, OP_LPAREN)) return 0;
    int32_t mark = cc_sym_used;
    struct cc_node *init = 0;
    if (cc_is_type_start(p)) {
        init = cc_parse_local_decl(p);
        if (!init) return 0;
    } else {
        if (!cc_is_op(p, OP_SEMI)) {
            init = cc_new_node(N_EXPR, p->tok.line);
            if (!init) return 0;
            init->a = cc_parse_expr(p);
            if (!init->a) return 0;
        }
        if (!cc_expect_op(p, OP_SEMI)) return 0;
    }
    struct cc_node *cond = 0;
    if (!cc_is_op(p, OP_SEMI)) {
        cond = cc_parse_expr(p);
        if (!cond) return 0;
    }
    if (!cc_expect_op(p, OP_SEMI)) return 0;
    struct cc_node *step = 0;
    if (!cc_is_op(p, OP_RPAREN)) {
        step = cc_parse_expr(p);
        if (!step) return 0;
    }
    if (!cc_expect_op(p, OP_RPAREN)) return 0;
    struct cc_node *body = cc_parse_statement(p);
    if (!body) return 0;
    for (int32_t i = mark; i < cc_sym_used; i++) cc_syms[i].active = 0;
    struct cc_node *n = cc_new_node(N_FOR, line);
    if (!n) return 0;
    n->a = init; n->b = cond; n->c = step; n->d = body;
    return n;
}

static struct cc_node *cc_parse_statement(struct cc_parser *p) {
    int32_t line = p->tok.line;
    if (cc_is_op(p, OP_LBRACE)) return cc_parse_block(p);
    if (cc_is_op(p, OP_SEMI)) {
        if (!cc_advance(p)) return 0;
        return cc_new_node(N_EMPTY, line);
    }
    if (cc_is_kw(p, K_IF)) {
        if (!cc_advance(p) || !cc_expect_op(p, OP_LPAREN)) return 0;
        struct cc_node *cond = cc_parse_expr(p);
        if (!cond) return 0;
        if (!cc_expect_op(p, OP_RPAREN)) return 0;
        struct cc_node *yes = cc_parse_statement(p);
        if (!yes) return 0;
        struct cc_node *no = 0;
        if (cc_is_kw(p, K_ELSE)) {
            if (!cc_advance(p)) return 0;
            no = cc_parse_statement(p);
            if (!no) return 0;
        }
        struct cc_node *n = cc_new_node(N_IF, line);
        if (!n) return 0;
        n->a = cond; n->b = yes; n->c = no;
        return n;
    }
    if (cc_is_kw(p, K_WHILE)) {
        if (!cc_advance(p) || !cc_expect_op(p, OP_LPAREN)) return 0;
        struct cc_node *cond = cc_parse_expr(p);
        if (!cond) return 0;
        if (!cc_expect_op(p, OP_RPAREN)) return 0;
        struct cc_node *body = cc_parse_statement(p);
        if (!body) return 0;
        struct cc_node *n = cc_new_node(N_WHILE, line);
        if (!n) return 0;
        n->a = cond; n->b = body;
        return n;
    }
    if (cc_is_kw(p, K_DO)) {
        if (!cc_advance(p)) return 0;
        struct cc_node *body = cc_parse_statement(p);
        if (!body) return 0;
        if (!cc_is_kw(p, K_WHILE) || !cc_advance(p) ||
            !cc_expect_op(p, OP_LPAREN)) return 0;
        struct cc_node *cond = cc_parse_expr(p);
        if (!cond) return 0;
        if (!cc_expect_op(p, OP_RPAREN) || !cc_expect_op(p, OP_SEMI)) return 0;
        struct cc_node *n = cc_new_node(N_DO, line);
        if (!n) return 0;
        n->a = body; n->b = cond;
        return n;
    }
    if (cc_is_kw(p, K_FOR)) return cc_parse_for(p);
    if (cc_is_kw(p, K_RETURN)) {
        if (!cc_advance(p)) return 0;
        struct cc_node *n = cc_new_node(N_RETURN, line);
        if (!n) return 0;
        if (!cc_is_op(p, OP_SEMI)) {
            n->a = cc_parse_expr(p);
            if (!n->a) return 0;
        }
        if (!cc_expect_op(p, OP_SEMI)) return 0;
        return n;
    }
    if (cc_is_kw(p, K_BREAK)) {
        if (!cc_advance(p) || !cc_expect_op(p, OP_SEMI)) return 0;
        return cc_new_node(N_BREAK, line);
    }
    if (cc_is_kw(p, K_CONTINUE)) {
        if (!cc_advance(p) || !cc_expect_op(p, OP_SEMI)) return 0;
        return cc_new_node(N_CONTINUE, line);
    }
    if (cc_is_type_start(p)) return cc_parse_local_decl(p);
    struct cc_node *expr = cc_parse_expr(p);
    if (!expr) return 0;
    if (!cc_expect_op(p, OP_SEMI)) return 0;
    struct cc_node *n = cc_new_node(N_EXPR, line);
    if (!n) return 0;
    n->a = expr;
    return n;
}

static int cc_parse_params(struct cc_parser *p, struct cc_func *f) {
    f->nparams = 0;
    if (!cc_expect_op(p, OP_LPAREN)) return 0;
    if (cc_is_op(p, OP_RPAREN)) {
        if (!cc_advance(p)) return 0;
        return 1;
    }
    if (cc_is_kw(p, K_VOID)) {
        if (!cc_advance(p)) return 0;
        if (cc_is_op(p, OP_RPAREN)) return cc_advance(p);
        cc_set_error(p->tok.line, "void must be the only parameter");
        return 0;
    }
    for (;;) {
        if (!cc_parse_type(p)) return 0;
        const char *name = cc_expect_ident(p);
        if (!name) return 0;
        int32_t offset = cc_active_parser->frame_cursor + 8;
        cc_active_parser->frame_cursor = offset;
        struct cc_sym *s = cc_add_sym(name, offset, 0, 1);
        if (!s) return 0;
        if (f->nparams >= CC_MAX_ARGS) {
            cc_set_error(p->tok.line, "more than six parameters");
            return 0;
        }
        f->param_syms[f->nparams++] = (int32_t)(s - cc_syms);
        if (!cc_eat_op(p, OP_COMMA)) break;
    }
    return cc_expect_op(p, OP_RPAREN);
}

static int cc_parse_function(struct cc_parser *p, struct cc_func *f) {
    cc_active_parser = p;
    int32_t saved_func = p->current_func;
    int32_t saved_cursor = p->frame_cursor;
    p->current_func = (int32_t)(f - cc_funcs);
    p->frame_cursor = 0;
    if (!cc_parse_params(p, f)) return 0;
    if (cc_is_op(p, OP_SEMI)) {
        if (!cc_advance(p)) return 0;
        p->current_func = saved_func;
        p->frame_cursor = saved_cursor;
        return 1;
    }
    if (!cc_is_op(p, OP_LBRACE)) {
        cc_set_error(p->tok.line, "function body must start with {");
        return 0;
    }
    f->body = cc_parse_statement(p);
    if (!f->body) return 0;
    f->frame_size = (p->frame_cursor + 15) & ~15;
    f->defined = 1;
    p->current_func = saved_func;
    p->frame_cursor = saved_cursor;
    return 1;
}

static int64_t cc_const_value(struct cc_node *n, int *ok) {
    if (!n) { *ok = 0; return 0; }
    if (n->kind == N_NUM) { *ok = 1; return n->num; }
    if (n->kind == N_STR) {
        *ok = 1;
        return (int64_t)(CC_DATA_BASE + (uintptr_t)n->num);
    }
    if (n->kind == N_UN) {
        int sub_ok = 0;
        int64_t v = cc_const_value(n->a, &sub_ok);
        if (!sub_ok) { *ok = 0; return 0; }
        *ok = 1;
        if (n->op == OP_MINUS) return -v;
        if (n->op == OP_TILDE) return ~v;
        if (n->op == OP_BANG) return !v;
        return v;
    }
    if (n->kind == N_BIN) {
        int a_ok = 0, b_ok = 0;
        int64_t a = cc_const_value(n->a, &a_ok);
        int64_t b = cc_const_value(n->b, &b_ok);
        if (!a_ok || !b_ok) { *ok = 0; return 0; }
        *ok = 1;
        switch (n->op) {
        case OP_PLUS: return a + b;
        case OP_MINUS: return a - b;
        case OP_STAR: return a * b;
        case OP_SLASH: if (!b) { *ok = 0; return 0; } return a / b;
        case OP_PERCENT: if (!b) { *ok = 0; return 0; } return a % b;
        case OP_AMP: return a & b;
        case OP_PIPE: return a | b;
        case OP_CARET: return a ^ b;
        case OP_SHL: if (b < 0 || b > 63) { *ok = 0; return 0; } return a << b;
        case OP_SHR: if (b < 0 || b > 63) { *ok = 0; return 0; } return a >> b;
        case OP_EQ: return a == b;
        case OP_NE: return a != b;
        case OP_LT: return a < b;
        case OP_LE: return a <= b;
        case OP_GT: return a > b;
        case OP_GE: return a >= b;
        case OP_ANDAND: return a && b;
        case OP_OROR: return a || b;
        default: *ok = 0; return 0;
        }
    }
    if (n->kind == N_COND) {
        int c_ok = 0, a_ok = 0, b_ok = 0;
        int64_t c = cc_const_value(n->a, &c_ok);
        int64_t a = cc_const_value(n->b, &a_ok);
        int64_t b = cc_const_value(n->c, &b_ok);
        if (!c_ok || !a_ok || !b_ok) { *ok = 0; return 0; }
        *ok = 1;
        return c ? a : b;
    }
    *ok = 0;
    return 0;
}

static int cc_parse_global(struct cc_parser *p) {
    if (!cc_parse_type(p)) return 0;
    const char *name = cc_expect_ident(p);
    if (!name) return 0;
    if (cc_is_op(p, OP_LPAREN)) {
        struct cc_func *f = cc_add_func(name);
        if (!f) return 0;
        if (!cc_parse_function(p, f)) return 0;
        return 1;
    }
    for (;;) {
        uint8_t zero[8] = {0};
        int32_t offset = cc_add_data(zero, 8);
        if (offset < 0) return 0;
        struct cc_sym *s = cc_add_sym(name, offset, 1, 0);
        if (!s) return 0;
        if (cc_eat_op(p, OP_ASSIGN)) {
            struct cc_node *init = cc_parse_assign(p);
            if (!init) return 0;
            int ok = 0;
            int64_t value = cc_const_value(init, &ok);
            if (!ok) {
                cc_set_error(init->line, "global initializer must be constant");
                return 0;
            }
            for (int i = 0; i < 8; i++) cc_data[offset + i] = (uint8_t)(value >> (i * 8));
        }
        if (!cc_eat_op(p, OP_COMMA)) break;
        name = cc_expect_ident(p);
        if (!name) return 0;
    }
    return cc_expect_op(p, OP_SEMI);
}

static int cc_parse_program(struct cc_parser *p) {
    while (p->tok.kind != TO_EOF) {
        if (!cc_is_type_start(p)) {
            cc_set_error(p->tok.line, p->tok.text ? p->tok.text :
                         "expected a top-level declaration");
            return 0;
        }
        if (!cc_parse_global(p)) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* x86-64 encoder                                                      */
/* ------------------------------------------------------------------ */

static void cc_emit8(uint8_t value) {
    if ((uint32_t)cc_code_used >= CC_MAX_CODE) {
        cc_set_error(0, "generated code exceeds 64 KiB");
        return;
    }
    cc_code[cc_code_used++] = value;
}
static void cc_emit32(uint32_t value) {
    cc_emit8((uint8_t)value);
    cc_emit8((uint8_t)(value >> 8));
    cc_emit8((uint8_t)(value >> 16));
    cc_emit8((uint8_t)(value >> 24));
}
static void cc_emit64(uint64_t value) {
    for (int i = 0; i < 8; i++) cc_emit8((uint8_t)(value >> (i * 8)));
}
static void cc_emit_mov_imm64(int reg, uint64_t value) {
    if (reg >= 8) cc_emit8(0x49);
    else cc_emit8(0x48);
    cc_emit8((uint8_t)(0xB8 + (reg & 7)));
    cc_emit64(value);
}
static void cc_emit_push(int reg) {
    if (reg >= 8) cc_emit8(0x41);
    cc_emit8((uint8_t)(0x50 + (reg & 7)));
}
static void cc_emit_pop(int reg) {
    if (reg >= 8) cc_emit8(0x41);
    cc_emit8((uint8_t)(0x58 + (reg & 7)));
}
static void cc_emit_call_rax(void) {
    cc_emit8(0xFF); cc_emit8(0xD0);
}
static void cc_emit_test_rax(void) {
    cc_emit8(0x48); cc_emit8(0x85); cc_emit8(0xC0);
}
static int32_t cc_emit_jmp_placeholder(void) {
    cc_emit8(0xE9);
    int32_t position = cc_code_used;
    cc_emit32(0);
    return position;
}
static int32_t cc_emit_jz_placeholder(void) {
    cc_emit8(0x0F); cc_emit8(0x84);
    int32_t position = cc_code_used;
    cc_emit32(0);
    return position;
}
static int32_t cc_emit_jnz_placeholder(void) {
    cc_emit8(0x0F); cc_emit8(0x85);
    int32_t position = cc_code_used;
    cc_emit32(0);
    return position;
}
static void cc_patch_rel32(int32_t position, int32_t target) {
    int32_t relative = target - (position + 4);
    cc_code[position] = (uint8_t)relative;
    cc_code[position + 1] = (uint8_t)((uint32_t)relative >> 8);
    cc_code[position + 2] = (uint8_t)((uint32_t)relative >> 16);
    cc_code[position + 3] = (uint8_t)((uint32_t)relative >> 24);
}
static void cc_emit_call_rel(int32_t function) {
    cc_emit8(0xE8);
    int32_t position = cc_code_used;
    cc_emit32(0);
    if (cc_fixup_used < CC_MAX_FIXUPS) {
        cc_fixups[cc_fixup_used].position = position;
        cc_fixups[cc_fixup_used].function = function;
        cc_fixup_used++;
    } else {
        cc_set_error(0, "too many function calls");
    }
}
static void cc_emit_load_local(int32_t offset) {
    cc_emit8(0x48); cc_emit8(0x8B); cc_emit8(0x85); cc_emit32((uint32_t)-offset);
}
static void cc_emit_store_local(int32_t offset) {
    cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0x85); cc_emit32((uint32_t)-offset);
}
static void cc_emit_store_local_reg(int32_t offset, int reg) {
    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x04;
    cc_emit8(rex);
    cc_emit8(0x89);
    cc_emit8((uint8_t)(0x85 | ((reg & 7) << 3)));
    cc_emit32((uint32_t)-offset);
}
static void cc_emit_load_global(int32_t offset) {
    cc_emit_mov_imm64(0, (uint64_t)(CC_DATA_BASE + (uintptr_t)offset));
    cc_emit8(0x48); cc_emit8(0x8B); cc_emit8(0x00);
}
static void cc_emit_store_global(int32_t offset) {
    cc_emit_mov_imm64(1, (uint64_t)(CC_DATA_BASE + (uintptr_t)offset));
    cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0x01);
}
static void cc_emit_load_var(int32_t sym) {
    if (cc_syms[sym].global) cc_emit_load_global(cc_syms[sym].offset);
    else cc_emit_load_local(cc_syms[sym].offset);
}
static void cc_emit_store_var(int32_t sym) {
    if (cc_syms[sym].global) cc_emit_store_global(cc_syms[sym].offset);
    else cc_emit_store_local(cc_syms[sym].offset);
}

static void cc_gen_expr(struct cc_node *n);
static void cc_gen_stmt(struct cc_node *n);

static void cc_gen_call_args(struct cc_node *arg) {
    int count = 0;
    while (arg) {
        cc_gen_expr(arg);
        cc_emit_push(0);
        count++;
        arg = arg->next;
    }
    static const int regs[CC_MAX_ARGS] = {7, 6, 2, 1, 8, 9};
    for (int i = count - 1; i >= 0; i--) cc_emit_pop(regs[i]);
}

static void cc_gen_binop(int op) {
    switch (op) {
        case OP_PLUS: cc_emit8(0x48); cc_emit8(0x01); cc_emit8(0xC8); break;
        case OP_MINUS:
            cc_emit8(0x48); cc_emit8(0x29); cc_emit8(0xC1);
            cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0xC8); break;
        case OP_STAR: cc_emit8(0x48); cc_emit8(0x0F); cc_emit8(0xAF); cc_emit8(0xC1); break;
        case OP_SLASH:
            cc_emit8(0x48); cc_emit8(0x91);
            cc_emit8(0x48); cc_emit8(0x99);
            cc_emit8(0x48); cc_emit8(0xF7); cc_emit8(0xF9); break;
        case OP_PERCENT:
            cc_emit8(0x48); cc_emit8(0x91);
            cc_emit8(0x48); cc_emit8(0x99);
            cc_emit8(0x48); cc_emit8(0xF7); cc_emit8(0xF9);
            cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0xD0); break;
        case OP_AMP: cc_emit8(0x48); cc_emit8(0x21); cc_emit8(0xC8); break;
        case OP_PIPE: cc_emit8(0x48); cc_emit8(0x09); cc_emit8(0xC8); break;
        case OP_CARET: cc_emit8(0x48); cc_emit8(0x31); cc_emit8(0xC8); break;
        case OP_SHL:
            cc_emit8(0x48); cc_emit8(0x91);
            cc_emit8(0x48); cc_emit8(0xD3); cc_emit8(0xE0); break;
        case OP_SHR:
            cc_emit8(0x48); cc_emit8(0x91);
            cc_emit8(0x48); cc_emit8(0xD3); cc_emit8(0xF8); break;
        case OP_EQ: case OP_NE: case OP_LT: case OP_GT: case OP_LE: case OP_GE: {
            cc_emit8(0x48); cc_emit8(0x39); cc_emit8(0xC1);
            uint8_t set = 0x94;
            if (op == OP_NE) set = 0x95;
            else if (op == OP_LT) set = 0x9C;
            else if (op == OP_GT) set = 0x9F;
            else if (op == OP_LE) set = 0x9E;
            else if (op == OP_GE) set = 0x9D;
            cc_emit8(0x0F); cc_emit8(set); cc_emit8(0xC0);
            cc_emit8(0x48); cc_emit8(0x0F); cc_emit8(0xB6); cc_emit8(0xC0);
            break;
        }
        default: break;
    }
}

static void cc_gen_expr(struct cc_node *n) {
    if (!n) { cc_emit_mov_imm64(0, 0); return; }
    switch (n->kind) {
        case N_NUM:
            cc_emit_mov_imm64(0, (uint64_t)n->num);
            break;
        case N_STR:
            cc_emit_mov_imm64(0, (uint64_t)(CC_DATA_BASE + (uintptr_t)n->num));
            break;
        case N_VAR:
            cc_emit_load_var(n->sym);
            break;
        case N_COMMA:
            cc_gen_expr(n->a);
            cc_gen_expr(n->b);
            break;
        case N_CALL: {
            cc_gen_call_args(n->a);
            if (n->builtin) {
                uintptr_t address = 0;
                if (n->builtin == BI_PRINTF) address = (uintptr_t)&cc_rt_printf;
                else if (n->builtin == BI_PUTS) address = (uintptr_t)&cc_rt_puts;
                else address = (uintptr_t)&cc_rt_putchar;
                cc_emit_mov_imm64(0, (uint64_t)address);
                cc_emit_call_rax();
            } else {
                struct cc_func *f = cc_find_func(n->name);
                if (!f) { cc_set_error(n->line, "call to undefined function"); return; }
                if (!f->defined) { cc_set_error(n->line, "call to a function without a body"); return; }
                cc_emit_call_rel((int32_t)(f - cc_funcs));
            }
            break;
        }
        case N_BIN:
            if (n->op == OP_ANDAND) {
                cc_emit_test_rax();
                int32_t false_label = cc_emit_jz_placeholder();
                cc_gen_expr(n->b);
                cc_emit_test_rax();
                int32_t false_label2 = cc_emit_jz_placeholder();
                cc_emit_mov_imm64(0, 1);
                int32_t end_label = cc_emit_jmp_placeholder();
                cc_patch_rel32(false_label, cc_code_used);
                cc_patch_rel32(false_label2, cc_code_used);
                cc_emit_mov_imm64(0, 0);
                cc_patch_rel32(end_label, cc_code_used);
            } else if (n->op == OP_OROR) {
                cc_emit_test_rax();
                int32_t true_label = cc_emit_jnz_placeholder();
                cc_gen_expr(n->b);
                cc_emit_test_rax();
                int32_t true_label2 = cc_emit_jnz_placeholder();
                cc_emit_mov_imm64(0, 0);
                int32_t end_label = cc_emit_jmp_placeholder();
                cc_patch_rel32(true_label, cc_code_used);
                cc_patch_rel32(true_label2, cc_code_used);
                cc_emit_mov_imm64(0, 1);
                cc_patch_rel32(end_label, cc_code_used);
            } else {
                cc_gen_expr(n->a);
                cc_emit_push(0);
                cc_gen_expr(n->b);
                cc_emit_pop(1);
                cc_gen_binop(n->op);
            }
            break;
        case N_UN:
            cc_gen_expr(n->a);
            if (n->op == OP_MINUS) {
                cc_emit8(0x48); cc_emit8(0xF7); cc_emit8(0xD8);
            } else if (n->op == OP_TILDE) {
                cc_emit8(0x48); cc_emit8(0xF7); cc_emit8(0xD0);
            } else if (n->op == OP_BANG) {
                cc_emit8(0x48); cc_emit8(0x85); cc_emit8(0xC0);
                cc_emit8(0x0F); cc_emit8(0x94); cc_emit8(0xC0);
                cc_emit8(0x48); cc_emit8(0x0F); cc_emit8(0xB6); cc_emit8(0xC0);
            }
            break;
        case N_ASSIGN:
            if (n->op == OP_ASSIGN) {
                cc_gen_expr(n->b);
            } else {
                cc_emit_load_var(n->a->sym);
                cc_emit_push(0);
                cc_gen_expr(n->b);
                cc_emit_pop(1);
                int op = OP_PLUS;
                if (n->op == OP_SUB_ASSIGN) op = OP_MINUS;
                else if (n->op == OP_MUL_ASSIGN) op = OP_STAR;
                else if (n->op == OP_DIV_ASSIGN) op = OP_SLASH;
                else if (n->op == OP_MOD_ASSIGN) op = OP_PERCENT;
                else if (n->op == OP_AND_ASSIGN) op = OP_AMP;
                else if (n->op == OP_OR_ASSIGN) op = OP_PIPE;
                else if (n->op == OP_XOR_ASSIGN) op = OP_CARET;
                else if (n->op == OP_SHL_ASSIGN) op = OP_SHL;
                else if (n->op == OP_SHR_ASSIGN) op = OP_SHR;
                cc_gen_binop(op);
            }
            cc_emit_store_var(n->a->sym);
            break;
        case N_PRE:
            cc_emit_load_var(n->a->sym);
            cc_emit8(0x48); cc_emit8(n->op == OP_INC ? 0x83 : 0x83);
            cc_emit8(n->op == OP_INC ? 0xC0 : 0xE8); cc_emit8(1);
            cc_emit_store_var(n->a->sym);
            break;
        case N_POST:
            cc_emit_load_var(n->a->sym);
            cc_emit_push(0);
            cc_emit8(0x48); cc_emit8(n->op == OP_INC ? 0x83 : 0x83);
            cc_emit8(n->op == OP_INC ? 0xC0 : 0xE8); cc_emit8(1);
            cc_emit_store_var(n->a->sym);
            cc_emit_pop(0);
            break;
        case N_COND: {
            cc_gen_expr(n->a);
            cc_emit_test_rax();
            int32_t else_label = cc_emit_jz_placeholder();
            cc_gen_expr(n->b);
            int32_t end_label = cc_emit_jmp_placeholder();
            cc_patch_rel32(else_label, cc_code_used);
            cc_gen_expr(n->c);
            cc_patch_rel32(end_label, cc_code_used);
            break;
        }
        default:
            cc_emit_mov_imm64(0, 0);
            break;
    }
}

static void cc_gen_stmt_list(struct cc_node *n) {
    while (n) {
        cc_gen_stmt(n);
        n = n->next;
    }
}

static void cc_gen_stmt(struct cc_node *n) {
    if (!n) return;
    switch (n->kind) {
        case N_BLOCK: cc_gen_stmt_list(n->a); break;
        case N_DECL:
            if (n->a) {
                cc_gen_expr(n->a);
                cc_emit_store_var(n->sym);
            }
            break;
        case N_EXPR: cc_gen_expr(n->a); break;
        case N_IF: {
            cc_gen_expr(n->a);
            cc_emit_test_rax();
            int32_t else_label = cc_emit_jz_placeholder();
            cc_gen_stmt(n->b);
            if (n->c) {
                int32_t end_label = cc_emit_jmp_placeholder();
                cc_patch_rel32(else_label, cc_code_used);
                cc_gen_stmt(n->c);
                cc_patch_rel32(end_label, cc_code_used);
            } else {
                cc_patch_rel32(else_label, cc_code_used);
            }
            break;
        }
        case N_WHILE: {
            if (cc_loop_sp >= CC_MAX_LOOPS) {
                cc_set_error(n->line, "loops are nested too deeply");
                return;
            }
            struct cc_loop_ctx *loop = &cc_loops[cc_loop_sp++];
            loop->break_count = loop->continue_count = 0;
            int32_t cond = cc_code_used;
            cc_gen_expr(n->a);
            cc_emit_test_rax();
            int32_t end_jump = cc_emit_jz_placeholder();
            loop->continue_target = cond;
            cc_gen_stmt(n->b);
            int32_t back = cc_emit_jmp_placeholder();
            cc_patch_rel32(back, cond);
            loop->break_target = cc_code_used;
            cc_patch_rel32(end_jump, loop->break_target);
            for (int i = 0; i < loop->break_count; i++)
                cc_patch_rel32(loop->breaks[i], loop->break_target);
            for (int i = 0; i < loop->continue_count; i++)
                cc_patch_rel32(loop->continues[i], loop->continue_target);
            cc_loop_sp--;
            break;
        }
        case N_DO: {
            if (cc_loop_sp >= CC_MAX_LOOPS) {
                cc_set_error(n->line, "loops are nested too deeply");
                return;
            }
            struct cc_loop_ctx *loop = &cc_loops[cc_loop_sp++];
            loop->break_count = loop->continue_count = 0;
            int32_t start = cc_code_used;
            loop->continue_target = start;
            cc_gen_stmt(n->a);
            int32_t cond = cc_code_used;
            cc_gen_expr(n->b);
            cc_emit_test_rax();
            int32_t back = cc_emit_jnz_placeholder();
            cc_patch_rel32(back, start);
            loop->break_target = cc_code_used;
            for (int i = 0; i < loop->break_count; i++)
                cc_patch_rel32(loop->breaks[i], loop->break_target);
            for (int i = 0; i < loop->continue_count; i++)
                cc_patch_rel32(loop->continues[i], cond);
            cc_loop_sp--;
            break;
        }
        case N_FOR: {
            if (cc_loop_sp >= CC_MAX_LOOPS) {
                cc_set_error(n->line, "loops are nested too deeply");
                return;
            }
            if (n->a) cc_gen_stmt(n->a);
            struct cc_loop_ctx *loop = &cc_loops[cc_loop_sp++];
            loop->break_count = loop->continue_count = 0;
            int32_t cond = cc_code_used;
            if (n->b) cc_gen_expr(n->b); else cc_emit_mov_imm64(0, 1);
            cc_emit_test_rax();
            int32_t end_jump = cc_emit_jz_placeholder();
            cc_gen_stmt(n->d);
            int32_t step = cc_code_used;
            loop->continue_target = step;
            if (n->c) cc_gen_expr(n->c);
            int32_t back = cc_emit_jmp_placeholder();
            cc_patch_rel32(back, cond);
            loop->break_target = cc_code_used;
            cc_patch_rel32(end_jump, loop->break_target);
            for (int i = 0; i < loop->break_count; i++)
                cc_patch_rel32(loop->breaks[i], loop->break_target);
            for (int i = 0; i < loop->continue_count; i++)
                cc_patch_rel32(loop->continues[i], loop->continue_target);
            cc_loop_sp--;
            break;
        }
        case N_RETURN:
            if (n->a) cc_gen_expr(n->a); else cc_emit_mov_imm64(0, 0);
            cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0xEC);
            cc_emit8(0x5D);
            cc_emit8(0xC3);
            break;
        case N_BREAK:
            if (cc_loop_sp <= 0) { cc_set_error(n->line, "break outside loop"); return; }
            {
                struct cc_loop_ctx *loop = &cc_loops[cc_loop_sp - 1];
                if (loop->break_count < CC_MAX_LOOP_JUMPS)
                    loop->breaks[loop->break_count++] = cc_emit_jmp_placeholder();
                else cc_set_error(n->line, "too many break statements");
            }
            break;
        case N_CONTINUE:
            if (cc_loop_sp <= 0) { cc_set_error(n->line, "continue outside loop"); return; }
            {
                struct cc_loop_ctx *loop = &cc_loops[cc_loop_sp - 1];
                if (loop->continue_count < CC_MAX_LOOP_JUMPS)
                    loop->continues[loop->continue_count++] = cc_emit_jmp_placeholder();
                else cc_set_error(n->line, "too many continue statements");
            }
            break;
        case N_EMPTY: break;
        default: break;
    }
}

static void cc_gen_func(struct cc_func *f) {
    if (!f || !f->defined) return;
    f->offset = cc_code_used;
    cc_emit8(0x55);
    cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0xE5);
    if (f->frame_size > 0) {
        cc_emit8(0x48); cc_emit8(0x81); cc_emit8(0xEC);
        cc_emit32((uint32_t)f->frame_size);
    }
    static const int regs[CC_MAX_ARGS] = {7, 6, 2, 1, 8, 9};
    for (int i = 0; i < f->nparams; i++)
        cc_emit_store_local_reg(cc_syms[f->param_syms[i]].offset, regs[i]);
    cc_gen_stmt(f->body);
    cc_emit_mov_imm64(0, 0);
    cc_emit8(0x48); cc_emit8(0x89); cc_emit8(0xEC);
    cc_emit8(0x5D);
    cc_emit8(0xC3);
}

/* ------------------------------------------------------------------ */
/* Runtime output                                                      */
/* ------------------------------------------------------------------ */

extern void cc_output_char(char c);

void cc_rt_putchar(int c) {
    cc_output_char((char)c);
}

void cc_rt_puts(const char *text) {
    if (!text) text = "(null)";
    while (*text) cc_output_char(*text++);
    cc_output_char('\n');
}

static void cc_print_unsigned(uint64_t value, int base, int width, char pad) {
    char buffer[32];
    int count = 0;
    if (value == 0) buffer[count++] = '0';
    while (value > 0 && count < 31) {
        int digit = (int)(value % (uint64_t)base);
        buffer[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= (uint64_t)base;
    }
    while (count < width && count < 31) buffer[count++] = pad;
    while (count > 0) cc_output_char(buffer[--count]);
}

static void cc_print_signed(int64_t value, int width, char pad) {
    if (value < 0) {
        cc_output_char('-');
        cc_print_unsigned((uint64_t)(-value), 10, width ? width - 1 : 0, pad);
    } else {
        cc_print_unsigned((uint64_t)value, 10, width, pad);
    }
}

int cc_rt_printf(const char *format, int a, int b, int c, int d, int e) {
    int values[5] = {a, b, c, d, e};
    int next = 0;
    if (!format) return 0;
    while (*format) {
        if (*format != '%') {
            cc_output_char(*format++);
            continue;
        }
        format++;
        char pad = ' ';
        int width = 0;
        if (*format == '0') { pad = '0'; format++; }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        char spec = *format++;
        if (spec == '%') { cc_output_char('%'); continue; }
        int value = next < 5 ? values[next++] : 0;
        if (spec == 'd' || spec == 'i') cc_print_signed(value, width, pad);
        else if (spec == 'u') cc_print_unsigned((uint32_t)value, 10, width, pad);
        else if (spec == 'x') cc_print_unsigned((uint32_t)value, 16, width, pad);
        else if (spec == 'c') cc_output_char((char)value);
        else if (spec == 's') {
            const char *text = (const char *)(uintptr_t)value;
            if (!text) text = "(null)";
            while (*text) cc_output_char(*text++);
        }
        else if (spec == 'p') {
            cc_output_char('0'); cc_output_char('x');
            cc_print_unsigned((uint32_t)value, 16, 8, '0');
        } else {
            cc_output_char('%');
            cc_output_char(spec);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

int cc_compile(const char *source, uint8_t *image, uint32_t image_capacity,
               uint32_t *image_size) {
    if (!source || !image || !image_size) {
        cc_set_error(0, "invalid compiler buffers");
        return -1;
    }
    for (int i = 0; i < (int)sizeof(cc_nodes); i++) ((uint8_t *)cc_nodes)[i] = 0;
    cc_node_used = 0;
    cc_name_used = 0;
    cc_data_used = 0;
    cc_code_used = 0;
    cc_sym_used = 0;
    cc_func_used = 0;
    cc_error[0] = '\0';
    cc_fixup_used = 0;
    cc_loop_sp = 0;
    cc_main_func = -1;

    struct cc_parser parser;
    parser.lx.p = source;
    parser.lx.line = 1;
    parser.current_func = -1;
    parser.frame_cursor = 0;
    parser.scope_mark = 0;
    cc_active_parser = &parser;
    if (!cc_next_token(&parser.lx, &parser.tok)) return -1;
    if (!cc_parse_program(&parser)) return -1;

    struct cc_func *main_func = cc_find_func("main");
    if (!main_func || !main_func->defined) {
        cc_set_error(0, "program has no defined main()");
        return -1;
    }
    cc_main_func = (int32_t)(main_func - cc_funcs);

    cc_gen_func(main_func);
    for (int32_t i = 0; i < cc_func_used; i++)
        if (i != cc_main_func) cc_gen_func(&cc_funcs[i]);
    if (cc_error[0]) return -1;

    for (int32_t i = 0; i < cc_fixup_used; i++) {
        struct cc_func *target = &cc_funcs[cc_fixups[i].function];
        if (!target->defined || target->offset < 0) {
            cc_set_error(0, "call target was not generated");
            return -1;
        }
        cc_patch_rel32(cc_fixups[i].position, target->offset);
    }

    uint32_t total = (uint32_t)sizeof(struct scx_header) +
                     (uint32_t)cc_code_used + (uint32_t)cc_data_used;
    if (total > image_capacity) {
        cc_set_error(0, "output image is too large");
        return -1;
    }
    struct scx_header *header = (struct scx_header *)image;
    header->magic = SOLIS_SCX_MAGIC;
    header->version = SOLIS_SCX_VERSION;
    header->header_size = sizeof(struct scx_header);
    header->code_size = (uint32_t)cc_code_used;
    header->data_size = (uint32_t)cc_data_used;
    header->entry = 0;
    header->reserved = 0;
    uint8_t *output = image + sizeof(struct scx_header);
    for (int32_t i = 0; i < cc_code_used; i++) output[i] = cc_code[i];
    for (int32_t i = 0; i < cc_data_used; i++)
        output[cc_code_used + i] = cc_data[i];
    *image_size = total;
    return 0;
}
