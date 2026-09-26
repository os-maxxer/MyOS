#include <solis/apps/cinterp.h>
#include <stdint.h>
#include <stddef.h>

typedef int32_t Value;
typedef uint32_t Addr;

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

#define CI_MAX_NODES      8192
#define CI_MAX_NAMES      1024
#define CI_MAX_STRLIT     2048
#define CI_MAX_GLOBAL_MEM 4096
#define CI_MAX_STACK_MEM  32768
#define CI_MAX_TOP_MEM    4096
#define CI_MAX_TYPES      256
#define CI_MAX_STRUCTS    32
#define CI_MAX_MEMBERS    24
#define CI_MAX_SYM        512
#define CI_MAX_SCOPES     64
#define CI_MAX_FUNCS      64
#define CI_MAX_ARGS       16
#define CI_MAX_DEPTH      64
#define CI_MAX_INIT       64

/* ------------------------------------------------------------------ */
/* Tokens                                                              */
/* ------------------------------------------------------------------ */

enum {
    TK_EOF = 0, TK_IDENT, TK_NUM, TK_STR, TK_CHARLIT, TK_PUNCT, TK_KEYWORD
};

/* Punctuator ids double as binary operator ids. */
enum {
    P_NONE = 0,
    P_LPAREN, P_RPAREN, P_LBRACE, P_RBRACE, P_LBRACK, P_RBRACK,
    P_SEMI, P_COMMA, P_DOT, P_QUESTION, P_COLON,
    P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT,
    P_AMP, P_PIPE, P_CARET, P_TILDE, P_BANG,
    P_LT, P_GT, P_LE, P_GE, P_EQ, P_NE,
    P_ANDAND, P_OROR, P_SHL, P_SHR,
    P_ASSIGN, P_ADD_ASSIGN, P_SUB_ASSIGN, P_MUL_ASSIGN, P_DIV_ASSIGN,
    P_MOD_ASSIGN, P_AND_ASSIGN, P_OR_ASSIGN, P_XOR_ASSIGN,
    P_SHL_ASSIGN, P_SHR_ASSIGN,
    P_INC, P_DEC
};

enum {
    K_NONE = 0, K_INT, K_CHAR, K_VOID, K_STRUCT, K_TYPEDEF, K_UNSIGNED,
    K_SIGNED, K_CONST, K_SIZEOF, K_STATIC, K_EXTERN,
    K_IF, K_ELSE, K_WHILE, K_DO, K_FOR, K_RETURN, K_BREAK, K_CONTINUE
};

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

enum { TY_VOID = 0, TY_CHAR, TY_INT, TY_PTR, TY_ARRAY, TY_STRUCT };

struct type {
    uint8_t kind;
    uint8_t is_unsigned;
    struct type *base;   /* pointee / element type */
    int32_t len;         /* array length */
    int32_t sid;         /* struct id */
    int32_t size;        /* bytes */
};

struct member {
    const char *name;
    struct type *type;
    int32_t offset;
};

struct struct_def {
    const char *name;
    int32_t size;
    int32_t count;
    struct member members[CI_MAX_MEMBERS];
};

struct sym {
    const char *name;
    struct type *type;
    uint8_t is_global;
    uint8_t is_active;
    uint8_t is_typedef;
    uint8_t is_func;
    int32_t offset;      /* byte offset in frame, or in global_mem */
    int32_t scope;
    int32_t func;        /* index into funcs[] when is_func */
};

struct func {
    const char *name;
    struct type *ret;
    int32_t nparams;
    struct sym *params[CI_MAX_ARGS];
    int32_t param_off[CI_MAX_ARGS];
    struct node *body;
    int32_t frame_size;
    int32_t builtin;     /* BI_* below, 0 for a real body */
};

enum {
    BI_NONE = 0, BI_PRINTF, BI_PUTS, BI_PUTCHAR, BI_STRLEN, BI_STRCMP,
    BI_STRCPY, BI_MEMSET, BI_ABS, BI_EXIT, BI_STRCAT, BI_STRNCMP
};

static const char *const builtin_names[] = {
    "printf", "puts", "putchar", "strlen", "strcmp", "strcpy", "memset",
    "abs", "exit", "strcat", "strncmp"
};
static const int32_t builtin_ids[] = {
    BI_PRINTF, BI_PUTS, BI_PUTCHAR, BI_STRLEN, BI_STRCMP, BI_STRCPY,
    BI_MEMSET, BI_ABS, BI_EXIT, BI_STRCAT, BI_STRNCMP
};
#define builtin_count ((int32_t)(sizeof builtin_ids / sizeof builtin_ids[0]))

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum {
    N_NUM = 0, N_STR, N_VAR, N_CALL,
    N_BIN, N_UN, N_ASSIGN, N_COND, N_CAST, N_PREINC, N_POSTINC, N_COMMA,
    N_DECL_ITEM, N_ARG,
    N_EXPR, N_DECL, N_BLOCK, N_IF, N_WHILE, N_DO, N_FOR, N_RETURN,
    N_BREAK, N_CONTINUE, N_EMPTY
};

struct node {
    uint8_t kind;
    uint8_t op;
    int32_t line;
    Value num;              /* literal value, or symbol/struct/function id */
    struct node *a, *b, *c, *d;
    struct node *next;
    struct type *type;      /* static result type */
    struct sym *sym;
    int32_t extra;          /* argc, member index, ... */
};

/* Control-flow signals returned by exec_stmt(). */
enum { FLOW_NORMAL = 0, FLOW_BREAK, FLOW_CONTINUE, FLOW_RETURN };

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static struct node node_pool[CI_MAX_NODES];
static int32_t node_used;

static char name_pool[CI_MAX_NAMES];
static int32_t name_used;
static char str_pool[CI_MAX_STRLIT];
static int32_t str_used;

static uint8_t global_mem[CI_MAX_GLOBAL_MEM];
static int32_t global_used;
static uint8_t stack_mem[CI_MAX_STACK_MEM];
static int32_t stack_sp;
static uint8_t top_mem[CI_MAX_TOP_MEM];

static struct type type_pool[CI_MAX_TYPES];
static int32_t type_used;
static struct struct_def structs[CI_MAX_STRUCTS];
static int32_t struct_used;
static struct sym syms[CI_MAX_SYM];
static int32_t sym_used;
static struct func funcs[CI_MAX_FUNCS];
static int32_t func_used;

static struct node *global_inits[CI_MAX_INIT];
static int32_t global_init_used;

static ci_putchar_fn out_putchar;
static char ci_error[160];
static Value ret_value;

/* Loop depth tracking for break/continue, used only to keep the parser
 * honest about unbalanced loops. */
static int loop_depth;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void fail(int32_t line, const char *msg) {
    if (ci_error[0]) return;                 /* keep the first error */
    if (line > 0) {
        const char *p = msg;
        int i = 0;
        while (*p && i < 100) { ci_error[i++] = *p++; }
        ci_error[i] = 0;
        /* append " (line N)" */
        int n = i;
        ci_error[n++] = ' ';
        ci_error[n++] = '(';
        ci_error[n++] = 'l';
        ci_error[n++] = 'i';
        ci_error[n++] = 'n';
        ci_error[n++] = 'e';
        ci_error[n++] = ' ';
        if (line >= 1000) ci_error[n++] = (char)('0' + (line / 1000) % 10);
        if (line >= 100)  ci_error[n++] = (char)('0' + (line / 100) % 10);
        if (line >= 10)   ci_error[n++] = (char)('0' + (line / 10) % 10);
        ci_error[n++] = (char)('0' + line % 10);
        ci_error[n++] = ')';
        ci_error[n] = 0;
    } else {
        int i = 0;
        while (*msg && i < (int)sizeof(ci_error) - 1) ci_error[i++] = *msg++;
        ci_error[i] = 0;
    }
}

static int name_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static const char *intern(const char *s, int len) {
    if (name_used + len + 1 > CI_MAX_NAMES) return 0;
    char *dst = name_pool + name_used;
    for (int i = 0; i < len; i++) dst[i] = s[i];
    dst[len] = 0;
    name_used += len + 1;
    return dst;
}

static struct node *new_node(int kind, int32_t line) {
    if (node_used >= CI_MAX_NODES) {
        fail(line, "program too large");
        return 0;
    }
    struct node *n = &node_pool[node_used++];
    n->kind = (uint8_t)kind;
    n->op = 0;
    n->line = line;
    n->num = 0;
    n->a = n->b = n->c = n->d = 0;
    n->next = 0;
    n->type = 0;
    n->sym = 0;
    n->extra = 0;
    return n;
}

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

static struct type *new_type(uint8_t kind, int32_t line) {
    if (type_used >= CI_MAX_TYPES) {
        fail(line, "too many types");
        return 0;
    }
    struct type *t = &type_pool[type_used++];
    t->kind = kind;
    t->is_unsigned = 0;
    t->base = 0;
    t->len = 0;
    t->sid = -1;
    t->size = (kind == TY_CHAR) ? 1 : (kind == TY_VOID ? 1 : 4);
    return t;
}

static struct type *ty_int, *ty_char, *ty_void;

static struct type *pointer_to(struct type *base) {
    for (int i = 0; i < type_used; i++)
        if (type_pool[i].kind == TY_PTR && type_pool[i].base == base)
            return &type_pool[i];
    struct type *t = new_type(TY_PTR, 0);
    if (!t) return 0;
    t->base = base;
    t->size = 4;
    return t;
}

static struct type *array_of(struct type *base, int32_t len) {
    struct type *t = new_type(TY_ARRAY, 0);
    if (!t) return 0;
    t->base = base;
    t->len = len;
    t->size = base->size * len;
    return t;
}

static int32_t type_size(const struct type *t) {
    return t ? t->size : 0;
}

/* Size of the value an expression of this type denotes, i.e. what a
 * dereference or a pointer step moves. */
static int32_t elem_size(const struct type *t) {
    if (!t) return 1;
    if (t->kind == TY_ARRAY) return t->base ? t->base->size : 1;
    return t->size ? t->size : 1;
}

/* ------------------------------------------------------------------ */
/* Symbols                                                             */
/* ------------------------------------------------------------------ */

static struct sym *find_sym_local(const char *name) {
    for (int32_t i = sym_used - 1; i >= 0; i--)
        if (syms[i].is_active && !syms[i].is_global && name_eq(syms[i].name, name)) return &syms[i];
    return 0;
}

static struct sym *find_sym_global(const char *name) {
    for (int32_t i = 0; i < sym_used; i++)
        if (syms[i].is_global && name_eq(syms[i].name, name)) return &syms[i];
    return 0;
}

static struct sym *find_sym_any(const char *name) {
    struct sym *s = find_sym_local(name);
    return s ? s : find_sym_global(name);
}

static struct sym *new_sym(const char *name, struct type *t, int32_t line) {
    if (sym_used >= CI_MAX_SYM) {
        fail(line, "too many variables");
        return 0;
    }
    struct sym *s = &syms[sym_used++];
    s->name = name;
    s->type = t;
    s->is_global = 0;
    s->is_active = 1;
    s->is_typedef = 0;
    s->is_func = 0;
    s->offset = 0;
    s->scope = 0;
    s->func = -1;
    return s;
}

/* ------------------------------------------------------------------ */
/* Memory: every guest address lives in one of these three arenas      */
/* ------------------------------------------------------------------ */

static int in_region(Addr a, uint32_t size, const void *base, uint32_t len) {
    Addr lo = (Addr)base;
    Addr hi = lo + len;
    if (a < lo || a >= hi) return 0;
    return (hi - a) >= size;
}

static int mem_ok(Addr a, uint32_t size) {
    if (in_region(a, size, stack_mem, CI_MAX_STACK_MEM)) return 1;
    if (in_region(a, size, global_mem, CI_MAX_GLOBAL_MEM)) return 1;
    if (in_region(a, size, str_pool, CI_MAX_STRLIT)) return 1;
    if (in_region(a, size, top_mem, CI_MAX_TOP_MEM)) return 1;
    return 0;
}

static int32_t mem_read(Addr a, int width) {
    if (!mem_ok(a, (uint32_t)width)) return 0;
    if (width == 1) return *(const int8_t *)a;
    if (width == 2) {
        int16_t v;
        const uint8_t *p = (const uint8_t *)a;
        v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        return v;
    }
    return *(const int32_t *)(const void *)a;
}

static void mem_write(Addr a, int32_t value, int width) {
    if (!mem_ok(a, (uint32_t)width)) return;
    if (width == 1) { *(int8_t *)a = (int8_t)value; return; }
    if (width == 2) {
        uint8_t *p = (uint8_t *)a;
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)(value >> 8);
        return;
    }
    *(int32_t *)(void *)a = value;
}

static int32_t align4(int32_t v) { return (v + 3) & ~3; }

static int32_t frame_alloc(int32_t size) {
    size = align4(size);
    if (size <= 0) size = 4;
    if (stack_sp + size > CI_MAX_STACK_MEM) return -1;
    int32_t base = stack_sp;
    stack_sp += size;
    for (int32_t i = 0; i < size; i++) stack_mem[base + i] = 0;
    return base;
}

/* ------------------------------------------------------------------ */
/* Lexer                                                               */
/* ------------------------------------------------------------------ */

struct token {
    uint8_t kind;
    uint8_t op;        /* punctuator / keyword id */
    int32_t line;
    Value num;         /* TK_NUM, TK_CHARLIT */
    const char *text;  /* TK_IDENT (in name_pool), TK_STR (in str_pool) */
};

struct kw_entry { const char *name; uint8_t id; };
static const struct kw_entry keywords[] = {
    { "int", K_INT }, { "char", K_CHAR }, { "void", K_VOID },
    { "struct", K_STRUCT }, { "typedef", K_TYPEDEF },
    { "unsigned", K_UNSIGNED }, { "signed", K_SIGNED },
    { "const", K_CONST }, { "sizeof", K_SIZEOF },
    { "static", K_STATIC }, { "extern", K_EXTERN },
    { "if", K_IF }, { "else", K_ELSE }, { "while", K_WHILE },
    { "do", K_DO }, { "for", K_FOR }, { "return", K_RETURN },
    { "break", K_BREAK }, { "continue", K_CONTINUE },
    { 0, 0 }
};

/* Longest match first, so "<=" wins over "<". */
struct punct_entry { const char *text; uint8_t id; };
static const struct punct_entry puncts[] = {
    { "<<=", P_SHL_ASSIGN }, { ">>=", P_SHR_ASSIGN },
    { "...", P_NONE },
    { "==", P_EQ }, { "!=", P_NE }, { "<=", P_LE }, { ">=", P_GE },
    { "&&", P_ANDAND }, { "||", P_OROR }, { "<<", P_SHL }, { ">>", P_SHR },
    { "++", P_INC }, { "--", P_DEC },
    { "+=", P_ADD_ASSIGN }, { "-=", P_SUB_ASSIGN }, { "*=", P_MUL_ASSIGN },
    { "/=", P_DIV_ASSIGN }, { "%=", P_MOD_ASSIGN }, { "&=", P_AND_ASSIGN },
    { "|=", P_OR_ASSIGN }, { "^=", P_XOR_ASSIGN },
    { "(", P_LPAREN }, { ")", P_RPAREN }, { "{", P_LBRACE }, { "}", P_RBRACE },
    { "[", P_LBRACK }, { "]", P_RBRACK }, { ";", P_SEMI }, { ",", P_COMMA },
    { ".", P_DOT }, { "?", P_QUESTION }, { ":", P_COLON },
    { "+", P_PLUS }, { "-", P_MINUS }, { "*", P_STAR }, { "/", P_SLASH },
    { "%", P_PERCENT }, { "&", P_AMP }, { "|", P_PIPE }, { "^", P_CARET },
    { "~", P_TILDE }, { "!", P_BANG }, { "<", P_LT }, { ">", P_GT },
    { "=", P_ASSIGN },
    { 0, 0 }
};

struct lexer {
    const char *p;
    int32_t line;
};

static void skip_space(struct lexer *lx) {
    for (;;) {
        char c = *lx->p;
        if (c == '\n') { lx->line++; lx->p++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { lx->p++; continue; }
        /* A line starting with '#' is skipped wholesale. The subset has no
         * preprocessor, so this keeps #include/#define headers harmless. */
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

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_ident(char c) { return is_ident_start(c) || is_digit(c); }

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode one escape sequence, advancing *p past it. */
static int read_escape(const char **pp) {
    const char *p = *pp;
    char c = *p++;
    int value;
    switch (c) {
        case 'n': value = '\n'; break;
        case 't': value = '\t'; break;
        case 'r': value = '\r'; break;
        case '0': value = '\0'; break;
        case 'a': value = 7; break;
        case 'b': value = 8; break;
        case 'f': value = 12; break;
        case 'v': value = 11; break;
        case '\\': value = '\\'; break;
        case '\'': value = '\''; break;
        case '"': value = '"'; break;
        case '?': value = '?'; break;
        case 'x': {
            value = 0;
            int digits = 0;
            while (hex_value(*p) >= 0 && digits < 2) {
                value = value * 16 + hex_value(*p);
                p++;
                digits++;
            }
            break;
        }
        default:
            value = (unsigned char)c;
            break;
    }
    *pp = p;
    return value;
}

static int next_token(struct lexer *lx, struct token *t) {
    skip_space(lx);
    t->line = lx->line;
    t->op = 0;
    t->num = 0;
    t->text = 0;
    char c = *lx->p;

    if (!c) { t->kind = TK_EOF; return 1; }

    if (is_ident_start(c)) {
        const char *start = lx->p;
        while (is_ident(*lx->p)) lx->p++;
        int len = (int)(lx->p - start);
        for (int i = 0; keywords[i].name; i++) {
            int klen = 0;
            while (keywords[i].name[klen]) klen++;
            if (klen == len) {
                int match = 1;
                for (int j = 0; j < len; j++)
                    if (start[j] != keywords[i].name[j]) { match = 0; break; }
                if (match) {
                    t->kind = TK_KEYWORD;
                    t->op = keywords[i].id;
                    return 1;
                }
            }
        }
        t->kind = TK_IDENT;
        t->text = intern(start, len);
        if (!t->text) { fail(lx->line, "out of identifier space"); return 0; }
        return 1;
    }

    if (is_digit(c)) {
        Value value = 0;
        if (c == '0' && (lx->p[1] == 'x' || lx->p[1] == 'X')) {
            lx->p += 2;
            while (hex_value(*lx->p) >= 0) {
                value = value * 16 + hex_value(*lx->p);
                lx->p++;
            }
        } else {
            while (is_digit(*lx->p)) {
                value = value * 10 + (*lx->p - '0');
                lx->p++;
            }
        }
        t->kind = TK_NUM;
        t->num = value;
        return 1;
    }

    if (c == '\'') {
        lx->p++;
        int value;
        if (*lx->p == '\\') { lx->p++; value = read_escape(&lx->p); }
        else value = (unsigned char)*lx->p++;
        if (*lx->p == '\'') lx->p++;
        t->kind = TK_CHARLIT;
        t->num = value;
        return 1;
    }

    if (c == '"') {
        lx->p++;
        if (str_used + 1 >= CI_MAX_STRLIT) { fail(lx->line, "out of string space"); return 0; }
        char *dst = str_pool + str_used;
        const char *start = dst;
        while (*lx->p && *lx->p != '"') {
            int value;
            if (*lx->p == '\\') { lx->p++; value = read_escape(&lx->p); }
            else value = (unsigned char)*lx->p++;
            if (str_used + 2 >= CI_MAX_STRLIT) { fail(lx->line, "out of string space"); return 0; }
            *dst++ = (char)value;
            str_used++;
            if (value == '\n') lx->line++;
        }
        if (*lx->p == '"') lx->p++;
        *dst = 0;
        str_used++;
        t->kind = TK_STR;
        t->text = start;
        return 1;
    }

    for (int i = 0; puncts[i].text; i++) {
        int len = 0;
        while (puncts[i].text[len]) len++;
        int match = 1;
        for (int j = 0; j < len; j++) {
            if (lx->p[j] != puncts[i].text[j]) { match = 0; break; }
        }
        if (match) {
            /* "..." is not supported; report it as an unknown token rather
             * than silently treating it as three '.' operators. */
            t->kind = TK_PUNCT;
            t->op = puncts[i].id;
            lx->p += len;
            return 1;
        }
    }

    fail(lx->line, "unexpected character");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

struct parser {
    struct lexer lx;
    struct token tok;      /* current */
    struct token ahead;    /* one token of lookahead */
    int has_ahead;
    int32_t scope_depth;
    int32_t fn_scope_base; /* sym count at the start of the current function */
    int32_t fn_frame;      /* running frame size for locals */
    int32_t top_frame;     /* running frame size for top-level locals */
    int in_function;
};

static int advance(struct parser *p) {
    if (p->has_ahead) {
        p->tok = p->ahead;
        p->has_ahead = 0;
        return 1;
    }
    return next_token(&p->lx, &p->tok);
}

static struct token *peek(struct parser *p) {
    if (!p->has_ahead) {
        if (!next_token(&p->lx, &p->ahead)) { p->ahead.kind = TK_EOF; p->ahead.line = p->tok.line; }
        p->has_ahead = 1;
    }
    return &p->ahead;
}

static int is_punct(struct parser *p, uint8_t op) {
    return p->tok.kind == TK_PUNCT && p->tok.op == op;
}
static int is_kw(struct parser *p, uint8_t kw) {
    return p->tok.kind == TK_KEYWORD && p->tok.op == kw;
}
static int eat_punct(struct parser *p, uint8_t op) {
    if (!is_punct(p, op)) return 0;
    advance(p);
    return 1;
}
static int eat_kw(struct parser *p, uint8_t kw) {
    if (!is_kw(p, kw)) return 0;
    advance(p);
    return 1;
}

static int expect_punct(struct parser *p, uint8_t op) {
    if (eat_punct(p, op)) return 1;
    fail(p->tok.line, "syntax error");
    return 0;
}

static struct node *parse_expr(struct parser *p);
static struct node *parse_assign(struct parser *p);
static struct node *parse_statement(struct parser *p);
static struct node *parse_declaration(struct parser *p, int allow_func_def, struct func *out_fn);
static int parse_type(struct parser *p, struct type **out, int allow_unsigned);

/* ---------------- scopes ---------------- */

static int push_scope(struct parser *p) {
    if (p->scope_depth + 1 >= CI_MAX_SCOPES) return 0;
    p->scope_depth++;
    return 1;
}

static void pop_scope(struct parser *p) {
    for (int32_t i = p->fn_scope_base; i < sym_used; i++)
        if (!syms[i].is_global && syms[i].scope == p->scope_depth)
            syms[i].is_active = 0;
    p->scope_depth--;
}

/* ---------------- types ---------------- */

static struct type *find_typedef(const char *name) {
    struct sym *s = find_sym_local(name);
    if (s && s->is_typedef) return s->type;
    s = find_sym_global(name);
    if (s && s->is_typedef) return s->type;
    return 0;
}

static int is_type_start(struct parser *p) {
    if (p->tok.kind == TK_IDENT) return find_typedef(p->tok.text) != 0;
    if (p->tok.kind != TK_KEYWORD) return 0;
    switch (p->tok.op) {
        case K_INT: case K_CHAR: case K_VOID: case K_STRUCT:
        case K_UNSIGNED: case K_SIGNED: case K_CONST:
        case K_STATIC: case K_EXTERN: case K_TYPEDEF:
            return 1;
        default:
            return 0;
    }
}

/* Parse a type specifier. Returns 0 (without failing) when the current
 * token cannot start a type at all. */
static int parse_type(struct parser *p, struct type **out, int allow_unsigned) {
    struct type *base = 0;
    int is_unsigned = 0;
    int32_t line = p->tok.line;

    for (;;) {
        if (is_kw(p, K_CONST) || is_kw(p, K_STATIC) || is_kw(p, K_EXTERN)) {
            advance(p);
            continue;
        }
        if (is_kw(p, K_UNSIGNED)) { is_unsigned = 1; advance(p); continue; }
        if (is_kw(p, K_SIGNED)) { advance(p); continue; }
        break;
    }

    if (p->tok.kind == TK_IDENT && !base) {
        struct type *td = find_typedef(p->tok.text);
        if (!td) return 0;
        base = td;
        advance(p);
    } else if (is_kw(p, K_VOID)) {
        base = ty_void;
        advance(p);
    } else if (is_kw(p, K_CHAR)) {
        base = ty_char;
        advance(p);
    } else if (is_kw(p, K_INT)) {
        base = ty_int;
        advance(p);
    } else if (is_kw(p, K_STRUCT)) {
        advance(p);
        int32_t sid = -1;
        const char *tag = 0;
        if (p->tok.kind == TK_IDENT) {
            tag = p->tok.text;
            advance(p);
        }
        if (is_punct(p, P_LBRACE)) {
            /* struct definition */
            if (struct_used >= CI_MAX_STRUCTS) { fail(line, "too many structs"); return 0; }
            sid = struct_used++;
            structs[sid].size = 0;
            structs[sid].count = 0;
            structs[sid].name = tag;
            if (!expect_punct(p, P_LBRACE)) return 0;
            int32_t offset = 0;
            while (!is_punct(p, P_RBRACE)) {
                struct type *mt = 0;
                if (!parse_type(p, &mt, allow_unsigned)) { fail(p->tok.line, "bad member type"); return 0; }
                for (;;) {
                    if (p->tok.kind != TK_IDENT) { fail(p->tok.line, "expected member name"); return 0; }
                    const char *mname = p->tok.text;
                    advance(p);
                    struct type *full = mt;
                    while (eat_punct(p, P_STAR)) {
                        full = pointer_to(full);
                        if (!full) return 0;
                    }
                    while (is_punct(p, P_LBRACK)) {
                        advance(p);
                        int32_t len = 0;
                        if (p->tok.kind == TK_NUM) { len = p->tok.num; advance(p); }
                        if (!expect_punct(p, P_RBRACK)) return 0;
                        full = array_of(full, len);
                        if (!full) return 0;
                    }
                    if (structs[sid].count >= CI_MAX_MEMBERS) { fail(line, "too many members"); return 0; }
                    struct member *m = &structs[sid].members[structs[sid].count++];
                    m->name = mname;
                    m->type = full;
                    m->offset = offset;
                    /* Align the member. */
                    int align = type_size(full) > 4 ? 4 : type_size(full);
                    if (align < 1) align = 1;
                    offset = align4(offset);
                    if (align > 1) offset = (offset / align) * align;
                    offset += type_size(full);
                    if (!eat_punct(p, P_COMMA)) break;
                }
                if (!expect_punct(p, P_SEMI)) return 0;
            }
            if (!expect_punct(p, P_RBRACE)) return 0;
            structs[sid].size = align4(offset);
        } else {
            if (!tag) { fail(line, "expected struct tag"); return 0; }
            for (int32_t i = 0; i < struct_used; i++)
                if (structs[i].name && name_eq(structs[i].name, tag)) { sid = i; break; }
            if (sid < 0) { fail(line, "unknown struct tag"); return 0; }
        }
        base = new_type(TY_STRUCT, line);
        if (!base) return 0;
        base->sid = sid;
        base->size = structs[sid].size;
    } else {
        return 0;
    }

    while (eat_punct(p, P_STAR)) {
        base = pointer_to(base);
        if (!base) return 0;
    }
    if (is_unsigned && base) {
        /* Do not mutate the shared base type; "unsigned" only changes how
         * comparisons and division treat the value. */
        struct type *u = new_type(base->kind, line);
        if (!u) return 0;
        u->base = base->base;
        u->sid = base->sid;
        u->size = base->size;
        u->len = base->len;
        u->is_unsigned = 1;
        base = u;
    }
    *out = base;
    return 1;
}

/* ---------------- struct member lookup ---------------- */

static struct member *find_member(int32_t sid, const char *name, int32_t *offset) {
    if (sid < 0 || sid >= struct_used) return 0;
    for (int32_t i = 0; i < structs[sid].count; i++) {
        if (name_eq(structs[sid].members[i].name, name)) {
            *offset += structs[sid].members[i].offset;
            return &structs[sid].members[i];
        }
    }
    return 0;
}

/* ---------------- expressions ---------------- */

static struct node *make_num(Value v, int32_t line) {
    struct node *n = new_node(N_NUM, line);
    if (!n) return 0;
    n->num = v;
    n->type = ty_int;
    return n;
}

static struct node *make_un(int op, struct node *a, int32_t line) {
    struct node *n = new_node(N_UN, line);
    if (!n) return 0;
    n->op = (uint8_t)op;
    n->a = a;
    if (op == P_BANG) n->type = ty_int;
    else if (op == P_MINUS) n->type = a ? a->type : ty_int;
    else n->type = a ? a->type : ty_int;   /* & ~ + keep the operand type */
    return n;
}

static int is_type_name_tok(const struct token *t) {
    if (t->kind == TK_IDENT) return find_typedef(t->text) != 0;
    if (t->kind != TK_KEYWORD) return 0;
    switch (t->op) {
        case K_INT: case K_CHAR: case K_VOID: case K_STRUCT:
        case K_UNSIGNED: case K_SIGNED: case K_CONST: case K_STATIC:
        case K_EXTERN: case K_TYPEDEF:
            return 1;
        default: return 0;
    }
}

static struct node *parse_primary(struct parser *p) {
    int32_t line = p->tok.line;

    /* cast: '(' type ')' unary */
    if (is_punct(p, P_LPAREN) && is_type_name_tok(peek(p))) {
        advance(p);
        struct type *ct = 0;
        if (!parse_type(p, &ct, 1)) return 0;
        if (!expect_punct(p, P_RPAREN)) return 0;
        struct node *operand = parse_primary(p);
        if (!operand) return 0;
        struct node *n = new_node(N_CAST, line);
        if (!n) return 0;
        n->type = ct;
        n->a = operand;
        return n;
    }

    if (is_punct(p, P_LPAREN)) {
        advance(p);
        struct node *e = parse_expr(p);
        if (!e) return 0;
        if (!expect_punct(p, P_RPAREN)) return 0;
        return e;
    }

    if (p->tok.kind == TK_NUM) {
        Value v = p->tok.num;
        advance(p);
        return make_num(v, line);
    }
    if (p->tok.kind == TK_CHARLIT) {
        Value v = p->tok.num;
        advance(p);
        return make_num(v, line);
    }
    if (p->tok.kind == TK_STR) {
        Addr addr = (Addr)p->tok.text;
        advance(p);
        struct node *n = new_node(N_STR, line);
        if (!n) return 0;
        n->num = (Value)addr;
        n->type = pointer_to(ty_char);
        return n;
    }
    if (is_kw(p, K_SIZEOF)) {
        advance(p);
        int32_t size = 1;
        if (is_punct(p, P_LPAREN) && is_type_name_tok(peek(p))) {
            advance(p);
            struct type *t = 0;
            if (!parse_type(p, &t, 1)) return 0;
            if (!expect_punct(p, P_RPAREN)) return 0;
            size = type_size(t);
        } else {
            struct node *operand = parse_primary(p);
            if (!operand) return 0;
            size = type_size(operand->type);
        }
        return make_num(size, line);
    }
    if (p->tok.kind == TK_IDENT) {
        const char *name = p->tok.text;
        advance(p);
        if (is_punct(p, P_LPAREN)) {
            advance(p);
            struct node *n = new_node(N_CALL, line);
            if (!n) return 0;
            struct node *last = 0;
            int argc = 0;
            if (!is_punct(p, P_RPAREN)) {
                for (;;) {
                    struct node *arg = parse_assign(p);
                    if (!arg) return 0;
                    struct node *link = new_node(N_ARG, line);
                    if (!link) return 0;
                    link->a = arg;
                    if (last) last->b = link; else n->a = link;
                    last = link;
                    if (++argc > CI_MAX_ARGS) { fail(line, "too many arguments"); return 0; }
                    if (!eat_punct(p, P_COMMA)) break;
                }
            }
            if (!expect_punct(p, P_RPAREN)) return 0;
            n->extra = argc;
            /* The callee is looked up after the whole file is parsed, so a
             * function may be called before it is defined. The name already
             * lives in the identifier pool, so its address is enough. */
            struct node *callee = new_node(N_STR, line);
            if (!callee) return 0;
            callee->num = (Value)(Addr)name;
            n->c = callee;
            n->type = ty_int;
            return n;
        }
        struct sym *s = find_sym_any(name);
        if (!s) { fail(line, "undefined variable"); return 0; }
        if (s->is_func) { fail(line, "function used as a value"); return 0; }
        struct node *n = new_node(N_VAR, line);
        if (!n) return 0;
        n->sym = s;
        n->type = s->type;
        return n;
    }

    fail(line, "expected an expression");
    return 0;
}

static struct node *parse_postfix(struct parser *p) {
    struct node *e = parse_primary(p);
    if (!e) return 0;
    for (;;) {
        int32_t line = p->tok.line;
        if (is_punct(p, P_LBRACK)) {
            advance(p);
            struct node *idx = parse_expr(p);
            if (!idx) return 0;
            if (!expect_punct(p, P_RBRACK)) return 0;
            struct node *n = new_node(N_BIN, line);
            if (!n) return 0;
            n->op = P_LBRACK;
            n->a = e;
            n->b = idx;
            n->type = (e->type && (e->type->kind == TY_PTR || e->type->kind == TY_ARRAY) && e->type->base)
                          ? e->type->base : ty_int;
            e = n;
            continue;
        }
        if (is_punct(p, P_DOT)) {
            advance(p);
            if (p->tok.kind != TK_IDENT) { fail(p->tok.line, "expected a member name"); return 0; }
            const char *mname = p->tok.text;
            advance(p);
            if (!e->type || e->type->kind != TY_STRUCT) { fail(line, "not a struct"); return 0; }
            int32_t offset = 0;
            struct member *m = find_member(e->type->sid, mname, &offset);
            if (!m) { fail(line, "no such member"); return 0; }
            struct node *n = new_node(N_BIN, line);
            if (!n) return 0;
            n->op = P_DOT;
            n->a = e;
            n->num = offset;
            n->type = m->type;
            e = n;
            continue;
        }
        if (is_punct(p, P_INC) || is_punct(p, P_DEC)) {
            int op = p->tok.op;
            advance(p);
            struct node *n = new_node(N_POSTINC, line);
            if (!n) return 0;
            n->op = (uint8_t)op;
            n->a = e;
            n->type = e->type;
            e = n;
            continue;
        }
        return e;
    }
}

static struct node *parse_unary(struct parser *p) {
    int32_t line = p->tok.line;
    if (is_punct(p, P_INC) || is_punct(p, P_DEC)) {
        int op = p->tok.op;
        advance(p);
        struct node *operand = parse_unary(p);
        if (!operand) return 0;
        struct node *n = new_node(N_PREINC, line);
        if (!n) return 0;
        n->op = (uint8_t)op;
        n->a = operand;
        n->type = operand->type;
        return n;
    }
    if (is_punct(p, P_PLUS) || is_punct(p, P_MINUS) ||
        is_punct(p, P_BANG) || is_punct(p, P_TILDE) || is_punct(p, P_AMP)) {
        int op = p->tok.op;
        advance(p);
        struct node *operand = parse_unary(p);
        if (!operand) return 0;
        return make_un((uint8_t)op, operand, line);
    }
    if (is_punct(p, P_STAR)) {
        advance(p);
        struct node *operand = parse_unary(p);
        if (!operand) return 0;
        struct node *n = new_node(N_UN, line);
        if (!n) return 0;
        n->op = P_STAR;
        n->a = operand;
        n->type = (operand->type && operand->type->kind == TY_PTR && operand->type->base)
                      ? operand->type->base : ty_int;
        return n;
    }
    return parse_postfix(p);
}

/* Binary operator precedence, loosest first. */
static int binop_prec(uint8_t op) {
    switch (op) {
        case P_OROR: return 1;
        case P_ANDAND: return 2;
        case P_PIPE: return 3;
        case P_CARET: return 4;
        case P_AMP: return 5;
        case P_EQ: case P_NE: return 6;
        case P_LT: case P_GT: case P_LE: case P_GE: return 7;
        case P_SHL: case P_SHR: return 8;
        case P_PLUS: case P_MINUS: return 9;
        case P_STAR: case P_SLASH: case P_PERCENT: return 10;
        default: return 0;
    }
}

static struct node *make_binop(uint8_t op, struct node *a, struct node *b, int32_t line) {
    struct node *n = new_node(N_BIN, line);
    if (!n) return 0;
    n->op = op;
    n->a = a;
    n->b = b;
    if (op == P_EQ || op == P_NE || op == P_LT || op == P_GT ||
        op == P_LE || op == P_GE || op == P_ANDAND || op == P_OROR) {
        n->type = ty_int;
    } else if (a && a->type && (a->type->kind == TY_PTR || a->type->kind == TY_ARRAY) &&
               op == P_PLUS) {
        n->type = a->type->kind == TY_ARRAY ? a->type : a->type;
    } else {
        n->type = a && a->type ? a->type : ty_int;
    }
    return n;
}

static struct node *parse_binary(struct parser *p, int min_prec) {
    struct node *left = parse_unary(p);
    if (!left) return 0;
    for (;;) {
        if (p->tok.kind != TK_PUNCT) return left;
        int prec = binop_prec(p->tok.op);
        if (prec == 0 || prec < min_prec) return left;
        uint8_t op = p->tok.op;
        int32_t line = p->tok.line;
        advance(p);
        struct node *right = parse_binary(p, prec + 1);
        if (!right) return 0;
        struct node *n = make_binop(op, left, right, line);
        if (!n) return 0;
        left = n;
    }
}

static struct node *parse_conditional(struct parser *p) {
    struct node *cond = parse_binary(p, 1);
    if (!cond) return 0;
    if (!is_punct(p, P_QUESTION)) return cond;
    int32_t line = p->tok.line;
    advance(p);
    struct node *a = parse_expr(p);
    if (!a) return 0;
    if (!expect_punct(p, P_COLON)) return 0;
    struct node *b = parse_conditional(p);
    if (!b) return 0;
    struct node *n = new_node(N_COND, line);
    if (!n) return 0;
    n->a = cond; n->b = a; n->c = b;
    n->type = a->type ? a->type : ty_int;
    return n;
}

static struct node *parse_assign(struct parser *p) {
    struct node *left = parse_conditional(p);
    if (!left) return 0;
    if (p->tok.kind != TK_PUNCT) return left;
    uint8_t op = p->tok.op;
    if (op != P_ASSIGN && op != P_ADD_ASSIGN && op != P_SUB_ASSIGN &&
        op != P_MUL_ASSIGN && op != P_DIV_ASSIGN && op != P_MOD_ASSIGN &&
        op != P_AND_ASSIGN && op != P_OR_ASSIGN && op != P_XOR_ASSIGN &&
        op != P_SHL_ASSIGN && op != P_SHR_ASSIGN) {
        return left;
    }
    int32_t line = p->tok.line;
    advance(p);
    struct node *right = parse_assign(p);
    if (!right) return 0;
    struct node *n = new_node(N_ASSIGN, line);
    if (!n) return 0;
    n->op = op;
    n->a = left;
    n->b = right;
    n->type = left->type;
    return n;
}

static struct node *parse_expr(struct parser *p) {
    struct node *e = parse_assign(p);
    if (!e) return 0;
    while (is_punct(p, P_COMMA)) {
        int32_t line = p->tok.line;
        advance(p);
        struct node *r = parse_assign(p);
        if (!r) return 0;
        struct node *n = new_node(N_COMMA, line);
        if (!n) return 0;
        n->a = e;
        n->b = r;
        n->type = r->type;
        e = n;
    }
    return e;
}

/* ---------------- declarations ---------------- */

/* Assign frame storage for a local, or global storage at file scope. */
static struct sym *declare_var(struct parser *p, const char *name, struct type *t,
                               int32_t line, int is_global) {
    struct sym *existing = is_global ? find_sym_global(name) : find_sym_local(name);
    if (existing && !existing->is_func) {
        /* Re-declaring in a nested scope shadows the outer one. */
        if (is_global && existing->scope == 0) return existing;
    }
    if (is_global && find_sym_global(name)) {
        fail(line, "duplicate global variable");
        return 0;
    }
    struct sym *s = new_sym(name, t, line);
    if (!s) return 0;
    s->is_global = (uint8_t)is_global;
    s->scope = is_global ? 0 : p->scope_depth;
    if (is_global) {
        int32_t size = type_size(t);
        if (size <= 0) size = 4;
        global_used = align4(global_used);
        if (global_used + size > CI_MAX_GLOBAL_MEM) { fail(line, "out of global memory"); return 0; }
        s->offset = global_used;
        global_used += size;
        for (int32_t i = 0; i < size; i++) global_mem[s->offset + i] = 0;
    } else {
        int32_t size = type_size(t);
        if (size <= 0) size = 4;
        int32_t *counter = &p->top_frame;
        if (p->in_function) counter = &p->fn_frame;
        *counter = align4(*counter);
        if (*counter + size > CI_MAX_TOP_MEM && !p->in_function) {
            fail(line, "out of stack memory");
            return 0;
        }
        s->offset = *counter;
        *counter += size;
    }
    return s;
}

/* Parse the declarator list after a base type has been read. Handles both
 * plain variables and function definitions. */
static struct node *parse_declaration(struct parser *p, int allow_func_def, struct func *out_fn) {
    int32_t line = p->tok.line;
    int is_global = (p->scope_depth == 0 && !p->in_function);

    if (is_kw(p, K_TYPEDEF)) {
        /* typedef <type> <name>[, <name>...]; -- alias only, no declarator
         * pointer/array syntax, which is all real programs need here. */
        advance(p);
        struct type *base = 0;
        if (!parse_type(p, &base, 1)) { fail(p->tok.line, "expected a type"); return 0; }
        for (;;) {
            if (p->tok.kind != TK_IDENT) { fail(p->tok.line, "expected a name"); return 0; }
            struct sym *s = new_sym(p->tok.text, base, p->tok.line);
            if (!s) return 0;
            s->is_typedef = 1;
            s->is_global = 1;
            advance(p);
            if (!eat_punct(p, P_COMMA)) break;
        }
        if (!expect_punct(p, P_SEMI)) return 0;
        return 0;
    }

    struct type *base = 0;
    if (!parse_type(p, &base, 1)) {
        fail(p->tok.line, "expected a type");
        return 0;
    }
    if (is_punct(p, P_SEMI)) {      /* e.g. "struct P { int x; };" */
        advance(p);
        return 0;
    }

    struct node *first_item = 0;
    struct node *last_item = 0;
    int semi_done = 0;

    for (;;) {
        if (p->tok.kind != TK_IDENT) { fail(p->tok.line, "expected a name"); return 0; }
        const char *name = p->tok.text;
        int32_t dline = p->tok.line;
        advance(p);

        /* function declarator? */
        if (is_punct(p, P_LPAREN) && allow_func_def) {
            struct type *ft = base;

            int32_t fi = -1;
            for (int32_t i = 0; i < func_used; i++)
                if (name_eq(funcs[i].name, name)) { fi = i; break; }
            if (fi < 0) {
                if (func_used >= CI_MAX_FUNCS) { fail(dline, "too many functions"); return 0; }
                fi = func_used++;
                funcs[fi].name = name;
                funcs[fi].ret = ft;
                funcs[fi].nparams = 0;
                funcs[fi].body = 0;
                funcs[fi].frame_size = 0;
                funcs[fi].builtin = BI_NONE;
            }
            struct sym *fsym = new_sym(name, ft, dline);
            if (!fsym) return 0;
            fsym->is_global = 1;
            fsym->is_func = 1;
            fsym->func = fi;

            /* Parse the body in the function's own scope. Parameters get
             * frame slots first, so the body can name them. */
            int32_t saved_base = p->fn_scope_base;
            int32_t saved_frame = p->fn_frame;
            int saved_depth = p->scope_depth;
            int saved_in_fn = p->in_function;
            p->fn_scope_base = sym_used;
            p->fn_frame = 0;
            p->scope_depth = 0;
            p->in_function = 1;
            if (!push_scope(p)) return 0;
            advance(p);   /* consume '(' */

            int32_t nparams = 0;
            if (!is_punct(p, P_RPAREN)) {
                for (;;) {
                    /* "void" alone means no parameters. */
                    if (is_kw(p, K_VOID) && nparams == 0) {
                        advance(p);
                        if (!eat_punct(p, P_COMMA)) break;
                        continue;
                    }
                    struct type *pt = 0;
                    if (!parse_type(p, &pt, 1)) { fail(p->tok.line, "bad parameter type"); return 0; }
                    /* array parameters decay to pointers */
                    for (;;) {
                        if (eat_punct(p, P_STAR)) {
                            pt = pointer_to(pt);
                            if (!pt) return 0;
                        } else if (is_punct(p, P_LBRACK)) {
                            advance(p);
                            while (!is_punct(p, P_RBRACK)) {
                                if (p->tok.kind == TK_EOF) { fail(p->tok.line, "syntax error"); return 0; }
                                advance(p);
                            }
                            advance(p);
                            pt = pointer_to(pt);
                            if (!pt) return 0;
                        } else break;
                    }
                    const char *pname = 0;
                    if (p->tok.kind == TK_IDENT) { pname = p->tok.text; advance(p); }
                    if (nparams < CI_MAX_ARGS) {
                        p->fn_frame = align4(p->fn_frame);
                        funcs[fi].param_off[nparams] = p->fn_frame;
                        funcs[fi].nparams = nparams + 1;
                        p->fn_frame += type_size(pt) > 0 ? type_size(pt) : 4;
                        if (pname) {
                            struct sym *ps = new_sym(pname, pt, dline);
                            if (!ps) return 0;
                            ps->is_global = 0;
                            ps->scope = p->scope_depth;
                            ps->offset = funcs[fi].param_off[nparams];
                            funcs[fi].params[nparams] = ps;
                        } else {
                            funcs[fi].params[nparams] = 0;
                        }
                    }
                    nparams++;
                    if (!eat_punct(p, P_COMMA)) break;
                }
            }
            if (!expect_punct(p, P_RPAREN)) return 0;

            if (is_punct(p, P_SEMI)) {      /* prototype only */
                advance(p);
                pop_scope(p);
                p->fn_scope_base = saved_base;
                p->fn_frame = saved_frame;
                p->scope_depth = saved_depth;
                p->in_function = saved_in_fn;
                semi_done = 1;
                break;
            }
            if (!is_punct(p, P_LBRACE)) { fail(p->tok.line, "expected a function body"); return 0; }
            if (out_fn) *out_fn = funcs[fi];
            advance(p);   /* consume '{' */

            struct node *first = 0, *last = 0;
            while (!is_punct(p, P_RBRACE)) {
                if (p->tok.kind == TK_EOF) { fail(p->tok.line, "unterminated function"); return 0; }
                struct node *st = parse_statement(p);
                if (!st) return 0;
                if (!first) first = st; else last->next = st;
                last = st;
            }
            advance(p);   /* consume '}' */

            struct node *block = new_node(N_BLOCK, dline);
            if (!block) return 0;
            block->a = first;
            block->c = last;   /* tail of the statement chain */
            funcs[fi].body = block;
            funcs[fi].frame_size = p->fn_frame;
            if (out_fn) *out_fn = funcs[fi];

            pop_scope(p);
            p->fn_scope_base = saved_base;
            p->fn_frame = saved_frame;
            p->scope_depth = saved_depth;
            p->in_function = saved_in_fn;
            return 0;
        }

        /* plain variable */
        struct type *vt = base;
        for (;;) {
            if (eat_punct(p, P_STAR)) {
                vt = pointer_to(vt);
                if (!vt) return 0;
            } else if (is_punct(p, P_LBRACK)) {
                advance(p);
                int32_t len = 0;
                if (p->tok.kind == TK_NUM) { len = p->tok.num; advance(p); }
                if (len < 0) len = 0;
                if (!expect_punct(p, P_RBRACK)) return 0;
                vt = array_of(vt, len);
                if (!vt) return 0;
            } else {
                break;
            }
        }

        struct sym *s = declare_var(p, name, vt, dline, is_global);
        if (!s) return 0;

        struct node *init = 0;
        if (eat_punct(p, P_ASSIGN)) {
            if (is_punct(p, P_LBRACE)) {
                /* brace initialiser: only meaningful for arrays; copy the
                 * comma separated values in order. */
                advance(p);
                if (vt->kind == TY_ARRAY) {
                    int32_t width = elem_size(vt->base);
                    for (int32_t i = 0; i < vt->len; i++) {
                        if (is_punct(p, P_RBRACE)) break;
                        struct node *e = parse_assign(p);
                        if (!e) return 0;
                        struct node *item = new_node(N_DECL_ITEM, dline);
                        if (!item) return 0;
                        item->sym = s;      /* the array itself */
                        item->num = i;      /* element index */
                        item->a = e;
                        item->op = 1;       /* array element initialiser */
                        item->extra = width;
                        if (!first_item) first_item = item; else last_item->b = item;
                        last_item = item;
                        if (!eat_punct(p, P_COMMA)) break;
                    }
                } else {
                    struct node *e = parse_assign(p);
                    if (!e) return 0;
                    struct node *item = new_node(N_DECL_ITEM, dline);
                    if (!item) return 0;
                    item->a = e;
                    if (!first_item) first_item = item; else last_item->b = item;
                    last_item = item;
                }
                if (!expect_punct(p, P_RBRACE)) return 0;
            } else {
                init = parse_assign(p);
                if (!init) return 0;
            }
        }

        struct node *item = new_node(N_DECL_ITEM, dline);
        if (!item) return 0;
        item->sym = s;
        item->a = init;
        if (!first_item) first_item = item; else last_item->b = item;
        last_item = item;

        if (!eat_punct(p, P_COMMA)) break;
    }

    if (!semi_done && !expect_punct(p, P_SEMI)) return 0;

    if (is_global) {
        /* Global initialisers run before main, in the order they appear. */
        struct node *item = first_item;
        while (item) {
            if (item->sym && item->a && !item->op) {
                struct node *target = new_node(N_VAR, line);
                struct node *asn = new_node(N_ASSIGN, line);
                struct node *st = new_node(N_EXPR, line);
                if (!target || !asn || !st) return 0;
                target->sym = item->sym;
                target->type = item->sym->type;
                asn->op = P_ASSIGN;
                asn->a = target;
                asn->b = item->a;
                asn->type = item->sym->type;
                st->a = asn;
                if (global_init_used < CI_MAX_INIT)
                    global_inits[global_init_used++] = st;
            }
            item = item->b;
        }
        return 0;
    }

    struct node *decl = new_node(N_DECL, line);
    if (!decl) return 0;
    decl->a = first_item;
    return decl;
}

static struct node *parse_statement(struct parser *p) {
    int32_t line = p->tok.line;

    if (is_punct(p, P_SEMI)) { advance(p); return new_node(N_EMPTY, line); }

    if (is_punct(p, P_LBRACE)) {
        if (!push_scope(p)) return 0;
        advance(p);
        struct node *first = 0, *last = 0;
        while (!is_punct(p, P_RBRACE)) {
            if (p->tok.kind == TK_EOF) { fail(p->tok.line, "unterminated block"); return 0; }
            struct node *st = parse_statement(p);
            if (!st) return 0;
            if (!first) first = st; else last->next = st;
            last = st;
        }
        advance(p);
        struct node *block = new_node(N_BLOCK, line);
        if (!block) return 0;
        block->a = first;
        block->c = last;
        pop_scope(p);
        return block;
    }

    if (is_kw(p, K_IF)) {
        advance(p);
        if (!expect_punct(p, P_LPAREN)) return 0;
        struct node *cond = parse_expr(p);
        if (!cond) return 0;
        if (!expect_punct(p, P_RPAREN)) return 0;
        struct node *then_s = parse_statement(p);
        if (!then_s) return 0;
        struct node *else_s = 0;
        if (eat_kw(p, K_ELSE)) {
            else_s = parse_statement(p);
            if (!else_s) return 0;
        }
        struct node *n = new_node(N_IF, line);
        if (!n) return 0;
        n->a = cond; n->b = then_s; n->c = else_s;
        return n;
    }

    if (is_kw(p, K_WHILE)) {
        advance(p);
        if (!expect_punct(p, P_LPAREN)) return 0;
        struct node *cond = parse_expr(p);
        if (!cond) return 0;
        if (!expect_punct(p, P_RPAREN)) return 0;
        loop_depth++;
        struct node *body = parse_statement(p);
        loop_depth--;
        if (!body) return 0;
        struct node *n = new_node(N_WHILE, line);
        if (!n) return 0;
        n->a = cond; n->b = body;
        return n;
    }

    if (is_kw(p, K_DO)) {
        advance(p);
        loop_depth++;
        struct node *body = parse_statement(p);
        loop_depth--;
        if (!body) return 0;
        if (!eat_kw(p, K_WHILE)) { fail(p->tok.line, "expected 'while'"); return 0; }
        if (!expect_punct(p, P_LPAREN)) return 0;
        struct node *cond = parse_expr(p);
        if (!cond) return 0;
        if (!expect_punct(p, P_RPAREN)) return 0;
        if (!expect_punct(p, P_SEMI)) return 0;
        struct node *n = new_node(N_DO, line);
        if (!n) return 0;
        n->a = cond; n->b = body;
        return n;
    }

    if (is_kw(p, K_FOR)) {
        advance(p);
        if (!expect_punct(p, P_LPAREN)) return 0;
        if (!push_scope(p)) return 0;
        struct node *init = 0;
        if (!is_punct(p, P_SEMI)) {
            if (is_type_start(p)) {
                init = parse_declaration(p, 0, 0);
            } else {
                init = new_node(N_EXPR, p->tok.line);
                if (!init) return 0;
                init->a = parse_expr(p);
                if (!init->a) return 0;
                if (!expect_punct(p, P_SEMI)) return 0;
            }
        } else {
            advance(p);
        }
        struct node *cond = 0;
        if (!is_punct(p, P_SEMI)) {
            cond = parse_expr(p);
            if (!cond) return 0;
        }
        if (!expect_punct(p, P_SEMI)) return 0;
        struct node *step = 0;
        if (!is_punct(p, P_RPAREN)) {
            step = parse_expr(p);
            if (!step) return 0;
        }
        if (!expect_punct(p, P_RPAREN)) return 0;
        loop_depth++;
        struct node *body = parse_statement(p);
        loop_depth--;
        if (!body) return 0;
        struct node *n = new_node(N_FOR, line);
        if (!n) return 0;
        n->a = init; n->b = cond; n->c = step; n->d = body;
        pop_scope(p);
        return n;
    }

    if (is_kw(p, K_RETURN)) {
        advance(p);
        struct node *value = 0;
        if (!is_punct(p, P_SEMI)) {
            value = parse_expr(p);
            if (!value) return 0;
        }
        if (!expect_punct(p, P_SEMI)) return 0;
        struct node *n = new_node(N_RETURN, line);
        if (!n) return 0;
        n->a = value;
        return n;
    }

    if (is_kw(p, K_BREAK)) {
        advance(p);
        if (!expect_punct(p, P_SEMI)) return 0;
        if (loop_depth == 0) { fail(line, "break outside a loop"); return 0; }
        return new_node(N_BREAK, line);
    }

    if (is_kw(p, K_CONTINUE)) {
        advance(p);
        if (!expect_punct(p, P_SEMI)) return 0;
        if (loop_depth == 0) { fail(line, "continue outside a loop"); return 0; }
        return new_node(N_CONTINUE, line);
    }

    if (is_type_start(p)) {
        /* A declaration, unless it is a cast-like '(' type ')'. */
        if (is_punct(p, P_LPAREN) && is_type_name_tok(peek(p))) {
            struct node *e = parse_expr(p);
            if (!e) return 0;
            if (!expect_punct(p, P_SEMI)) return 0;
            struct node *st = new_node(N_EXPR, line);
            if (!st) return 0;
            st->a = e;
            return st;
        }
        return parse_declaration(p, 0, 0);
    }

    struct node *e = parse_expr(p);
    if (!e) return 0;
    if (!expect_punct(p, P_SEMI)) return 0;
    struct node *st = new_node(N_EXPR, line);
    if (!st) return 0;
    st->a = e;
    return st;
}

/* ------------------------------------------------------------------ */
/* Runtime                                                             */
/* ------------------------------------------------------------------ */

struct rt_state {
    uint32_t frame_base;   /* base of the running function's frame */
    int32_t frame_size;
};

static struct rt_state frames[CI_MAX_DEPTH];
static int depth;
static int aborted;

static void emit(char c) {
    if (out_putchar) out_putchar(c);
}

static void emit_str(const char *s, int32_t len) {
    for (int32_t i = 0; i < len; i++) emit(s[i]);
}

/* Copy from a guest string. */
static int32_t ci_strlen(Addr a) {
    int32_t n = 0;
    while (n < 4096 && mem_ok((Addr)(a + n), 1) && *(const char *)(a + n)) n++;
    return n;
}

static void ci_strcpy(Addr dst, Addr src) {
    int32_t i = 0;
    while (i < 4096 && mem_ok((Addr)(src + i), 1) && i < CI_MAX_STRLIT) {
        int32_t c = mem_read((Addr)(src + i), 1);
        mem_write((Addr)(dst + i), c, 1);
        if (!c) break;
        i++;
    }
}

static int eval(struct node *n);
static int exec_stmt(struct node *n, uint32_t frame_base);

static Addr var_addr(struct sym *s, uint32_t frame_base) {
    if (s->is_global) return (Addr)(global_mem + s->offset);
    return (Addr)(stack_mem + frame_base + s->offset);
}

static Value load_value(struct type *t, Addr addr) {
    if (!t) return mem_read(addr, 4);
    if (t->kind == TY_ARRAY) return (Value)addr;    /* arrays are addresses */
    if (t->kind == TY_STRUCT) return (Value)addr;   /* structs are addresses */
    return mem_read(addr, t->size);
}

static void store_value(struct type *t, Addr addr, Value v) {
    if (!t) { mem_write(addr, v, 4); return; }
    if (t->kind == TY_ARRAY || t->kind == TY_STRUCT) { mem_write(addr, v, 4); return; }
    mem_write(addr, v, t->size);
}

/* Evaluate an expression that must be assignable, yielding its address. */
static int eval_addr(struct node *n, uint32_t fb, Addr *out) {
    switch (n->kind) {
        case N_VAR:
            *out = var_addr(n->sym, fb);
            return 1;
        case N_CAST:
            /* a cast expression is not an lvalue, but a parenthesised one
             * already lost its parentheses, so this cannot happen. */
            return 0;
        case N_BIN:
            if (n->op == P_LBRACK) {
                Addr base = (Addr)eval(n->a);
                Value idx = eval(n->b);
                struct type *bt = n->a->type;
                if (bt && (bt->kind == TY_ARRAY || bt->kind == TY_PTR)) bt = bt->base;
                int32_t scale = bt ? bt->size : 1;
                if (scale < 1) scale = 1;
                *out = (Addr)(base + idx * scale);
                return 1;
            }
            if (n->op == P_DOT) {
                Addr base;
                if (!eval_addr(n->a, fb, &base)) return 0;
                *out = (Addr)(base + n->num);
                return 1;
            }
            return 0;
        case N_UN:
            if (n->op == P_STAR) {
                Value p = eval(n->a);
                *out = (Addr)p;
                return 1;
            }
            return 0;
        default:
            return 0;
    }
}

static uint8_t binop_for_assign(uint8_t assign_op) {
    switch (assign_op) {
        case P_ADD_ASSIGN: return P_PLUS;
        case P_SUB_ASSIGN: return P_MINUS;
        case P_MUL_ASSIGN: return P_STAR;
        case P_DIV_ASSIGN: return P_SLASH;
        case P_MOD_ASSIGN: return P_PERCENT;
        case P_AND_ASSIGN: return P_AMP;
        case P_OR_ASSIGN:  return P_PIPE;
        case P_XOR_ASSIGN: return P_CARET;
        case P_SHL_ASSIGN: return P_SHL;
        case P_SHR_ASSIGN: return P_SHR;
        default: return P_NONE;
    }
}

static Value apply_binop(uint8_t op, Value l, Value r, struct type *lt, struct type *rt) {
    int lptr = lt && (lt->kind == TY_PTR || lt->kind == TY_ARRAY);
    int rptr = rt && (rt->kind == TY_PTR || rt->kind == TY_ARRAY);

    /* Pointer arithmetic scales by the pointee size. */
    if (lptr && (op == P_PLUS || op == P_MINUS) && !rptr) {
        int32_t es = elem_size(lt);
        if (es < 1) es = 1;
        return (op == P_PLUS) ? (l + r * es) : (l - r * es);
    }
    if (rptr && op == P_PLUS && !lptr) {
        int32_t es = elem_size(rt);
        if (es < 1) es = 1;
        return l + r * es;
    }
    if (lptr && op == P_MINUS && rptr) {
        int32_t es = elem_size(lt);
        if (es < 1) es = 1;
        return (l - r) / es;
    }

    switch (op) {
        case P_PLUS:  return (Value)((int32_t)l + (int32_t)r);
        case P_MINUS: return (Value)((int32_t)l - (int32_t)r);
        case P_STAR:  return (Value)((int32_t)l * (int32_t)r);
        case P_SLASH:
            if (r == 0) { fail(0, "division by zero"); aborted = 1; return 0; }
            return (Value)((int32_t)l / (int32_t)r);
        case P_PERCENT:
            if (r == 0) { fail(0, "division by zero"); aborted = 1; return 0; }
            return (Value)((int32_t)l % (int32_t)r);
        case P_AMP:   return (Value)((int32_t)l & (int32_t)r);
        case P_PIPE:  return (Value)((int32_t)l | (int32_t)r);
        case P_CARET: return (Value)((int32_t)l ^ (int32_t)r);
        case P_SHL:   return (Value)((int32_t)l << ((int32_t)r & 31));
        case P_SHR:   return (Value)((int32_t)l >> ((int32_t)r & 31));
        case P_LT:    return (int32_t)l <  (int32_t)r;
        case P_GT:    return (int32_t)l >  (int32_t)r;
        case P_LE:    return (int32_t)l <= (int32_t)r;
        case P_GE:    return (int32_t)l >= (int32_t)r;
        case P_EQ:    return l == r;
        case P_NE:    return l != r;
        default:      return 0;
    }
}

/* printf supporting %d %i %u %x %X %o %c %s %p %% with flags '-', '0', '+',
 * ' ', a width, and a precision for strings. */
static int builtin_printf(Value *argv, int argc) {
    if (argc < 1) return 0;
    Addr fmt = (Addr)argv[0];
    int argi = 1;
    int32_t width = 0, prec = -1;
    int left = 0, zero = 0, plus = 0, space = 0;

    for (int32_t i = 0;; i++) {
        int c = mem_read((Addr)(fmt + i), 1);
        if (!c) break;
        if (c != '%') { emit((char)c); continue; }
        i++;
        left = zero = plus = space = 0;
        width = 0; prec = -1;
        for (;;) {
            c = mem_read((Addr)(fmt + i), 1);
            if (c == '-') { left = 1; i++; continue; }
            if (c == '0') { zero = 1; i++; continue; }
            if (c == '+') { plus = 1; i++; continue; }
            if (c == ' ') { space = 1; i++; continue; }
            if (c == '#') { i++; continue; }
            break;
        }
        while (c >= '0' && c <= '9') { width = width * 10 + (c - '0'); i++; c = mem_read((Addr)(fmt + i), 1); }
        if (c == '.') {
            i++;
            prec = 0;
            c = mem_read((Addr)(fmt + i), 1);
            while (c >= '0' && c <= '9') { prec = prec * 10 + (c - '0'); i++; c = mem_read((Addr)(fmt + i), 1); }
        }
        while (c == 'l' || c == 'h' || c == 'z') { i++; c = mem_read((Addr)(fmt + i), 1); }

        if (c == 0) break;
        if (c == '%') { emit('%'); continue; }

        /* Render into a scratch buffer, then pad. */
        char tmp[48];
        int32_t len = 0;
        char sign = 0;
        int is_string = (c == 's');
        int is_char = (c == 'c');

        if (is_string) {
            Addr sa = (argi < argc) ? (Addr)argv[argi++] : 0;
            int32_t slen = ci_strlen(sa);
            if (prec >= 0 && slen > prec) slen = prec;
            for (int32_t k = 0; k < slen && len < 47; k++) tmp[len++] = (char)mem_read((Addr)(sa + k), 1);
        } else if (is_char) {
            Value cv = (argi < argc) ? argv[argi++] : 0;
            tmp[len++] = (char)cv;
        } else {
            Value v = (argi < argc) ? argv[argi++] : 0;
            if (c == 'x' || c == 'X' || c == 'p') {
                uint32_t x = (uint32_t)v;
                char digits[16];
                const char *tbl = (c == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                if (c == 'p') { tmp[len++] = '0'; tmp[len++] = 'x'; }
                do { digits[len++] = tbl[x & 0xF]; x >>= 4; } while (x && len < 47);
                for (int32_t k = len - 1; k >= 0; k--) tmp[k] = digits[k];
            } else if (c == 'o') {
                uint32_t x = (uint32_t)v;
                do { tmp[len++] = (char)('0' + (x & 7)); x >>= 3; } while (x && len < 47);
                for (int32_t k = len - 1; k >= 0; k--) tmp[k] = tmp[k];
                /* reverse in place */
                for (int32_t a2 = 0, b2 = len - 1; a2 < b2; a2++, b2--) {
                    char t = tmp[a2]; tmp[a2] = tmp[b2]; tmp[b2] = t;
                }
            } else {
                int32_t x = (int32_t)v;
                if (x < 0) { sign = '-'; x = -x; }
                else if (plus) sign = '+';
                else if (space) sign = ' ';
                char digits[16];
                int n = 0;
                if (x == 0) digits[n++] = '0';
                while (x > 0) { digits[n++] = (char)('0' + x % 10); x /= 10; }
                for (int32_t k = n - 1; k >= 0; k--) tmp[len++] = digits[k];
            }
        }

        int32_t pad = width - len - (sign ? 1 : 0);
        if (pad < 0) pad = 0;
        if (!left && !zero) for (int32_t k = 0; k < pad; k++) emit(' ');
        if (sign) emit(sign);
        if (!left && zero) for (int32_t k = 0; k < pad; k++) emit('0');
        for (int32_t k = 0; k < len; k++) emit(tmp[k]);
        if (left) for (int32_t k = 0; k < pad; k++) emit(' ');
    }
    return 0;
}

static Value call_builtin(int which, Value *argv, int argc, int32_t line) {
    switch (which) {
        case BI_PRINTF:
            return builtin_printf(argv, argc);
        case BI_PUTS: {
            if (argc < 1) return 0;
            emit_str((const char *)(Addr)argv[0], ci_strlen((Addr)argv[0]));
            emit('\n');
            return 1;
        }
        case BI_PUTCHAR:
            if (argc < 1) return 0;
            emit((char)argv[0]);
            return (int8_t)argv[0];
        case BI_STRLEN:
            return argc < 1 ? 0 : ci_strlen((Addr)argv[0]);
        case BI_STRCMP: {
            if (argc < 2) return 0;
            Addr a = (Addr)argv[0], b = (Addr)argv[1];
            for (int32_t i = 0; i < 1024; i++) {
                int ca = mem_read((Addr)(a + i), 1);
                int cb = mem_read((Addr)(b + i), 1);
                if (ca != cb) return ca - cb;
                if (!ca) return 0;
            }
            return 0;
        }
        case BI_STRNCMP: {
            if (argc < 3) return 0;
            Addr a = (Addr)argv[0], b = (Addr)argv[1];
            int32_t n = argv[2];
            for (int32_t i = 0; i < n && i < 1024; i++) {
                int ca = mem_read((Addr)(a + i), 1);
                int cb = mem_read((Addr)(b + i), 1);
                if (ca != cb) return ca - cb;
                if (!ca) return 0;
            }
            return 0;
        }
        case BI_STRCPY:
            if (argc < 2) return 0;
            ci_strcpy((Addr)argv[0], (Addr)argv[1]);
            return argv[0];
        case BI_STRCAT: {
            if (argc < 2) return 0;
            Addr a = (Addr)argv[0], b = (Addr)argv[1];
            int32_t alen = ci_strlen(a);
            ci_strcpy((Addr)(a + alen), b);
            return argv[0];
        }
        case BI_MEMSET: {
            if (argc < 3) return 0;
            Addr d = (Addr)argv[0];
            int32_t v = argv[1];
            int32_t n = argv[2];
            for (int32_t i = 0; i < n; i++) mem_write((Addr)(d + i), v, 1);
            return argv[0];
        }
        case BI_ABS: {
            if (argc < 1) return 0;
            int32_t v = (int32_t)argv[0];
            return v < 0 ? -v : v;
        }
        case BI_EXIT:
            aborted = 1;
            if (argc >= 1) ret_value = argv[0];
            return 0;
        default:
            fail(line, "unknown builtin");
            aborted = 1;
            return 0;
    }
}

static Value call_function(int32_t fi, Value *argv, int argc, int32_t line) {
    struct func *f = &funcs[fi];
    if (f->builtin) return call_builtin(f->builtin, argv, argc, line);
    if (!f->body) { fail(line, "function has no body"); aborted = 1; return 0; }
    if (depth >= CI_MAX_DEPTH - 1) { fail(line, "stack overflow (too much recursion)"); aborted = 1; return 0; }

    int32_t saved_sp = stack_sp;
    int32_t base = frame_alloc(f->frame_size);
    if (base < 0) { fail(line, "stack overflow"); aborted = 1; return 0; }

    int ncopy = argc < f->nparams ? argc : f->nparams;
    for (int i = 0; i < ncopy; i++) {
        struct type *pt = f->params[i] ? f->params[i]->type : ty_int;
        mem_write((Addr)(stack_mem + base + f->param_off[i]), argv[i], type_size(pt) > 0 ? type_size(pt) : 4);
    }

    depth++;
    frames[depth].frame_base = (uint32_t)base;
    frames[depth].frame_size = f->frame_size;
    Value saved_ret = ret_value;
    ret_value = 0;
    exec_stmt(f->body, (uint32_t)base);
    Value r = ret_value;
    ret_value = saved_ret;
    depth--;
    stack_sp = saved_sp;
    return r;
}

static int find_func_by_name(const char *name) {
    for (int32_t i = 0; i < func_used; i++)
        if (name_eq(funcs[i].name, name)) return (int)i;
    return -1;
}

static Value eval(struct node *n) {
    if (!n || aborted) return 0;
    uint32_t fb = (depth > 0) ? frames[depth].frame_base : 0;

    switch (n->kind) {
        case N_NUM:  return n->num;
        case N_STR:  return n->num;
        case N_VAR:  return load_value(n->sym->type, var_addr(n->sym, fb));
        case N_EMPTY: return 0;

        case N_CAST: {
            Value v = eval(n->a);
            int32_t sz = type_size(n->type);
            if (n->type->kind == TY_CHAR) return (int8_t)v;
            if (n->type->kind == TY_INT) return (int32_t)v;
            if (n->type->kind == TY_PTR || n->type->kind == TY_ARRAY) return v;
            (void)sz;
            return v;
        }

        case N_BIN: {
            if (n->op == P_LBRACK) {
                Addr a;
                if (!eval_addr(n, fb, &a)) { fail(n->line, "not indexable"); aborted = 1; return 0; }
                return load_value(n->type, a);
            }
            if (n->op == P_DOT) {
                Addr a;
                if (!eval_addr(n, fb, &a)) { fail(n->line, "not a struct"); aborted = 1; return 0; }
                return load_value(n->type, a);
            }
            if (n->op == P_ANDAND) {
                Value l = eval(n->a);
                if (!l) return 0;
                return eval(n->b) ? 1 : 0;
            }
            if (n->op == P_OROR) {
                Value l = eval(n->a);
                if (l) return 1;
                return eval(n->b) ? 1 : 0;
            }
            Value l = eval(n->a);
            Value r = eval(n->b);
            return apply_binop(n->op, l, r, n->a->type, n->b->type);
        }

        case N_UN: {
            if (n->op == P_AMP) {
                Addr a;
                if (!eval_addr(n->a, fb, &a)) { fail(n->line, "cannot take the address of this"); aborted = 1; return 0; }
                return (Value)a;
            }
            if (n->op == P_STAR) {
                Value p = eval(n->a);
                return load_value(n->type, (Addr)p);
            }
            Value v = eval(n->a);
            switch (n->op) {
                case P_MINUS: return (Value)(-(int32_t)v);
                case P_BANG:  return v ? 0 : 1;
                case P_TILDE: return (Value)(~(int32_t)v);
                case P_PLUS:  return v;
                default: return v;
            }
        }

        case N_COND: {
            Value c = eval(n->a);
            return c ? eval(n->b) : eval(n->c);
        }

        case N_COMMA:
            eval(n->a);
            return eval(n->b);

        case N_ASSIGN: {
            Addr a;
            if (!eval_addr(n->a, fb, &a)) { fail(n->line, "not assignable"); aborted = 1; return 0; }
            if (n->op == P_ASSIGN) {
                Value v = eval(n->b);
                store_value(n->a->type, a, v);
                return v;
            }
            uint8_t op = binop_for_assign(n->op);
            Value cur = load_value(n->a->type, a);
            Value rhs = eval(n->b);
            Value v = apply_binop(op, cur, rhs, n->a->type, n->b->type);
            store_value(n->a->type, a, v);
            return v;
        }

        case N_PREINC: {
            Addr a;
            if (!eval_addr(n->a, fb, &a)) { fail(n->line, "not incrementable"); aborted = 1; return 0; }
            Value cur = load_value(n->a->type, a);
            Value v = (n->op == P_INC) ? cur + 1 : cur - 1;
            store_value(n->a->type, a, v);
            return v;
        }

        case N_POSTINC: {
            Addr a;
            if (!eval_addr(n->a, fb, &a)) { fail(n->line, "not incrementable"); aborted = 1; return 0; }
            Value cur = load_value(n->a->type, a);
            Value v = (n->op == P_INC) ? cur + 1 : cur - 1;
            store_value(n->a->type, a, v);
            return cur;
        }

        case N_CALL: {
            const char *name = (const char *)(Addr)n->c->num;
            int fi = find_func_by_name(name);
            if (fi < 0) { fail(n->line, "undefined function"); aborted = 1; return 0; }
            Value argv[CI_MAX_ARGS];
            int argc = n->extra;
            int k = 0;
            for (struct node *a = n->a; a && k < argc; a = a->b, k++)
                argv[k] = eval(a->a);
            return call_function(fi, argv, k, n->line);
        }

        default:
            fail(n->line, "internal: bad expression node");
            aborted = 1;
            return 0;
    }
}

static void exec_decl_item(struct node *item, uint32_t fb) {
    if (item->op == 1) {   /* array element initialiser: {1,2,3} */
        if (!item->sym) return;
        Addr base = var_addr(item->sym, fb);
        Value v = eval(item->a);
        mem_write((Addr)(base + item->num * (int32_t)item->extra), v, item->extra);
        return;
    }
    if (!item->sym || !item->a) return;
    Addr a = var_addr(item->sym, fb);
    Value v = eval(item->a);
    store_value(item->sym->type, a, v);
}

static int exec_stmt(struct node *n, uint32_t fb) {
    if (!n || aborted) return FLOW_NORMAL;
    switch (n->kind) {
        case N_EMPTY:
        case N_DECL:
            if (n->kind == N_DECL) {
                for (struct node *it = n->a; it; it = it->b) exec_decl_item(it, fb);
            }
            return FLOW_NORMAL;

        case N_EXPR:
            eval(n->a);
            return FLOW_NORMAL;

        case N_BLOCK: {
            for (struct node *s = n->a; s; s = s->next) {
                int flow = exec_stmt(s, fb);
                if (flow != FLOW_NORMAL) return flow;
                if (aborted) return FLOW_NORMAL;
            }
            return FLOW_NORMAL;
        }

        case N_IF: {
            if (eval(n->a)) return exec_stmt(n->b, fb);
            if (n->c) return exec_stmt(n->c, fb);
            return FLOW_NORMAL;
        }

        case N_WHILE:
            while (!aborted) {
                if (!eval(n->a)) break;
                int flow = exec_stmt(n->b, fb);
                if (aborted) break;
                if (flow == FLOW_BREAK) break;
                if (flow == FLOW_RETURN) return FLOW_RETURN;
            }
            return FLOW_NORMAL;

        case N_DO:
            for (;;) {
                int flow = exec_stmt(n->b, fb);
                if (aborted) break;
                if (flow == FLOW_BREAK) break;
                if (flow == FLOW_RETURN) return FLOW_RETURN;
                if (!eval(n->a)) break;
            }
            return FLOW_NORMAL;

        case N_FOR: {
            if (n->a) {
                if (n->a->kind == N_DECL) {
                    for (struct node *it = n->a->a; it; it = it->b) exec_decl_item(it, fb);
                } else {
                    eval(n->a->a);
                }
            }
            for (;;) {
                if (aborted) break;
                if (n->b && !eval(n->b)) break;
                int flow = exec_stmt(n->d, fb);
                if (aborted) break;
                if (flow == FLOW_BREAK) break;
                if (flow == FLOW_RETURN) return FLOW_RETURN;
                if (n->c) eval(n->c);
            }
            return FLOW_NORMAL;
        }

        case N_RETURN:
            ret_value = n->a ? eval(n->a) : 0;
            return FLOW_RETURN;

        case N_BREAK:    return FLOW_BREAK;
        case N_CONTINUE: return FLOW_CONTINUE;

        default: {
            /* A bare expression used as a statement. */
            eval(n);
            return FLOW_NORMAL;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

/* Create the base types. Must run before anything is parsed. */
static void init_types(void) {
    type_used = 0;
    ty_void = new_type(TY_VOID, 0);
    ty_char = new_type(TY_CHAR, 0);
    ty_int = new_type(TY_INT, 0);
    ty_char->size = 1;
    ty_int->size = 4;
    ty_void->size = 0;
    ty_char->base = 0;
    ty_int->base = 0;
}

static void ci_reset(void) {
    ci_error[0] = 0;
    init_types();
    node_used = 0;
    sym_used = 0;
    type_used = 0;
    struct_used = 0;
    for (int32_t i = 0; i < CI_MAX_STRUCTS; i++) structs[i].count = 0;
    func_used = 0;
    global_used = 0;
    global_init_used = 0;
    name_used = 0;
    str_used = 0;
    loop_depth = 0;
    depth = 0;
    stack_sp = 0;
    aborted = 0;
    ret_value = 0;
    for (int32_t i = 0; i < CI_MAX_GLOBAL_MEM; i++) global_mem[i] = 0;
    for (int32_t i = 0; i < CI_MAX_STACK_MEM; i++) stack_mem[i] = 0;
}

static struct lexer top_lex;
static struct parser top_parser;
static struct node *top_block;

static int parse_program(const char *src) {
    top_lex.p = src;
    top_lex.line = 1;

    init_types();   /* idempotent: a fresh type table for this run */
    top_parser.lx = top_lex;
    top_parser.tok = (struct token){0};
    top_parser.ahead = (struct token){0};
    top_parser.has_ahead = 0;
    top_parser.scope_depth = 0;
    top_parser.fn_scope_base = 0;
    top_parser.fn_frame = 0;
    top_parser.top_frame = 0;
    top_parser.in_function = 0;
    struct parser *pp = &top_parser;

    if (!advance(pp)) { fail(1, "empty program"); return 0; }

    struct node *first = 0, *last = 0;
    while (pp->tok.kind != TK_EOF) {
        if (is_type_start(pp)) {
            if (parse_declaration(pp, 1, 0)) return 0;   /* a function body */
        } else {
            struct node *st = parse_statement(pp);
            if (!st) return 0;
            if (!first) first = st; else last->next = st;
            last = st;
        }
    }

    struct node *block = new_node(N_BLOCK, 1);
    if (!block) return 0;
    block->a = first;
    block->c = last;
    top_block = block;
    return 1;
}

void ci_set_putchar(ci_putchar_fn fn) {
    out_putchar = fn;
}

int ci_check(const char *src) {
    ci_reset();
    top_block = 0;
    if (!src) { fail(0, "no source"); return -1; }
    return parse_program(src) ? 0 : -1;
}

int32_t ci_run(const char *src) {
    ci_reset();
    top_block = 0;

    if (!src) { fail(0, "no source"); return -1; }

    if (!parse_program(src)) return -1;

    /* Register the standard library, and mark the ones the program defines
     * itself as ordinary functions. */
    for (int32_t i = 0; i < builtin_count; i++) {
        if (find_func_by_name(builtin_names[i]) >= 0) continue;
        if (func_used >= CI_MAX_FUNCS) { fail(0, "too many functions"); return -1; }
        int32_t fi = func_used++;
        funcs[fi].name = builtin_names[i];
        funcs[fi].ret = ty_int;
        funcs[fi].body = 0;
        funcs[fi].frame_size = 0;
        funcs[fi].nparams = 0;
        funcs[fi].builtin = builtin_ids[i];
    }

    depth = 0;
    stack_sp = 0;

    for (int32_t i = 0; i < global_init_used; i++) {
        exec_stmt(global_inits[i], 0);
        if (aborted) return -1;
    }
    exec_stmt(top_block, 0);
    if (aborted) return -1;

    int32_t main_i = find_func_by_name("main");
    if (main_i >= 0) {
        Value result = call_function(main_i, 0, 0, 0);
        if (aborted && ci_error[0]) return -1;
        return (int32_t)result;
    }
    return 0;
}

const char *ci_last_error(void) {
    return ci_error[0] ? ci_error : 0;
}
