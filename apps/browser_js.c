#include <solis/apps/browser_js.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * Solis Browser JavaScript
 * ------------------------
 * A deliberately small, integer-only JavaScript subset for local demo pages.
 *
 * Supported: var/let/const, functions and closures, if/else, while, for,
 * return/break, arithmetic and comparison, strings, booleans, arrays,
 * document.getElementById/querySelector, textContent/innerHTML, element
 * attributes, inline style properties, addEventListener/onclick and
 * document.write.  There is no eval, timers, fetch, regex or floating point.
 *
 * All storage is static.  A script gets a small AST pool, a bounded number
 * of environments and an instruction budget so a bad page cannot lock the
 * browser forever.
 */

#define JS_MAX_NODES   4096
#define JS_MAX_ENVS    80
#define JS_MAX_VARS    40
#define JS_MAX_FUNCS   80
#define JS_MAX_EVENTS  96
#define JS_MAX_STRINGS 45056
#define JS_MAX_ARRAYS  24
#define JS_MAX_ARRAY   32
#define JS_MAX_PARAMS  6
#define JS_MAX_ARGS    6
#define JS_MAX_STEPS   250000
#define JS_MAX_TOKEN   160
#define JS_MAX_SOURCE  32768

enum {
    JS_T_EOF = 0, JS_T_NUM, JS_T_STR, JS_T_NAME, JS_T_PUNC, JS_T_KEYWORD
};

enum {
    JS_K_EMPTY = 0, JS_K_NUM, JS_K_STR, JS_K_BOOL, JS_K_NULL, JS_K_UNDEF,
    JS_K_THIS, JS_K_IDENT, JS_K_MEMBER, JS_K_INDEX, JS_K_CALL, JS_K_BINARY,
    JS_K_UNARY, JS_K_ASSIGN, JS_K_COND, JS_K_VAR_LIST, JS_K_VAR, JS_K_IF,
    JS_K_WHILE, JS_K_FOR, JS_K_BLOCK, JS_K_FUNC, JS_K_RETURN, JS_K_BREAK,
    JS_K_EXPR, JS_K_ARRAY
};

enum {
    JS_OP_NONE = 0, JS_OP_ADD, JS_OP_SUB, JS_OP_MUL, JS_OP_DIV, JS_OP_MOD,
    JS_OP_EQ, JS_OP_NE, JS_OP_LT, JS_OP_LE, JS_OP_GT, JS_OP_GE,
    JS_OP_AND, JS_OP_OR, JS_OP_NOT, JS_OP_NEG, JS_OP_POS, JS_OP_TYPEOF,
    JS_OP_ASSIGN, JS_OP_ADD_ASSIGN, JS_OP_SUB_ASSIGN, JS_OP_INC, JS_OP_DEC
};

enum {
    JS_V_UNDEF = 0, JS_V_NULL, JS_V_NUM, JS_V_BOOL, JS_V_STR, JS_V_OBJ,
    JS_V_FUNC, JS_V_NATIVE
};

enum {
    JS_N_GET_ID = 1, JS_N_QUERY, JS_N_WRITE, JS_N_ADD_EVENT, JS_N_ALERT,
    JS_N_LOG, JS_N_GET_ATTR, JS_N_SET_ATTR,
    JS_N_QUERY_ALL, JS_N_CREATE, JS_N_APPEND, JS_N_REMOVE, JS_N_PARENT,
    JS_N_CHILDREN, JS_N_NEXT, JS_N_CLASS_ADD, JS_N_CLASS_REMOVE,
    JS_N_CLASS_TOGGLE, JS_N_CLASS_CONTAINS,
    JS_N_STRING_UPPER, JS_N_STRING_LOWER, JS_N_STRING_TRIM,
    JS_N_STRING_INDEX, JS_N_STRING_SUBSTR, JS_N_STRING_CHAR,
    JS_N_STRING_INCLUDES, JS_N_STRING_STARTS, JS_N_STRING_ENDS,
    JS_N_ARRAY_PUSH, JS_N_ARRAY_POP, JS_N_ARRAY_JOIN, JS_N_ARRAY_INDEX,
    JS_N_ARRAY_FOREACH, JS_N_MATH_FLOOR, JS_N_MATH_ABS, JS_N_MATH_MIN,
    JS_N_MATH_MAX, JS_N_MATH_RANDOM, JS_N_PARSE_INT, JS_N_TIMEOUT
};

#define JS_OBJ_GLOBAL   (-2)
#define JS_OBJ_DOCUMENT (-3)
#define JS_OBJ_WINDOW   (-4)
#define JS_OBJ_CONSOLE  (-5)
#define JS_OBJ_STYLE    10000
#define JS_OBJ_ARRAY    11000
#define JS_OBJ_CLASS    12000
#define JS_OBJ_MATH     14000

struct js_token {
    uint8_t kind;
    int line;
    int64_t num;
    char text[JS_MAX_TOKEN];
};

struct js_node {
    uint8_t kind;
    uint8_t op;
    int64_t num;
    int16_t a, b, c, d;
    int16_t next;
    int16_t text;
};

struct js_value {
    uint8_t type;
    uint8_t boolean;
    int16_t str;
    int16_t obj;
    int16_t func;
    int16_t native;
    int64_t num;
};

struct js_func {
    int16_t params[JS_MAX_PARAMS];
    int16_t body;
    int16_t env;
    int16_t name;
    uint8_t nparams;
};

struct js_var {
    int16_t name;
    struct js_value value;
};

struct js_env {
    int16_t parent;
    int16_t count;
    struct js_var vars[JS_MAX_VARS];
};

struct js_event {
    int16_t node;
    int16_t func;
    int16_t source;
    uint8_t used;
};

struct js_array {
    int count;
    struct js_value items[JS_MAX_ARRAY];
};

struct js_timer {
    struct js_value func;
    uint8_t used;
};

static struct js_timer js_timers[16];
static int js_timer_used;

static struct js_node js_nodes[JS_MAX_NODES];
static int js_node_used;

static char js_strings[JS_MAX_STRINGS];
static int js_string_used;

static struct js_env js_envs[JS_MAX_ENVS];
static int js_env_used;
static int js_global_env;

static struct js_func js_funcs[JS_MAX_FUNCS];
static int js_func_used;

static struct js_event js_events[JS_MAX_EVENTS];
static struct js_array js_arrays[JS_MAX_ARRAYS];
static int js_array_used;

static struct js_token js_tok;
static const char *js_source;
static int js_source_len;
static int js_source_pos;
static int js_line;
static int js_parse_failed;
static char js_error[192];

static struct browser_js_dom js_dom;
static int js_this_obj;
static int js_this_str = -1;
static int js_flow;
static struct js_value js_return_value;
static int js_steps;

enum { JS_FLOW_OK = 0, JS_FLOW_RETURN, JS_FLOW_BREAK, JS_FLOW_ERROR };

/* ------------------------------------------------------------------ */
/* Strings and small helpers                                            */
/* ------------------------------------------------------------------ */

static int js_strlen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int js_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int js_mem_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int js_str_eq(int a, int b) {
    if (a == b) return 1;
    if (a < 0 || b < 0 || a >= js_string_used || b >= js_string_used) return 0;
    return js_streq(js_strings + a, js_strings + b);
}

static int js_store(const char *text, int len) {
    if (!text) len = 0;
    if (len < 0) len = 0;
    if (js_string_used + len + 1 > JS_MAX_STRINGS) {
        if (!js_error[0]) {
            const char *msg = "out of script string space";
            int i = 0;
            while (msg[i] && i < 180) { js_error[i] = msg[i]; i++; }
            js_error[i] = '\0';
        }
        return 0;
    }
    int off = js_string_used;
    for (int i = 0; i < len; i++) js_strings[js_string_used + i] = text[i];
    js_strings[js_string_used + len] = '\0';
    js_string_used += len + 1;
    return off;
}

static int js_store_z(const char *text) {
    return js_store(text, js_strlen(text));
}

static const char *js_str(int off) {
    if (off < 0 || off >= js_string_used) return "";
    return js_strings + off;
}

static void js_set_error(const char *message, int line) {
    if (js_error[0]) return;
    int n = 0;
    while (message[n] && n < 150) { js_error[n] = message[n]; n++; }
    if (line > 0 && n < 180) {
        js_error[n++] = ' ';
        js_error[n++] = '(';
        js_error[n++] = 'l';
        js_error[n++] = 'i';
        js_error[n++] = 'n';
        js_error[n++] = 'e';
        js_error[n++] = ' ';
        char digits[12];
        int dn = 0;
        int v = line;
        if (v <= 0) digits[dn++] = '0';
        while (v > 0 && dn < 11) { digits[dn++] = (char)('0' + v % 10); v /= 10; }
        while (dn > 0 && n < 180) js_error[n++] = digits[--dn];
        js_error[n++] = ')';
    }
    js_error[n] = '\0';
    js_flow = JS_FLOW_ERROR;
}

static void js_set_parse_error(const char *message) {
    js_parse_failed = 1;
    js_set_error(message, js_line);
}

static int js_parse_ok(void) {
    return !js_parse_failed && !js_error[0];
}

static int js_is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '$';
}

static int js_is_name(char c) {
    return js_is_name_start(c) || (c >= '0' && c <= '9');
}

static int js_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int js_keyword(const char *s) {
    static const char *const words[] = {
        "var", "let", "const", "function", "if", "else", "while", "for",
        "return", "break", "true", "false", "null", "undefined", "this",
        "typeof"
    };
    for (int i = 0; i < (int)(sizeof(words) / sizeof(words[0])); i++)
        if (js_streq(s, words[i])) return 1;
    return 0;
}

static int js_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Lexer                                                               */
/* ------------------------------------------------------------------ */

static void js_next_token(void) {
    js_tok.kind = JS_T_EOF;
    js_tok.num = 0;
    js_tok.text[0] = '\0';

    while (js_source_pos < js_source_len) {
        char c = js_source[js_source_pos];
        if (c == '\n') {
            js_line++;
            js_source_pos++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            js_source_pos++;
        } else if (c == '/' && js_source_pos + 1 < js_source_len &&
                   js_source[js_source_pos + 1] == '/') {
            js_source_pos += 2;
            while (js_source_pos < js_source_len &&
                   js_source[js_source_pos] != '\n') js_source_pos++;
        } else if (c == '/' && js_source_pos + 1 < js_source_len &&
                   js_source[js_source_pos + 1] == '*') {
            js_source_pos += 2;
            while (js_source_pos + 1 < js_source_len &&
                   !(js_source[js_source_pos] == '*' &&
                     js_source[js_source_pos + 1] == '/')) {
                if (js_source[js_source_pos] == '\n') js_line++;
                js_source_pos++;
            }
            if (js_source_pos + 1 < js_source_len) js_source_pos += 2;
        } else {
            break;
        }
    }

    if (js_source_pos >= js_source_len) {
        js_tok.kind = JS_T_EOF;
        return;
    }

    char c = js_source[js_source_pos];
    if (js_is_name_start(c)) {
        int n = 0;
        while (js_source_pos < js_source_len &&
               js_is_name(js_source[js_source_pos])) {
            if (n < JS_MAX_TOKEN - 1)
                js_tok.text[n++] = js_source[js_source_pos];
            js_source_pos++;
        }
        js_tok.text[n] = '\0';
        js_tok.kind = js_keyword(js_tok.text) ? JS_T_KEYWORD : JS_T_NAME;
        return;
    }

    if (js_is_digit(c)) {
        int64_t value = 0;
        while (js_source_pos < js_source_len &&
               js_is_digit(js_source[js_source_pos])) {
            value = value * 10 + (js_source[js_source_pos] - '0');
            if (value > 1000000000LL) value = 1000000000LL;
            js_source_pos++;
        }
        js_tok.kind = JS_T_NUM;
        js_tok.num = value;
        return;
    }

    if (c == '"' || c == '\'') {
        char quote = c;
        int n = 0;
        js_source_pos++;
        while (js_source_pos < js_source_len) {
            char ch = js_source[js_source_pos++];
            if (ch == quote) break;
            if (ch == '\n') js_line++;
            if (ch == '\\' && js_source_pos < js_source_len) {
                char esc = js_source[js_source_pos++];
                if (esc == 'n') ch = '\n';
                else if (esc == 't') ch = '\t';
                else if (esc == 'r') ch = '\r';
                else if (esc == '0') ch = '\0';
                else if (esc == 'x' && js_source_pos + 1 < js_source_len) {
                    int hi = js_hex_digit(js_source[js_source_pos]);
                    int lo = js_hex_digit(js_source[js_source_pos + 1]);
                    if (hi >= 0 && lo >= 0) {
                        ch = (char)((hi << 4) | lo);
                        js_source_pos += 2;
                    }
                } else ch = esc;
            }
            if (n < JS_MAX_TOKEN - 1) js_tok.text[n++] = ch;
        }
        js_tok.text[n] = '\0';
        js_tok.kind = JS_T_STR;
        return;
    }

    js_tok.kind = JS_T_PUNC;
    int n = 0;
    js_tok.text[n++] = js_source[js_source_pos++];
    if (js_source_pos < js_source_len) {
        char d = js_source[js_source_pos];
        char e = (js_source_pos + 1 < js_source_len)
                     ? js_source[js_source_pos + 1] : '\0';
        if ((c == '=' && d == '=') || (c == '!' && d == '=') ||
            (c == '<' && d == '=') || (c == '>' && d == '=') ||
            (c == '&' && d == '&') || (c == '|' && d == '|') ||
            (c == '+' && d == '+') || (c == '-' && d == '-') ||
            (c == '+' && d == '=') || (c == '-' && d == '=') ||
            (c == '*' && d == '=') || (c == '/' && d == '=')) {
            js_tok.text[n++] = d;
            js_source_pos++;
            if ((c == '=' && d == '=' && e == '=') ||
                (c == '!' && d == '=' && e == '=')) {
                js_tok.text[n++] = e;
                js_source_pos++;
            }
        }
    }
    js_tok.text[n] = '\0';
}

static void js_advance(void) {
    js_next_token();
}

static int js_accept(const char *text) {
    if (js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, text)) {
        js_advance();
        return 1;
    }
    return 0;
}

static int js_expect(const char *text) {
    if (js_accept(text)) return 1;
    char msg[64];
    int n = 0;
    const char *prefix = "expected ";
    while (*prefix && n < 50) msg[n++] = *prefix++;
    while (*text && n < 58) msg[n++] = *text++;
    msg[n] = '\0';
    js_set_parse_error(msg);
    return 0;
}

static int js_expect_name(int *off) {
    if (js_tok.kind == JS_T_NAME || js_tok.kind == JS_T_KEYWORD) {
        if (js_keyword(js_tok.text)) {
            js_set_parse_error("reserved word cannot be a variable name");
            return 0;
        }
        *off = js_store_z(js_tok.text);
        js_advance();
        return 1;
    }
    js_set_parse_error("expected a name");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

static int js_new_node(int kind) {
    if (js_node_used >= JS_MAX_NODES) {
        js_set_parse_error("script is too large");
        return -1;
    }
    int id = js_node_used++;
    js_nodes[id].kind = (uint8_t)kind;
    js_nodes[id].op = 0;
    js_nodes[id].num = 0;
    js_nodes[id].a = -1;
    js_nodes[id].b = -1;
    js_nodes[id].c = -1;
    js_nodes[id].d = -1;
    js_nodes[id].next = -1;
    js_nodes[id].text = 0;
    return id;
}

static int js_parse_expression(void);
static int js_parse_statement(void);

static int js_parse_block(void) {
    int block = js_new_node(JS_K_BLOCK);
    if (block < 0) return -1;
    if (!js_expect("{")) return -1;
    int first = -1;
    int last = -1;
    while (js_parse_ok() && js_tok.kind != JS_T_EOF &&
           !(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, "}"))) {
        int stmt = js_parse_statement();
        if (stmt < 0) break;
        if (first < 0) first = stmt;
        else js_nodes[last].next = (int16_t)stmt;
        last = stmt;
    }
    if (!js_parse_ok()) return -1;
    if (!js_expect("}")) return -1;
    js_nodes[block].a = (int16_t)first;
    return block;
}

static int js_parse_var_list(void) {
    int list = js_new_node(JS_K_VAR_LIST);
    if (list < 0) return -1;
    int first = -1;
    int last = -1;
    for (;;) {
        int name = 0;
        if (!js_expect_name(&name)) return -1;
        int init = -1;
        if (js_accept("=")) {
            init = js_parse_expression();
            if (init < 0) return -1;
        }
        int var = js_new_node(JS_K_VAR);
        if (var < 0) return -1;
        js_nodes[var].text = (int16_t)name;
        js_nodes[var].a = (int16_t)init;
        if (first < 0) first = var;
        else js_nodes[last].next = (int16_t)var;
        last = var;
        if (!js_accept(",")) break;
    }
    js_nodes[list].a = (int16_t)first;
    return list;
}

static int js_parse_function(int declaration) {
    int fn = js_new_node(JS_K_FUNC);
    if (fn < 0) return -1;
    int name = 0;
    if (declaration) {
        if (!js_expect_name(&name)) return -1;
        js_nodes[fn].text = (int16_t)name;
    } else if (js_tok.kind == JS_T_NAME) {
        js_nodes[fn].text = (int16_t)js_store_z(js_tok.text);
        js_advance();
    }
    if (!js_expect("(")) return -1;
    int first_param = -1;
    int last_param = -1;
    if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ")"))) {
        for (;;) {
            int p = 0;
            if (!js_expect_name(&p)) return -1;
            int pn = js_new_node(JS_K_IDENT);
            if (pn < 0) return -1;
            js_nodes[pn].text = (int16_t)p;
            if (first_param < 0) first_param = pn;
            else js_nodes[last_param].next = (int16_t)pn;
            last_param = pn;
            if (!js_accept(",")) break;
        }
    }
    if (!js_expect(")")) return -1;
    int body = js_parse_block();
    if (body < 0) return -1;
    js_nodes[fn].a = (int16_t)first_param;
    js_nodes[fn].b = (int16_t)body;
    return fn;
}

static int js_parse_statement(void) {
    if (!js_parse_ok()) return -1;

    if (js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, "{"))
        return js_parse_block();

    if (js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ";")) {
        js_advance();
        return js_new_node(JS_K_EMPTY);
    }

    if (js_tok.kind == JS_T_KEYWORD) {
        if (js_streq(js_tok.text, "var") || js_streq(js_tok.text, "let") ||
            js_streq(js_tok.text, "const")) {
            js_advance();
            return js_parse_var_list();
        }
        if (js_streq(js_tok.text, "function")) {
            js_advance();
            return js_parse_function(1);
        }
        if (js_streq(js_tok.text, "if")) {
            js_advance();
            int node = js_new_node(JS_K_IF);
            if (node < 0) return -1;
            if (!js_expect("(")) return -1;
            int cond = js_parse_expression();
            if (cond < 0 || !js_expect(")")) return -1;
            int then_part = js_parse_statement();
            if (then_part < 0) return -1;
            int else_part = -1;
            if (js_tok.kind == JS_T_KEYWORD && js_streq(js_tok.text, "else")) {
                js_advance();
                else_part = js_parse_statement();
                if (else_part < 0) return -1;
            }
            js_nodes[node].a = (int16_t)cond;
            js_nodes[node].b = (int16_t)then_part;
            js_nodes[node].c = (int16_t)else_part;
            return node;
        }
        if (js_streq(js_tok.text, "while")) {
            js_advance();
            int node = js_new_node(JS_K_WHILE);
            if (node < 0) return -1;
            if (!js_expect("(")) return -1;
            int cond = js_parse_expression();
            if (cond < 0 || !js_expect(")")) return -1;
            int body = js_parse_statement();
            if (body < 0) return -1;
            js_nodes[node].a = (int16_t)cond;
            js_nodes[node].b = (int16_t)body;
            return node;
        }
        if (js_streq(js_tok.text, "for")) {
            js_advance();
            int node = js_new_node(JS_K_FOR);
            if (node < 0) return -1;
            if (!js_expect("(")) return -1;
            int init = -1;
            if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ";"))) {
                if (js_tok.kind == JS_T_KEYWORD &&
                    (js_streq(js_tok.text, "var") ||
                     js_streq(js_tok.text, "let") ||
                     js_streq(js_tok.text, "const"))) {
                    js_advance();
                    init = js_parse_var_list();
                } else {
                    init = js_parse_expression();
                }
                if (init < 0) return -1;
            }
            if (!js_expect(";")) return -1;
            int cond = -1;
            if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ";"))) {
                cond = js_parse_expression();
                if (cond < 0) return -1;
            }
            if (!js_expect(";")) return -1;
            int post = -1;
            if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ")"))) {
                post = js_parse_expression();
                if (post < 0) return -1;
            }
            if (!js_expect(")")) return -1;
            int body = js_parse_statement();
            if (body < 0) return -1;
            js_nodes[node].a = (int16_t)init;
            js_nodes[node].b = (int16_t)cond;
            js_nodes[node].c = (int16_t)post;
            js_nodes[node].d = (int16_t)body;
            return node;
        }
        if (js_streq(js_tok.text, "return")) {
            js_advance();
            int node = js_new_node(JS_K_RETURN);
            if (node < 0) return -1;
            if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ";")) &&
                !(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, "}")) &&
                js_tok.kind != JS_T_EOF) {
                int value = js_parse_expression();
                if (value < 0) return -1;
                js_nodes[node].a = (int16_t)value;
            }
            js_accept(";");
            return node;
        }
        if (js_streq(js_tok.text, "break")) {
            js_advance();
            js_accept(";");
            return js_new_node(JS_K_BREAK);
        }
    }

    int expr = js_parse_expression();
    if (expr < 0) return -1;
    js_accept(";");
    int node = js_new_node(JS_K_EXPR);
    if (node < 0) return -1;
    js_nodes[node].a = (int16_t)expr;
    return node;
}

static int js_parse_args(void) {
    int first = -1;
    int last = -1;
    if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, ")"))) {
        for (int count = 0; count < JS_MAX_ARGS; count++) {
            int arg = js_parse_expression();
            if (arg < 0) return -1;
            if (first < 0) first = arg;
            else js_nodes[last].next = (int16_t)arg;
            last = arg;
            if (!js_accept(",")) break;
        }
    }
    return first;
}

static int js_parse_primary(void) {
    if (js_tok.kind == JS_T_NUM) {
        int node = js_new_node(JS_K_NUM);
        if (node < 0) return -1;
        js_nodes[node].num = js_tok.num;
        js_advance();
        return node;
    }
    if (js_tok.kind == JS_T_STR) {
        int node = js_new_node(JS_K_STR);
        if (node < 0) return -1;
        js_nodes[node].text = (int16_t)js_store_z(js_tok.text);
        js_advance();
        return node;
    }
    if (js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, "(")) {
        js_advance();
        int node = js_parse_expression();
        if (node < 0 || !js_expect(")")) return -1;
        return node;
    }
    if (js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, "[")) {
        js_advance();
        int node = js_new_node(JS_K_ARRAY);
        if (node < 0) return -1;
        int first = -1;
        int last = -1;
        if (!(js_tok.kind == JS_T_PUNC && js_streq(js_tok.text, "]"))) {
            for (;;) {
                int item = js_parse_expression();
                if (item < 0) return -1;
                if (first < 0) first = item;
                else js_nodes[last].next = (int16_t)item;
                last = item;
                if (!js_accept(",")) break;
            }
        }
        if (!js_expect("]")) return -1;
        js_nodes[node].a = (int16_t)first;
        return node;
    }
    if (js_tok.kind == JS_T_KEYWORD) {
        if (js_streq(js_tok.text, "function")) {
            js_advance();
            return js_parse_function(0);
        }
        int kind = -1;
        if (js_streq(js_tok.text, "true")) kind = JS_K_BOOL;
        else if (js_streq(js_tok.text, "false")) kind = JS_K_BOOL;
        else if (js_streq(js_tok.text, "null")) kind = JS_K_NULL;
        else if (js_streq(js_tok.text, "undefined")) kind = JS_K_UNDEF;
        else if (js_streq(js_tok.text, "this")) kind = JS_K_THIS;
        if (kind >= 0) {
            int node = js_new_node(kind);
            if (node < 0) return -1;
            if (kind == JS_K_BOOL) js_nodes[node].num =
                js_streq(js_tok.text, "true") ? 1 : 0;
            js_advance();
            return node;
        }
    }
    if (js_tok.kind == JS_T_NAME) {
        int node = js_new_node(JS_K_IDENT);
        if (node < 0) return -1;
        js_nodes[node].text = (int16_t)js_store_z(js_tok.text);
        js_advance();
        return node;
    }
    js_set_parse_error("expected an expression");
    return -1;
}

static int js_parse_postfix(void) {
    int left = js_parse_primary();
    if (left < 0) return -1;
    for (;;) {
        if (js_accept(".")) {
            if (js_tok.kind != JS_T_NAME && js_tok.kind != JS_T_KEYWORD) {
                js_set_parse_error("expected a property name");
                return -1;
            }
            int member = js_new_node(JS_K_MEMBER);
            if (member < 0) return -1;
            js_nodes[member].a = (int16_t)left;
            js_nodes[member].text = (int16_t)js_store_z(js_tok.text);
            js_advance();
            left = member;
        } else if (js_accept("[")) {
            int index = js_new_node(JS_K_INDEX);
            if (index < 0) return -1;
            int expr = js_parse_expression();
            if (expr < 0 || !js_expect("]")) return -1;
            js_nodes[index].a = (int16_t)left;
            js_nodes[index].b = (int16_t)expr;
            left = index;
        } else if (js_accept("(")) {
            int call = js_new_node(JS_K_CALL);
            if (call < 0) return -1;
            int args = js_parse_args();
            if (args < 0 && !js_parse_ok()) return -1;
            if (!js_expect(")")) return -1;
            js_nodes[call].a = (int16_t)left;
            js_nodes[call].b = (int16_t)args;
            left = call;
        } else {
            break;
        }
    }
    return left;
}

static int js_parse_unary(void) {
    if (js_tok.kind == JS_T_PUNC &&
        (js_streq(js_tok.text, "!") || js_streq(js_tok.text, "-") ||
         js_streq(js_tok.text, "+") || js_streq(js_tok.text, "++") ||
         js_streq(js_tok.text, "--"))) {
        int op = JS_OP_NONE;
        if (js_streq(js_tok.text, "!")) op = JS_OP_NOT;
        else if (js_streq(js_tok.text, "-")) op = JS_OP_NEG;
        else if (js_streq(js_tok.text, "+")) op = JS_OP_POS;
        else if (js_streq(js_tok.text, "++")) op = JS_OP_INC;
        else op = JS_OP_DEC;
        js_advance();
        int value = js_parse_unary();
        if (value < 0) return -1;
        int node = js_new_node(JS_K_UNARY);
        if (node < 0) return -1;
        js_nodes[node].op = (uint8_t)op;
        js_nodes[node].a = (int16_t)value;
        return node;
    }
    if (js_tok.kind == JS_T_KEYWORD && js_streq(js_tok.text, "typeof")) {
        js_advance();
        int value = js_parse_unary();
        if (value < 0) return -1;
        int node = js_new_node(JS_K_UNARY);
        if (node < 0) return -1;
        js_nodes[node].op = JS_OP_TYPEOF;
        js_nodes[node].a = (int16_t)value;
        return node;
    }
    return js_parse_postfix();
}

static int js_parse_binary(int min_prec) {
    int left = js_parse_unary();
    if (left < 0) return -1;
    for (;;) {
        int prec = 0;
        int op = JS_OP_NONE;
        if (js_tok.kind == JS_T_PUNC) {
            if (js_streq(js_tok.text, "||")) { prec = 1; op = JS_OP_OR; }
            else if (js_streq(js_tok.text, "&&")) { prec = 2; op = JS_OP_AND; }
            else if (js_streq(js_tok.text, "==") ||
                     js_streq(js_tok.text, "===")) { prec = 3; op = JS_OP_EQ; }
            else if (js_streq(js_tok.text, "!=") ||
                     js_streq(js_tok.text, "!==")) { prec = 3; op = JS_OP_NE; }
            else if (js_streq(js_tok.text, "<")) { prec = 4; op = JS_OP_LT; }
            else if (js_streq(js_tok.text, "<=")) { prec = 4; op = JS_OP_LE; }
            else if (js_streq(js_tok.text, ">")) { prec = 4; op = JS_OP_GT; }
            else if (js_streq(js_tok.text, ">=")) { prec = 4; op = JS_OP_GE; }
            else if (js_streq(js_tok.text, "+")) { prec = 5; op = JS_OP_ADD; }
            else if (js_streq(js_tok.text, "-")) { prec = 5; op = JS_OP_SUB; }
            else if (js_streq(js_tok.text, "*")) { prec = 6; op = JS_OP_MUL; }
            else if (js_streq(js_tok.text, "/")) { prec = 6; op = JS_OP_DIV; }
            else if (js_streq(js_tok.text, "%")) { prec = 6; op = JS_OP_MOD; }
        }
        if (prec < min_prec) break;
        js_advance();
        int right = js_parse_binary(prec + 1);
        if (right < 0) return -1;
        int node = js_new_node(JS_K_BINARY);
        if (node < 0) return -1;
        js_nodes[node].op = (uint8_t)op;
        js_nodes[node].a = (int16_t)left;
        js_nodes[node].b = (int16_t)right;
        left = node;
    }
    return left;
}

static int js_parse_expression(void) {
    int left = js_parse_binary(1);
    if (left < 0) return -1;
    if (js_tok.kind == JS_T_PUNC &&
        (js_streq(js_tok.text, "=") || js_streq(js_tok.text, "+=") ||
         js_streq(js_tok.text, "-=") || js_streq(js_tok.text, "*=") ||
         js_streq(js_tok.text, "/="))) {
        int op = JS_OP_ASSIGN;
        if (js_streq(js_tok.text, "+=")) op = JS_OP_ADD_ASSIGN;
        else if (js_streq(js_tok.text, "-=")) op = JS_OP_SUB_ASSIGN;
        js_advance();
        int right = js_parse_expression();
        if (right < 0) return -1;
        int node = js_new_node(JS_K_ASSIGN);
        if (node < 0) return -1;
        js_nodes[node].op = (uint8_t)op;
        js_nodes[node].a = (int16_t)left;
        js_nodes[node].b = (int16_t)right;
        return node;
    }
    if (js_accept("?")) {
        int yes = js_parse_expression();
        if (yes < 0 || !js_expect(":")) return -1;
        int no = js_parse_expression();
        if (no < 0) return -1;
        int node = js_new_node(JS_K_COND);
        if (node < 0) return -1;
        js_nodes[node].a = (int16_t)left;
        js_nodes[node].b = (int16_t)yes;
        js_nodes[node].c = (int16_t)no;
        return node;
    }
    return left;
}

static int js_parse_program(void) {
    int first = -1;
    int last = -1;
    js_advance();
    while (js_parse_ok() && js_tok.kind != JS_T_EOF) {
        int stmt = js_parse_statement();
        if (stmt < 0) break;
        if (first < 0) first = stmt;
        else js_nodes[last].next = (int16_t)stmt;
        last = stmt;
    }
    return first;
}

/* ------------------------------------------------------------------ */
/* Values and environments                                             */
/* ------------------------------------------------------------------ */

static struct js_value js_make_undef(void) {
    struct js_value v;
    v.type = JS_V_UNDEF;
    v.boolean = 0;
    v.str = 0;
    v.obj = 0;
    v.func = 0;
    v.native = 0;
    v.num = 0;
    return v;
}

static struct js_value js_make_null(void) {
    struct js_value v = js_make_undef();
    v.type = JS_V_NULL;
    return v;
}

static struct js_value js_make_num(int64_t n) {
    struct js_value v = js_make_undef();
    v.type = JS_V_NUM;
    v.num = n;
    return v;
}

static struct js_value js_make_bool(int b) {
    struct js_value v = js_make_undef();
    v.type = JS_V_BOOL;
    v.boolean = b ? 1 : 0;
    return v;
}

static struct js_value js_make_str(int off) {
    struct js_value v = js_make_undef();
    v.type = JS_V_STR;
    v.str = (int16_t)off;
    return v;
}

static struct js_value js_make_obj(int obj) {
    struct js_value v = js_make_undef();
    v.type = JS_V_OBJ;
    v.obj = (int16_t)obj;
    return v;
}

static struct js_value js_make_func(int func) {
    struct js_value v = js_make_undef();
    v.type = JS_V_FUNC;
    v.func = (int16_t)func;
    return v;
}

static struct js_value js_make_native(int native) {
    struct js_value v = js_make_undef();
    v.type = JS_V_NATIVE;
    v.native = (int16_t)native;
    return v;
}

static int js_new_env(int parent) {
    if (js_env_used >= JS_MAX_ENVS) {
        js_set_error("out of script scopes", js_line);
        return -1;
    }
    int id = js_env_used++;
    js_envs[id].parent = (int16_t)parent;
    js_envs[id].count = 0;
    return id;
}

static int js_env_define(int env, int name, struct js_value value) {
    if (env < 0 || env >= js_env_used) return 0;
    for (int i = 0; i < js_envs[env].count; i++) {
        if (js_str_eq(js_envs[env].vars[i].name, name)) {
            js_envs[env].vars[i].value = value;
            return 1;
        }
    }
    if (js_envs[env].count >= JS_MAX_VARS) {
        js_set_error("too many script variables", js_line);
        return 0;
    }
    int i = js_envs[env].count++;
    js_envs[env].vars[i].name = (int16_t)name;
    js_envs[env].vars[i].value = value;
    return 1;
}

static int js_env_get(int env, int name, struct js_value *out) {
    for (int e = env; e >= 0 && e < js_env_used; e = js_envs[e].parent) {
        for (int i = 0; i < js_envs[e].count; i++) {
            if (js_str_eq(js_envs[e].vars[i].name, name)) {
                *out = js_envs[e].vars[i].value;
                return 1;
            }
        }
    }
    *out = js_make_undef();
    return 0;
}

static int js_env_set(int env, int name, struct js_value value) {
    for (int e = env; e >= 0 && e < js_env_used; e = js_envs[e].parent) {
        for (int i = 0; i < js_envs[e].count; i++) {
            if (js_str_eq(js_envs[e].vars[i].name, name)) {
                js_envs[e].vars[i].value = value;
                return 1;
            }
        }
    }
    return js_env_define(env, name, value);
}

static const char *js_type_name(const struct js_value *v) {
    switch (v->type) {
        case JS_V_UNDEF: return "undefined";
        case JS_V_NULL: return "object";
        case JS_V_NUM: return "number";
        case JS_V_BOOL: return "boolean";
        case JS_V_STR: return "string";
        case JS_V_FUNC: return "function";
        case JS_V_NATIVE: return "function";
        default: return "object";
    }
}

static int64_t js_to_number(const struct js_value *v) {
    if (v->type == JS_V_NUM) return v->num;
    if (v->type == JS_V_BOOL) return v->boolean;
    if (v->type == JS_V_STR) {
        const char *s = js_str(v->str);
        int sign = 1;
        if (*s == '-') { sign = -1; s++; }
        int64_t value = 0;
        int any = 0;
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + (*s - '0');
            if (value > 1000000000LL) value = 1000000000LL;
            s++;
            any = 1;
        }
        return any ? value * sign : 0;
    }
    return 0;
}

static int js_to_string(const struct js_value *v, int *out_off) {
    if (v->type == JS_V_STR) {
        *out_off = v->str;
        return 1;
    }
    if (v->type == JS_V_NUM) {
        char buf[32];
        int n = 0;
        int64_t value = v->num;
        if (value < 0) {
            buf[n++] = '-';
            value = -value;
        }
        char digits[24];
        int dn = 0;
        if (value == 0) digits[dn++] = '0';
        while (value > 0 && dn < 23) {
            digits[dn++] = (char)('0' + value % 10);
            value /= 10;
        }
        while (dn > 0) buf[n++] = digits[--dn];
        buf[n] = '\0';
        *out_off = js_store(buf, n);
        return 1;
    }
    if (v->type == JS_V_BOOL) {
        *out_off = js_store_z(v->boolean ? "true" : "false");
        return 1;
    }
    if (v->type == JS_V_NULL) {
        *out_off = js_store_z("null");
        return 1;
    }
    if (v->type == JS_V_UNDEF) {
        *out_off = js_store_z("undefined");
        return 1;
    }
    if (v->type == JS_V_OBJ) {
        *out_off = js_store_z("[object Object]");
        return 1;
    }
    *out_off = js_store_z("function");
    return 1;
}

static int js_truthy(const struct js_value *v) {
    if (v->type == JS_V_UNDEF || v->type == JS_V_NULL) return 0;
    if (v->type == JS_V_BOOL) return v->boolean;
    if (v->type == JS_V_NUM) return v->num != 0;
    if (v->type == JS_V_STR) return js_str(v->str)[0] != '\0';
    return 1;
}

static int js_new_array(void) {
    if (js_array_used >= JS_MAX_ARRAYS) {
        js_set_error("out of arrays", js_line);
        return -1;
    }
    int id = js_array_used++;
    js_arrays[id].count = 0;
    return id;
}

/* ------------------------------------------------------------------ */
/* Function creation and calls                                         */
/* ------------------------------------------------------------------ */

static int js_make_function(int fn_node, int env) {
    if (js_func_used >= JS_MAX_FUNCS) {
        js_set_error("too many functions", js_line);
        return -1;
    }
    int id = js_func_used++;
    js_funcs[id].nparams = 0;
    js_funcs[id].body = js_nodes[fn_node].b;
    js_funcs[id].env = (int16_t)env;
    js_funcs[id].name = js_nodes[fn_node].text;
    int p = js_nodes[fn_node].a;
    while (p >= 0 && js_funcs[id].nparams < JS_MAX_PARAMS) {
        js_funcs[id].params[js_funcs[id].nparams++] = js_nodes[p].text;
        p = js_nodes[p].next;
    }
    return id;
}

static int js_node_array_from(int first, int element_only) {
    int arr = js_new_array();
    if (arr < 0) return -1;
    int count = 0;
    for (int n = first; n >= 0 && count < JS_MAX_ARRAY;
         n = js_dom.next_sibling ? js_dom.next_sibling(n, 1) : -1) {
        if (element_only && js_dom.tag_name && !js_dom.tag_name(n)[0]) continue;
        js_arrays[arr].items[count++] = js_make_obj(n);
    }
    js_arrays[arr].count = count;
    return JS_OBJ_ARRAY + arr;
}

static int js_class_has(int node, const char *name) {
    const char *classes = js_dom.get_attr ? js_dom.get_attr(node, "class") : "";
    int i = 0;
    while (classes[i]) {
        while (classes[i] == ' ' || classes[i] == '\t') i++;
        int start = i;
        while (classes[i] && classes[i] != ' ' && classes[i] != '\t') i++;
        int n = i - start;
        int same = 1;
        for (int j = 0; j < n; j++) {
            if (!name[j] || classes[start + j] != name[j]) { same = 0; break; }
        }
        if (same && !name[n]) return 1;
    }
    return 0;
}

static void js_class_change(int node, const char *name, int add) {
    const char *classes = js_dom.get_attr ? js_dom.get_attr(node, "class") : "";
    char out[256];
    int oi = 0;
    int i = 0;
    while (classes[i]) {
        while (classes[i] == ' ' || classes[i] == '\t') i++;
        int start = i;
        while (classes[i] && classes[i] != ' ' && classes[i] != '\t') i++;
        int n = i - start;
        int same = n == js_strlen(name);
        for (int j = 0; same && j < n; j++)
            if (classes[start + j] != name[j]) same = 0;
        if (same) continue;
        if (oi && oi < (int)sizeof(out) - 1) out[oi++] = ' ';
        for (int j = 0; j < n && oi < (int)sizeof(out) - 1; j++)
            out[oi++] = classes[start + j];
    }
    if (add && name[0] && !js_class_has(node, name)) {
        if (oi && oi < (int)sizeof(out) - 1) out[oi++] = ' ';
        for (int j = 0; name[j] && oi < (int)sizeof(out) - 1; j++)
            out[oi++] = name[j];
    }
    out[oi] = '\0';
    if (js_dom.set_attr) js_dom.set_attr(node, "class", out);
}

static int js_timer_add(struct js_value func) {
    for (int i = 0; i < 16; i++) {
        if (!js_timers[i].used) {
            js_timers[i].used = 1;
            js_timers[i].func = func;
            js_timer_used++;
            return 1;
        }
    }
    return 0;
}

static int js_eval(int node, int env, struct js_value *out);
static int js_exec_node(int node, int env);
static int js_exec_block(int first, int env);
static int js_call_function(int func, int this_obj, struct js_value *args,
                            int argc, struct js_value *ret);

static void js_hoist(int first, int env) {
    for (int n = first; n >= 0; n = js_nodes[n].next) {
        if (js_nodes[n].kind == JS_K_FUNC) {
            int func = js_make_function(n, env);
            if (func < 0) return;
            js_env_define(env, js_nodes[n].text, js_make_func(func));
        }
    }
}

static int js_call_value(struct js_value callee, int this_obj,
                         struct js_value *args, int argc,
                         struct js_value *ret) {
    if (callee.type == JS_V_FUNC)
        return js_call_function(callee.func, this_obj, args, argc, ret);
    if (callee.type != JS_V_NATIVE) {
        js_set_error("value is not callable", js_line);
        return 0;
    }

    *ret = js_make_undef();
    switch (callee.native) {
        case JS_N_GET_ID: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            *ret = js_make_obj(js_dom.find_id ? js_dom.find_id(js_str(sid)) : -1);
            if (ret->obj < 0) *ret = js_make_null();
            return 1;
        }
        case JS_N_QUERY: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            *ret = js_make_obj(js_dom.find_selector
                                   ? js_dom.find_selector(js_str(sid)) : -1);
            if (ret->obj < 0) *ret = js_make_null();
            return 1;
        }
        case JS_N_WRITE: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            if (js_dom.append_html) js_dom.append_html(js_str(sid));
            return 1;
        }
        case JS_N_ADD_EVENT: {
            int type = 0;
            if (argc < 2 || !js_to_string(&args[0], &type)) return 0;
            if (!js_streq(js_str(type), "click")) return 1;
            if (this_obj < 0) return 0;
            int slot = -1;
            for (int i = 0; i < JS_MAX_EVENTS; i++) {
                if (!js_events[i].used) { slot = i; break; }
            }
            if (slot < 0) { js_set_error("too many event handlers", js_line); return 0; }
            js_events[slot].used = 1;
            js_events[slot].node = (int16_t)this_obj;
            js_events[slot].source = 0;
            if (args[1].type == JS_V_FUNC) js_events[slot].func = args[1].func;
            else { js_events[slot].func = -1; js_events[slot].source = args[1].str; }
            return 1;
        }
        case JS_N_ALERT:
        case JS_N_LOG: {
            int sid = 0;
            if (argc > 0) js_to_string(&args[0], &sid);
            if (js_dom.status) js_dom.status(js_str(sid));
            return 1;
        }
        case JS_N_GET_ATTR: {
            int sid = 0;
            if (this_obj < 0 || argc < 1 || !js_to_string(&args[0], &sid)) {
                *ret = js_make_null();
                return 1;
            }
            const char *value = js_dom.get_attr
                                    ? js_dom.get_attr(this_obj, js_str(sid)) : "";
            *ret = js_make_str(js_store_z(value ? value : ""));
            return 1;
        }
        case JS_N_SET_ATTR: {
            int sid = 0, vid = 0;
            if (this_obj < 0 || argc < 2 || !js_to_string(&args[0], &sid) ||
                !js_to_string(&args[1], &vid)) return 0;
            if (js_dom.set_attr)
                js_dom.set_attr(this_obj, js_str(sid), js_str(vid));
            return 1;
        }
        case JS_N_QUERY_ALL: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            int nodes[JS_MAX_ARRAY];
            int count = js_dom.query_all ? js_dom.query_all(js_str(sid), nodes,
                                                             JS_MAX_ARRAY) : 0;
            int arr = js_new_array();
            if (arr < 0) return 0;
            for (int i = 0; i < count; i++)
                js_arrays[arr].items[i] = js_make_obj(nodes[i]);
            js_arrays[arr].count = count;
            *ret = js_make_obj(JS_OBJ_ARRAY + arr);
            return 1;
        }
        case JS_N_CREATE: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            int node = js_dom.create_element ? js_dom.create_element(js_str(sid)) : -1;
            *ret = node >= 0 ? js_make_obj(node) : js_make_null();
            return 1;
        }
        case JS_N_APPEND: {
            if (this_obj < 0 || argc < 1 || args[0].type != JS_V_OBJ) return 0;
            int node = args[0].obj;
            if (js_dom.append_child && js_dom.append_child(this_obj, node)) {
                *ret = args[0];
            }
            return 1;
        }
        case JS_N_REMOVE: {
            if (argc > 0 && args[0].type == JS_V_OBJ && js_dom.remove_child) {
                js_dom.remove_child(this_obj, args[0].obj);
            } else if (this_obj >= 0 && js_dom.parent_node) {
                int parent = js_dom.parent_node(this_obj);
                if (parent >= 0 && js_dom.remove_child)
                    js_dom.remove_child(parent, this_obj);
            }
            return 1;
        }
        case JS_N_PARENT: {
            int node = js_dom.parent_node ? js_dom.parent_node(this_obj) : -1;
            *ret = node >= 0 ? js_make_obj(node) : js_make_null();
            return 1;
        }
        case JS_N_CHILDREN: {
            int first = js_dom.first_child ? js_dom.first_child(this_obj, 1) : -1;
            if (first < 0) {
                int arr = js_new_array();
                *ret = arr >= 0 ? js_make_obj(JS_OBJ_ARRAY + arr) : js_make_null();
            } else {
                *ret = js_make_obj(js_node_array_from(first, 1));
            }
            return 1;
        }
        case JS_N_NEXT: {
            int node = js_dom.next_sibling ? js_dom.next_sibling(this_obj, 1) : -1;
            *ret = node >= 0 ? js_make_obj(node) : js_make_null();
            return 1;
        }
        case JS_N_CLASS_ADD:
        case JS_N_CLASS_REMOVE:
        case JS_N_CLASS_TOGGLE:
        case JS_N_CLASS_CONTAINS: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            int node = this_obj - JS_OBJ_CLASS;
            const char *name = js_str(sid);
            int has = js_class_has(node, name);
            if (callee.native == JS_N_CLASS_CONTAINS) {
                *ret = js_make_bool(has);
            } else if (callee.native == JS_N_CLASS_TOGGLE) {
                js_class_change(node, name, !has);
                *ret = js_make_bool(!has);
            } else {
                js_class_change(node, name, callee.native == JS_N_CLASS_ADD);
            }
            return 1;
        }
        case JS_N_STRING_UPPER:
        case JS_N_STRING_LOWER: {
            const char *s = js_str(js_this_str);
            char buf[512];
            int n = 0;
            while (s[n] && n < (int)sizeof(buf) - 1) {
                char c = s[n];
                if (callee.native == JS_N_STRING_UPPER && c >= 'a' && c <= 'z')
                    c = (char)(c - 32);
                if (callee.native == JS_N_STRING_LOWER && c >= 'A' && c <= 'Z')
                    c = (char)(c + 32);
                buf[n++] = c;
            }
            buf[n] = '\0';
            *ret = js_make_str(js_store(buf, n));
            return 1;
        }
        case JS_N_STRING_TRIM: {
            const char *s = js_str(js_this_str);
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            int n = js_strlen(s);
            while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                             s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
            *ret = js_make_str(js_store(s, n));
            return 1;
        }
        case JS_N_STRING_INDEX: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            const char *s = js_str(js_this_str);
            const char *needle = js_str(sid);
            int found = -1;
            for (int i = 0; s[i]; i++) {
                int j = 0;
                while (needle[j] && s[i + j] == needle[j]) j++;
                if (!needle[j]) { found = i; break; }
            }
            *ret = js_make_num(found);
            return 1;
        }
        case JS_N_STRING_SUBSTR: {
            const char *s = js_str(js_this_str);
            int len = js_strlen(s);
            int start = argc > 0 ? (int)js_to_number(&args[0]) : 0;
            int end = argc > 1 ? (int)js_to_number(&args[1]) : len;
            if (start < 0) start = 0;
            if (end < start) end = start;
            if (start > len) start = len;
            if (end > len) end = len;
            *ret = js_make_str(js_store(s + start, end - start));
            return 1;
        }
        case JS_N_STRING_CHAR: {
            const char *s = js_str(js_this_str);
            int i = argc > 0 ? (int)js_to_number(&args[0]) : 0;
            *ret = i >= 0 && i < js_strlen(s) ? js_make_str(js_store(s + i, 1))
                                              : js_make_str(js_store_z(""));
            return 1;
        }
        case JS_N_STRING_INCLUDES:
        case JS_N_STRING_STARTS:
        case JS_N_STRING_ENDS: {
            int sid = 0;
            if (argc < 1 || !js_to_string(&args[0], &sid)) return 0;
            const char *s = js_str(js_this_str);
            const char *needle = js_str(sid);
            int ns = js_strlen(s), nn = js_strlen(needle);
            int ok = 0;
            if (callee.native == JS_N_STRING_STARTS)
                ok = nn <= ns && js_mem_eq(s, needle, nn);
            else if (callee.native == JS_N_STRING_ENDS)
                ok = nn <= ns && js_mem_eq(s + ns - nn, needle, nn);
            else {
                for (int i = 0; i + nn <= ns && !ok; i++)
                    if (js_mem_eq(s + i, needle, nn)) ok = 1;
            }
            *ret = js_make_bool(ok);
            return 1;
        }
        case JS_N_ARRAY_PUSH: {
            int ai = this_obj - JS_OBJ_ARRAY;
            if (ai < 0 || ai >= JS_MAX_ARRAYS) return 0;
            for (int i = 0; i < argc && js_arrays[ai].count < JS_MAX_ARRAY; i++)
                js_arrays[ai].items[js_arrays[ai].count++] = args[i];
            *ret = js_make_num(js_arrays[ai].count);
            return 1;
        }
        case JS_N_ARRAY_POP: {
            int ai = this_obj - JS_OBJ_ARRAY;
            if (ai < 0 || ai >= JS_MAX_ARRAYS) return 0;
            if (js_arrays[ai].count <= 0) { *ret = js_make_undef(); return 1; }
            *ret = js_arrays[ai].items[--js_arrays[ai].count];
            return 1;
        }
        case JS_N_ARRAY_JOIN: {
            int ai = this_obj - JS_OBJ_ARRAY;
            if (ai < 0 || ai >= JS_MAX_ARRAYS) return 0;
            char sep = ',';
            if (argc > 0) { int sid = 0; js_to_string(&args[0], &sid); sep = js_str(sid)[0]; }
            char out[1024];
            int oi = 0;
            for (int i = 0; i < js_arrays[ai].count; i++) {
                int sid = 0;
                js_to_string(&js_arrays[ai].items[i], &sid);
                const char *s = js_str(sid);
                if (i && oi < (int)sizeof(out) - 1) out[oi++] = sep;
                while (*s && oi < (int)sizeof(out) - 1) out[oi++] = *s++;
            }
            out[oi] = '\0';
            *ret = js_make_str(js_store(out, oi));
            return 1;
        }
        case JS_N_ARRAY_INDEX: {
            int ai = this_obj - JS_OBJ_ARRAY;
            if (ai < 0 || ai >= JS_MAX_ARRAYS || argc < 1) return 0;
            int sid = 0;
            js_to_string(&args[0], &sid);
            for (int i = 0; i < js_arrays[ai].count; i++) {
                if (args[0].type == JS_V_STR) {
                    int item = 0;
                    js_to_string(&js_arrays[ai].items[i], &item);
                    if (js_str_eq(item, sid)) { *ret = js_make_num(i); return 1; }
                } else if (js_to_number(&js_arrays[ai].items[i]) ==
                           js_to_number(&args[0])) {
                    *ret = js_make_num(i);
                    return 1;
                }
            }
            *ret = js_make_num(-1);
            return 1;
        }
        case JS_N_ARRAY_FOREACH: {
            int ai = this_obj - JS_OBJ_ARRAY;
            if (ai < 0 || ai >= JS_MAX_ARRAYS || argc < 1) return 0;
            for (int i = 0; i < js_arrays[ai].count; i++) {
                struct js_value call_args[2];
                struct js_value ignored;
                call_args[0] = js_arrays[ai].items[i];
                call_args[1] = js_make_num(i);
                if (!js_call_value(args[0], JS_OBJ_GLOBAL, call_args, 2, &ignored))
                    return 0;
            }
            return 1;
        }
        case JS_N_MATH_FLOOR:
        case JS_N_MATH_ABS:
        case JS_N_MATH_MIN:
        case JS_N_MATH_MAX:
        case JS_N_MATH_RANDOM: {
            if (callee.native == JS_N_MATH_RANDOM) {
                static uint32_t seed = 12345;
                seed = seed * 1103515245u + 12345u;
                *ret = js_make_num((seed >> 16) % 1000);
            } else if (callee.native == JS_N_MATH_MIN ||
                       callee.native == JS_N_MATH_MAX) {
                int64_t value = argc ? js_to_number(&args[0]) : 0;
                for (int i = 1; i < argc; i++) {
                    int64_t n = js_to_number(&args[i]);
                    if (callee.native == JS_N_MATH_MIN ? n < value : n > value)
                        value = n;
                }
                *ret = js_make_num(value);
            } else {
                int64_t n = argc ? js_to_number(&args[0]) : 0;
                if (callee.native == JS_N_MATH_ABS) *ret = js_make_num(n < 0 ? -n : n);
                else *ret = js_make_num(n);
            }
            return 1;
        }
        case JS_N_PARSE_INT: {
            int sid = 0;
            if (argc > 0) js_to_string(&args[0], &sid);
            const char *s = js_str(sid);
            while (*s == ' ') s++;
            int sign = 1;
            if (*s == '-') { sign = -1; s++; }
            int64_t value = 0;
            while (*s >= '0' && *s <= '9') value = value * 10 + (*s++ - '0');
            *ret = js_make_num(value * sign);
            return 1;
        }
        case JS_N_TIMEOUT: {
            if (argc > 0) js_timer_add(args[0]);
            return 1;
        }
        default:
            return 1;
    }
}

static int js_call_function(int func, int this_obj, struct js_value *args,
                            int argc, struct js_value *ret) {
    if (func < 0 || func >= js_func_used) return 0;
    int fenv = js_new_env(js_funcs[func].env);
    if (fenv < 0) return 0;
    for (int i = 0; i < js_funcs[func].nparams; i++)
        js_env_define(fenv, js_funcs[func].params[i],
                      i < argc ? args[i] : js_make_undef());
    int saved_this = js_this_obj;
    js_this_obj = this_obj;
    js_flow = JS_FLOW_OK;
    *ret = js_make_undef();
    if (!js_exec_node(js_funcs[func].body, fenv)) {
        js_this_obj = saved_this;
        return 0;
    }
    if (js_flow == JS_FLOW_RETURN) *ret = js_return_value;
    js_flow = JS_FLOW_OK;
    js_this_obj = saved_this;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Properties                                                          */
/* ------------------------------------------------------------------ */

static int js_get_prop(struct js_value base, int prop, struct js_value *out) {
    *out = js_make_undef();
    const char *name = js_str(prop);

    if (base.type == JS_V_STR) {
        if (js_streq(name, "length")) {
            *out = js_make_num(js_strlen(js_str(base.str)));
            return 1;
        }
        if (js_streq(name, "toUpperCase")) { *out = js_make_native(JS_N_STRING_UPPER); return 1; }
        if (js_streq(name, "toLowerCase")) { *out = js_make_native(JS_N_STRING_LOWER); return 1; }
        if (js_streq(name, "trim")) { *out = js_make_native(JS_N_STRING_TRIM); return 1; }
        if (js_streq(name, "indexOf")) { *out = js_make_native(JS_N_STRING_INDEX); return 1; }
        if (js_streq(name, "substring") || js_streq(name, "slice"))
            { *out = js_make_native(JS_N_STRING_SUBSTR); return 1; }
        if (js_streq(name, "charAt")) { *out = js_make_native(JS_N_STRING_CHAR); return 1; }
        if (js_streq(name, "includes")) { *out = js_make_native(JS_N_STRING_INCLUDES); return 1; }
        if (js_streq(name, "startsWith")) { *out = js_make_native(JS_N_STRING_STARTS); return 1; }
        if (js_streq(name, "endsWith")) { *out = js_make_native(JS_N_STRING_ENDS); return 1; }
        return 0;
    }
    if (base.type != JS_V_OBJ) return 0;

    if (base.obj == JS_OBJ_GLOBAL || base.obj == JS_OBJ_WINDOW) {
        if (js_streq(name, "document")) { *out = js_make_obj(JS_OBJ_DOCUMENT); return 1; }
        if (js_streq(name, "console")) { *out = js_make_obj(JS_OBJ_CONSOLE); return 1; }
        if (js_streq(name, "onload")) {
            struct js_value v;
            if (js_env_get(js_global_env, js_store_z("onload"), &v)) *out = v;
            return 1;
        }
        return 0;
    }
    if (base.obj == JS_OBJ_DOCUMENT) {
        if (js_streq(name, "body")) {
            *out = js_make_obj(js_dom.body ? js_dom.body() : -1);
            return 1;
        }
        if (js_streq(name, "getElementById"))
            { *out = js_make_native(JS_N_GET_ID); return 1; }
        if (js_streq(name, "querySelector"))
            { *out = js_make_native(JS_N_QUERY); return 1; }
        if (js_streq(name, "querySelectorAll"))
            { *out = js_make_native(JS_N_QUERY_ALL); return 1; }
        if (js_streq(name, "createElement"))
            { *out = js_make_native(JS_N_CREATE); return 1; }
        if (js_streq(name, "write"))
            { *out = js_make_native(JS_N_WRITE); return 1; }
        return 0;
    }
    if (base.obj == JS_OBJ_CONSOLE) {
        if (js_streq(name, "log") || js_streq(name, "warn") ||
            js_streq(name, "error"))
            { *out = js_make_native(JS_N_LOG); return 1; }
        return 0;
    }
    if (base.obj >= JS_OBJ_ARRAY && base.obj < JS_OBJ_ARRAY + JS_MAX_ARRAYS) {
        struct js_array *arr = &js_arrays[base.obj - JS_OBJ_ARRAY];
        if (js_streq(name, "length")) {
            *out = js_make_num(arr->count);
            return 1;
        }
        if (js_streq(name, "push")) { *out = js_make_native(JS_N_ARRAY_PUSH); return 1; }
        if (js_streq(name, "pop")) { *out = js_make_native(JS_N_ARRAY_POP); return 1; }
        if (js_streq(name, "join")) { *out = js_make_native(JS_N_ARRAY_JOIN); return 1; }
        if (js_streq(name, "indexOf")) { *out = js_make_native(JS_N_ARRAY_INDEX); return 1; }
        if (js_streq(name, "forEach")) { *out = js_make_native(JS_N_ARRAY_FOREACH); return 1; }
        return 0;
    }
    if (base.obj >= JS_OBJ_CLASS && base.obj < JS_OBJ_CLASS + 8192) {
        if (js_streq(name, "add")) { *out = js_make_native(JS_N_CLASS_ADD); return 1; }
        if (js_streq(name, "remove")) { *out = js_make_native(JS_N_CLASS_REMOVE); return 1; }
        if (js_streq(name, "toggle")) { *out = js_make_native(JS_N_CLASS_TOGGLE); return 1; }
        if (js_streq(name, "contains")) { *out = js_make_native(JS_N_CLASS_CONTAINS); return 1; }
        return 0;
    }
    if (base.obj == JS_OBJ_MATH) {
        if (js_streq(name, "floor")) { *out = js_make_native(JS_N_MATH_FLOOR); return 1; }
        if (js_streq(name, "abs")) { *out = js_make_native(JS_N_MATH_ABS); return 1; }
        if (js_streq(name, "min")) { *out = js_make_native(JS_N_MATH_MIN); return 1; }
        if (js_streq(name, "max")) { *out = js_make_native(JS_N_MATH_MAX); return 1; }
        if (js_streq(name, "random")) { *out = js_make_native(JS_N_MATH_RANDOM); return 1; }
        return 0;
    }
    if (base.obj >= JS_OBJ_STYLE && base.obj < JS_OBJ_STYLE + 1024) {
        (void)name;
        return 0;
    }
    if (base.obj < 0) return 0;

    if (js_streq(name, "innerHTML") || js_streq(name, "textContent")) {
        const char *text = js_dom.get_text ? js_dom.get_text(base.obj) : "";
        *out = js_make_str(js_store_z(text ? text : ""));
        return 1;
    }
    if (js_streq(name, "style"))
        { *out = js_make_obj(JS_OBJ_STYLE + base.obj); return 1; }
    if (js_streq(name, "classList"))
        { *out = js_make_obj(JS_OBJ_CLASS + base.obj); return 1; }
    if (js_streq(name, "parentElement")) {
        int node = js_dom.parent_node ? js_dom.parent_node(base.obj) : -1;
        *out = node >= 0 ? js_make_obj(node) : js_make_null();
        return 1;
    }
    if (js_streq(name, "children")) {
        int first = js_dom.first_child ? js_dom.first_child(base.obj, 1) : -1;
        if (first < 0) {
            int arr = js_new_array();
            *out = arr >= 0 ? js_make_obj(JS_OBJ_ARRAY + arr) : js_make_null();
        } else {
            *out = js_make_obj(js_node_array_from(first, 1));
        }
        return 1;
    }
    if (js_streq(name, "nextElementSibling")) {
        int node = js_dom.next_sibling ? js_dom.next_sibling(base.obj, 1) : -1;
        *out = node >= 0 ? js_make_obj(node) : js_make_null();
        return 1;
    }
    if (js_streq(name, "remove"))
        { *out = js_make_native(JS_N_REMOVE); return 1; }
    if (js_streq(name, "appendChild"))
        { *out = js_make_native(JS_N_APPEND); return 1; }
    if (js_streq(name, "removeChild"))
        { *out = js_make_native(JS_N_REMOVE); return 1; }
    if (js_streq(name, "querySelectorAll"))
        { *out = js_make_native(JS_N_QUERY_ALL); return 1; }
    if (js_streq(name, "src") || js_streq(name, "href") ||
        js_streq(name, "id") || js_streq(name, "className") ||
        js_streq(name, "value") || js_streq(name, "alt") ||
        js_streq(name, "title")) {
        const char *value = js_dom.get_attr ? js_dom.get_attr(base.obj, name) : "";
        *out = js_make_str(js_store_z(value ? value : ""));
        return 1;
    }
    if (js_streq(name, "tagName")) {
        const char *tag = js_dom.tag_name ? js_dom.tag_name(base.obj) : "";
        *out = js_make_str(js_store_z(tag ? tag : ""));
        return 1;
    }
    if (js_streq(name, "addEventListener"))
        { *out = js_make_native(JS_N_ADD_EVENT); return 1; }
    if (js_streq(name, "getAttribute"))
        { *out = js_make_native(JS_N_GET_ATTR); return 1; }
    if (js_streq(name, "setAttribute"))
        { *out = js_make_native(JS_N_SET_ATTR); return 1; }
    return 0;
}

static int js_set_prop(struct js_value base, int prop, struct js_value value) {
    const char *name = js_str(prop);
    int sid = 0;

    if (base.type == JS_V_OBJ && (base.obj == JS_OBJ_GLOBAL ||
                                  base.obj == JS_OBJ_WINDOW)) {
        if (js_streq(name, "onload")) {
            js_env_define(js_global_env, js_store_z("onload"), value);
            return 1;
        }
        return 0;
    }
    if (base.type != JS_V_OBJ) return 0;

    if (base.obj >= JS_OBJ_ARRAY && base.obj < JS_OBJ_ARRAY + JS_MAX_ARRAYS) {
        struct js_array *arr = &js_arrays[base.obj - JS_OBJ_ARRAY];
        if (js_streq(name, "length")) {
            int n = (int)js_to_number(&value);
            if (n < 0) n = 0;
            if (n > JS_MAX_ARRAY) n = JS_MAX_ARRAY;
            arr->count = n;
            return 1;
        }
        return 0;
    }
    if (base.obj >= JS_OBJ_STYLE && base.obj < JS_OBJ_STYLE + 1024) {
        if (!js_to_string(&value, &sid)) return 0;
        int node = base.obj - JS_OBJ_STYLE;
        if (js_dom.set_style) js_dom.set_style(node, name, js_str(sid));
        return 1;
    }
    if (base.obj < 0) return 0;

    if (js_streq(name, "innerHTML")) {
        if (!js_to_string(&value, &sid)) return 0;
        if (js_dom.set_html) js_dom.set_html(base.obj, js_str(sid));
        return 1;
    }
    if (js_streq(name, "textContent")) {
        if (!js_to_string(&value, &sid)) return 0;
        if (js_dom.set_text) js_dom.set_text(base.obj, js_str(sid));
        return 1;
    }
    if (js_streq(name, "onclick")) {
        int slot = -1;
        for (int i = 0; i < JS_MAX_EVENTS; i++)
            if (!js_events[i].used) { slot = i; break; }
        if (slot < 0) return 0;
        js_events[slot].used = 1;
        js_events[slot].node = (int16_t)base.obj;
        js_events[slot].source = 0;
        if (value.type == JS_V_FUNC) js_events[slot].func = value.func;
        else {
            js_events[slot].func = -1;
            if (value.type == JS_V_STR) js_events[slot].source = value.str;
            else js_events[slot].source = js_store_z("");
        }
        return 1;
    }
    if (js_streq(name, "src") || js_streq(name, "href") ||
        js_streq(name, "id") || js_streq(name, "className") ||
        js_streq(name, "value") || js_streq(name, "alt")) {
        if (!js_to_string(&value, &sid)) return 0;
        if (js_dom.set_attr) js_dom.set_attr(base.obj, name, js_str(sid));
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Expression evaluation                                               */
/* ------------------------------------------------------------------ */

static int js_eval_assign(int node, int env, struct js_value *out) {
    struct js_node *n = &js_nodes[node];
    struct js_value value;
    if (!js_eval(n->b, env, &value)) return 0;

    struct js_node *target = &js_nodes[n->a];
    if (n->op == JS_OP_ADD_ASSIGN || n->op == JS_OP_SUB_ASSIGN) {
        struct js_value old;
        if (!js_eval(n->a, env, &old)) return 0;
        struct js_value result;
        int op = n->op == JS_OP_ADD_ASSIGN ? JS_OP_ADD : JS_OP_SUB;
        if (op == JS_OP_ADD && (old.type == JS_V_STR || value.type == JS_V_STR)) {
            int a = 0, b = 0;
            js_to_string(&old, &a);
            js_to_string(&value, &b);
            int total = js_strlen(js_str(a)) + js_strlen(js_str(b));
            char buf[512];
            if (total > (int)sizeof(buf) - 1) total = (int)sizeof(buf) - 1;
            int at = 0;
            const char *sa = js_str(a);
            const char *sb = js_str(b);
            while (*sa && at < total) buf[at++] = *sa++;
            while (*sb && at < total) buf[at++] = *sb++;
            buf[at] = '\0';
            result = js_make_str(js_store(buf, at));
        } else {
            result = js_make_num(js_to_number(&old) + js_to_number(&value));
            if (op == JS_OP_SUB) result.num = js_to_number(&old) - js_to_number(&value);
        }
        value = result;
    }

    if (target->kind == JS_K_IDENT) {
        js_env_set(env, target->text, value);
    } else if (target->kind == JS_K_MEMBER) {
        struct js_value base;
        if (!js_eval(target->a, env, &base)) return 0;
        if (!js_set_prop(base, target->text, value)) {
            js_set_error("property cannot be assigned", js_line);
            return 0;
        }
    } else if (target->kind == JS_K_INDEX) {
        struct js_value base, index;
        if (!js_eval(target->a, env, &base) ||
            !js_eval(target->b, env, &index)) return 0;
        if (base.type == JS_V_OBJ && base.obj >= JS_OBJ_ARRAY &&
            base.obj < JS_OBJ_ARRAY + JS_MAX_ARRAYS) {
            struct js_array *arr = &js_arrays[base.obj - JS_OBJ_ARRAY];
            int i = (int)js_to_number(&index);
            if (i >= 0 && i < JS_MAX_ARRAY) {
                if (i >= arr->count) arr->count = i + 1;
                arr->items[i] = value;
            }
        } else if (base.type == JS_V_OBJ && base.obj >= 0) {
            int sid = 0;
            js_to_string(&index, &sid);
            if (js_dom.set_attr) js_dom.set_attr(base.obj, js_str(sid), "");
        }
    } else {
        js_set_error("invalid assignment target", js_line);
        return 0;
    }
    *out = value;
    return 1;
}

static int js_eval_binary(int node, int env, struct js_value *out) {
    struct js_node *n = &js_nodes[node];
    struct js_value a, b;
    if (!js_eval(n->a, env, &a)) return 0;

    if (n->op == JS_OP_AND) {
        if (!js_truthy(&a)) { *out = a; return 1; }
        return js_eval(n->b, env, out);
    }
    if (n->op == JS_OP_OR) {
        if (js_truthy(&a)) { *out = a; return 1; }
        return js_eval(n->b, env, out);
    }
    if (!js_eval(n->b, env, &b)) return 0;

    if (n->op == JS_OP_ADD && (a.type == JS_V_STR || b.type == JS_V_STR)) {
        int ao = 0, bo = 0;
        js_to_string(&a, &ao);
        js_to_string(&b, &bo);
        const char *sa = js_str(ao);
        const char *sb = js_str(bo);
        int total = js_strlen(sa) + js_strlen(sb);
        char buf[512];
        if (total > (int)sizeof(buf) - 1) total = (int)sizeof(buf) - 1;
        int at = 0;
        while (*sa && at < total) buf[at++] = *sa++;
        while (*sb && at < total) buf[at++] = *sb++;
        buf[at] = '\0';
        *out = js_make_str(js_store(buf, at));
        return 1;
    }

    if (n->op == JS_OP_EQ || n->op == JS_OP_NE) {
        int equal = 0;
        if (a.type == JS_V_STR || b.type == JS_V_STR) {
            int ao = 0, bo = 0;
            js_to_string(&a, &ao);
            js_to_string(&b, &bo);
            equal = js_str_eq(ao, bo);
        } else {
            equal = js_to_number(&a) == js_to_number(&b);
        }
        *out = js_make_bool(n->op == JS_OP_EQ ? equal : !equal);
        return 1;
    }

    if (n->op == JS_OP_LT || n->op == JS_OP_LE ||
        n->op == JS_OP_GT || n->op == JS_OP_GE) {
        int cmp = 0;
        if (a.type == JS_V_STR && b.type == JS_V_STR) {
            const char *sa = js_str(a.str);
            const char *sb = js_str(b.str);
            while (*sa && *sb && *sa == *sb) { sa++; sb++; }
            cmp = (unsigned char)*sa - (unsigned char)*sb;
        } else {
            int64_t na = js_to_number(&a);
            int64_t nb = js_to_number(&b);
            cmp = na < nb ? -1 : (na > nb ? 1 : 0);
        }
        int result = 0;
        if (n->op == JS_OP_LT) result = cmp < 0;
        else if (n->op == JS_OP_LE) result = cmp <= 0;
        else if (n->op == JS_OP_GT) result = cmp > 0;
        else result = cmp >= 0;
        *out = js_make_bool(result);
        return 1;
    }

    int64_t na = js_to_number(&a);
    int64_t nb = js_to_number(&b);
    switch (n->op) {
        case JS_OP_ADD: *out = js_make_num(na + nb); break;
        case JS_OP_SUB: *out = js_make_num(na - nb); break;
        case JS_OP_MUL: *out = js_make_num(na * nb); break;
        case JS_OP_DIV: *out = js_make_num(nb ? na / nb : 0); break;
        case JS_OP_MOD: *out = js_make_num(nb ? na % nb : 0); break;
        default: *out = js_make_undef(); break;
    }
    return 1;
}

static int js_eval_call(int node, int env, struct js_value *out) {
    struct js_node *n = &js_nodes[node];
    struct js_value callee;
    int this_obj = JS_OBJ_GLOBAL;

    if (js_nodes[n->a].kind == JS_K_MEMBER) {
        struct js_value base;
        int member = n->a;
        if (!js_eval(js_nodes[member].a, env, &base)) return 0;
        if (!js_get_prop(base, js_nodes[member].text, &callee)) {
            js_set_error("unknown method", js_line);
            return 0;
        }
        this_obj = base.type == JS_V_OBJ ? base.obj : -1;
        js_this_str = base.type == JS_V_STR ? base.str : -1;
    } else {
        if (!js_eval(n->a, env, &callee)) return 0;
    }

    struct js_value args[JS_MAX_ARGS];
    int argc = 0;
    for (int a = n->b; a >= 0 && argc < JS_MAX_ARGS; a = js_nodes[a].next) {
        if (!js_eval(a, env, &args[argc])) return 0;
        argc++;
    }
    return js_call_value(callee, this_obj, args, argc, out);
}

static int js_eval(int node, int env, struct js_value *out) {
    if (node < 0) { *out = js_make_undef(); return 1; }
    if (++js_steps > JS_MAX_STEPS) {
        js_set_error("script exceeded its instruction budget", js_line);
        return 0;
    }
    struct js_node *n = &js_nodes[node];
    switch (n->kind) {
        case JS_K_NUM: *out = js_make_num(n->num); return 1;
        case JS_K_STR: *out = js_make_str(n->text); return 1;
        case JS_K_BOOL: *out = js_make_bool(n->num != 0); return 1;
        case JS_K_NULL: *out = js_make_null(); return 1;
        case JS_K_UNDEF: *out = js_make_undef(); return 1;
        case JS_K_THIS:
            *out = js_make_obj(js_this_obj >= 0 ? js_this_obj : JS_OBJ_GLOBAL);
            return 1;
        case JS_K_IDENT: {
            js_env_get(env, n->text, out);
            return 1;
        }
        case JS_K_MEMBER: {
            struct js_value base;
            if (!js_eval(n->a, env, &base)) return 0;
            if (!js_get_prop(base, n->text, out)) *out = js_make_undef();
            return 1;
        }
        case JS_K_INDEX: {
            struct js_value base, index;
            if (!js_eval(n->a, env, &base) || !js_eval(n->b, env, &index)) return 0;
            if (base.type == JS_V_OBJ && base.obj >= JS_OBJ_ARRAY &&
                base.obj < JS_OBJ_ARRAY + JS_MAX_ARRAYS) {
                struct js_array *arr = &js_arrays[base.obj - JS_OBJ_ARRAY];
                int i = (int)js_to_number(&index);
                *out = (i >= 0 && i < arr->count) ? arr->items[i] : js_make_undef();
                return 1;
            }
            if (base.type == JS_V_STR) {
                int i = (int)js_to_number(&index);
                const char *s = js_str(base.str);
                if (i >= 0 && i < js_strlen(s)) {
                    char c = s[i];
                    *out = js_make_str(js_store(&c, 1));
                } else *out = js_make_undef();
                return 1;
            }
            *out = js_make_undef();
            return 1;
        }
        case JS_K_CALL: return js_eval_call(node, env, out);
        case JS_K_BINARY: return js_eval_binary(node, env, out);
        case JS_K_ASSIGN: return js_eval_assign(node, env, out);
        case JS_K_UNARY: {
            struct js_value v;
            if (!js_eval(n->a, env, &v)) return 0;
            switch (n->op) {
                case JS_OP_NOT: *out = js_make_bool(!js_truthy(&v)); break;
                case JS_OP_NEG: *out = js_make_num(-js_to_number(&v)); break;
                case JS_OP_POS: *out = js_make_num(js_to_number(&v)); break;
                case JS_OP_TYPEOF: *out = js_make_str(js_store_z(js_type_name(&v))); break;
                case JS_OP_INC: {
                    struct js_value one = js_make_num(1);
                    struct js_value target;
                    if (!js_eval(n->a, env, &target)) return 0;
                    int64_t value = js_to_number(&target) + 1;
                    (void)one;
                    *out = js_make_num(value);
                    if (js_nodes[n->a].kind == JS_K_IDENT)
                        js_env_set(env, js_nodes[n->a].text, *out);
                    return 1;
                }
                case JS_OP_DEC: {
                    struct js_value target;
                    if (!js_eval(n->a, env, &target)) return 0;
                    *out = js_make_num(js_to_number(&target) - 1);
                    if (js_nodes[n->a].kind == JS_K_IDENT)
                        js_env_set(env, js_nodes[n->a].text, *out);
                    return 1;
                }
                default: *out = js_make_undef(); break;
            }
            return 1;
        }
        case JS_K_COND: {
            struct js_value cond;
            if (!js_eval(n->a, env, &cond)) return 0;
            return js_eval(js_truthy(&cond) ? n->b : n->c, env, out);
        }
        case JS_K_ARRAY: {
            int arr = js_new_array();
            if (arr < 0) return 0;
            *out = js_make_obj(JS_OBJ_ARRAY + arr);
            int i = 0;
            for (int item = n->a; item >= 0 && i < JS_MAX_ARRAY;
                 item = js_nodes[item].next) {
                if (!js_eval(item, env, &js_arrays[arr].items[i])) return 0;
                i++;
            }
            js_arrays[arr].count = i;
            return 1;
        }
        case JS_K_FUNC: {
            int func = js_make_function(node, env);
            if (func < 0) return 0;
            *out = js_make_func(func);
            return 1;
        }
        default:
            *out = js_make_undef();
            return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

static int js_exec_block(int first, int env) {
    js_hoist(first, env);
    for (int n = first; n >= 0; n = js_nodes[n].next) {
        if (js_nodes[n].kind == JS_K_FUNC) continue;
        if (!js_exec_node(n, env)) return 0;
        if (js_flow != JS_FLOW_OK) return 1;
    }
    return 1;
}

static int js_exec_node(int node, int env) {
    if (node < 0) return 1;
    if (++js_steps > JS_MAX_STEPS) {
        js_set_error("script exceeded its instruction budget", js_line);
        return 0;
    }
    struct js_node *n = &js_nodes[node];
    switch (n->kind) {
        case JS_K_EMPTY: return 1;
        case JS_K_BLOCK: return js_exec_block(n->a, env);
        case JS_K_VAR_LIST: {
            for (int var = n->a; var >= 0; var = js_nodes[var].next) {
                struct js_value value = js_make_undef();
                if (js_nodes[var].a >= 0 &&
                    !js_eval(js_nodes[var].a, env, &value)) return 0;
                js_env_define(env, js_nodes[var].text, value);
            }
            return 1;
        }
        case JS_K_FUNC: return 1;
        case JS_K_EXPR: {
            struct js_value value;
            return js_eval(n->a, env, &value);
        }
        case JS_K_IF: {
            struct js_value cond;
            if (!js_eval(n->a, env, &cond)) return 0;
            if (js_truthy(&cond)) return js_exec_node(n->b, env);
            return js_exec_node(n->c, env);
        }
        case JS_K_WHILE: {
            for (;;) {
                struct js_value cond;
                if (!js_eval(n->a, env, &cond)) return 0;
                if (!js_truthy(&cond)) break;
                if (!js_exec_node(n->b, env)) return 0;
                if (js_flow == JS_FLOW_BREAK) { js_flow = JS_FLOW_OK; break; }
                if (js_flow != JS_FLOW_OK) break;
            }
            return 1;
        }
        case JS_K_FOR: {
            if (n->a >= 0 && !js_exec_node(n->a, env)) return 0;
            for (;;) {
                if (n->b >= 0) {
                    struct js_value cond;
                    if (!js_eval(n->b, env, &cond)) return 0;
                    if (!js_truthy(&cond)) break;
                }
                if (!js_exec_node(n->d, env)) return 0;
                if (js_flow == JS_FLOW_BREAK) { js_flow = JS_FLOW_OK; break; }
                if (js_flow != JS_FLOW_OK) break;
                if (n->c >= 0) {
                    struct js_value value;
                    if (!js_eval(n->c, env, &value)) return 0;
                }
            }
            return 1;
        }
        case JS_K_RETURN: {
            if (n->a >= 0 && !js_eval(n->a, env, &js_return_value))
                return 0;
            else if (n->a < 0) js_return_value = js_make_undef();
            js_flow = JS_FLOW_RETURN;
            return 1;
        }
        case JS_K_BREAK:
            js_flow = JS_FLOW_BREAK;
            return 1;
        default: {
            struct js_value value;
            return js_eval(node, env, &value);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void browser_js_reset(void) {
    js_node_used = 0;
    js_string_used = 0;
    js_env_used = 0;
    js_func_used = 0;
    js_array_used = 0;
    js_global_env = 0;
    js_parse_failed = 0;
    js_error[0] = '\0';
    js_flow = JS_FLOW_OK;
    js_steps = 0;
    js_this_obj = JS_OBJ_GLOBAL;
    js_this_str = -1;
    js_return_value = js_make_undef();
    for (int i = 0; i < JS_MAX_EVENTS; i++) js_events[i].used = 0;
    for (int i = 0; i < 16; i++) js_timers[i].used = 0;
    js_timer_used = 0;
    js_strings[0] = '\0';
    js_string_used = 1;

    int env = js_new_env(-1);
    js_global_env = env >= 0 ? env : 0;
    js_env_define(js_global_env, js_store_z("document"),
                  js_make_obj(JS_OBJ_DOCUMENT));
    js_env_define(js_global_env, js_store_z("window"),
                  js_make_obj(JS_OBJ_WINDOW));
    js_env_define(js_global_env, js_store_z("console"),
                  js_make_obj(JS_OBJ_CONSOLE));
    js_env_define(js_global_env, js_store_z("globalThis"),
                  js_make_obj(JS_OBJ_GLOBAL));
    js_env_define(js_global_env, js_store_z("Math"),
                  js_make_obj(JS_OBJ_MATH));
    js_env_define(js_global_env, js_store_z("parseInt"),
                  js_make_native(JS_N_PARSE_INT));
    js_env_define(js_global_env, js_store_z("setTimeout"),
                  js_make_native(JS_N_TIMEOUT));
    js_env_define(js_global_env, js_store_z("setInterval"),
                  js_make_native(JS_N_TIMEOUT));
}

int browser_js_run(const char *source, const struct browser_js_dom *dom,
                   char *error, int error_max) {
    if (!source) return 0;
    if (dom) js_dom = *dom;
    js_source = source;
    js_source_len = js_strlen(source);
    if (js_source_len > JS_MAX_SOURCE) js_source_len = JS_MAX_SOURCE;
    js_source_pos = 0;
    js_line = 1;
    js_parse_failed = 0;
    js_error[0] = '\0';
    js_flow = JS_FLOW_OK;
    js_steps = 0;

    int program = js_parse_program();
    if (!js_parse_ok()) {
        if (error && error_max > 0) {
            int n = 0;
            while (js_error[n] && n < error_max - 1) {
                error[n] = js_error[n];
                n++;
            }
            error[n] = '\0';
        }
        return -1;
    }
    if (program >= 0) {
        js_flow = JS_FLOW_OK;
        js_exec_block(program, js_global_env);
    }
    if (js_flow == JS_FLOW_ERROR || js_error[0]) {
        if (error && error_max > 0) {
            int n = 0;
            while (js_error[n] && n < error_max - 1) {
                error[n] = js_error[n];
                n++;
            }
            error[n] = '\0';
        }
        return -1;
    }
    js_flow = JS_FLOW_OK;
    return 0;
}

int browser_js_set_inline_handler(int node, const char *source) {
    if (!source || node < 0) return -1;
    int slot = -1;
    for (int i = 0; i < JS_MAX_EVENTS; i++)
        if (!js_events[i].used) { slot = i; break; }
    if (slot < 0) return -1;
    js_events[slot].used = 1;
    js_events[slot].node = (int16_t)node;
    js_events[slot].func = -1;
    js_events[slot].source = (int16_t)js_store_z(source);
    return 0;
}

int browser_js_has_handler(int node) {
    if (node < 0) return 0;
    for (int i = 0; i < JS_MAX_EVENTS; i++)
        if (js_events[i].used && js_events[i].node == node) return 1;
    return 0;
}

int browser_js_click(int node) {
    if (node < 0) return 0;
    for (int i = JS_MAX_EVENTS - 1; i >= 0; i--) {
        if (!js_events[i].used || js_events[i].node != node) continue;
        if (js_events[i].func >= 0) {
            struct js_value ret;
            if (!js_call_function(js_events[i].func, node, 0, 0, &ret))
                return -1;
            return 1;
        }
        if (js_events[i].source > 0) {
            js_source = js_str(js_events[i].source);
            js_source_len = js_strlen(js_source);
            js_source_pos = 0;
            js_line = 1;
            js_parse_failed = 0;
            js_error[0] = '\0';
            js_flow = JS_FLOW_OK;
            js_steps = 0;
            int saved_this = js_this_obj;
            js_this_obj = node;
            js_this_str = -1;
            int program = js_parse_program();
            if (js_parse_ok() && program >= 0)
                js_exec_block(program, js_global_env);
            js_this_obj = saved_this;
            if (!js_parse_ok() || js_flow == JS_FLOW_ERROR || js_error[0])
                return -1;
            js_flow = JS_FLOW_OK;
            return 1;
        }
    }
    return 0;
}

int browser_js_fire_load(void) {
    struct js_value fn;
    if (!js_env_get(js_global_env, js_store_z("onload"), &fn)) return 0;
    js_this_str = -1;
    if (fn.type == JS_V_FUNC) {
        struct js_value ret;
        if (!js_call_function(fn.func, JS_OBJ_GLOBAL, 0, 0, &ret)) return -1;
    }
    return 0;
}

int browser_js_pending_timers(void) {
    return js_timer_used;
}

int browser_js_fire_timers(void) {
    int fired = 0;
    for (int i = 0; i < 16 && fired < 16; i++) {
        if (!js_timers[i].used) continue;
        struct js_value fn = js_timers[i].func;
        js_timers[i].used = 0;
        js_timer_used--;
        fired++;
        if (fn.type == JS_V_FUNC) {
            struct js_value ret;
            js_this_str = -1;
            js_flow = JS_FLOW_OK;
            if (!js_call_function(fn.func, JS_OBJ_GLOBAL, 0, 0, &ret))
                return -1;
        }
    }
    return 0;
}

const char *browser_js_last_error(void) {
    return js_error;
}
