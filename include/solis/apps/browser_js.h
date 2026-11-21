#ifndef SOLIS_APPS_BROWSER_JS_H
#define SOLIS_APPS_BROWSER_JS_H

#include <stdint.h>

/* DOM operations exposed to the JavaScript subset.  The browser owns the
 * document tree; the engine only asks for and updates small pieces of it.
 * Node handles are non-negative indices, or -1 for "not found". */
struct browser_js_dom {
    int (*find_id)(const char *id);
    int (*find_selector)(const char *selector);
    int (*query_all)(const char *selector, int *out, int max);
    int (*body)(void);
    const char *(*tag_name)(int node);
    const char *(*get_text)(int node);
    const char *(*get_attr)(int node, const char *name);
    void (*set_text)(int node, const char *text);
    void (*set_html)(int node, const char *html);
    void (*append_html)(const char *html);
    void (*set_style)(int node, const char *property, const char *value);
    void (*set_attr)(int node, const char *name, const char *value);
    int (*create_element)(const char *tag);
    int (*append_child)(int parent, int child);
    int (*remove_child)(int parent, int child);
    int (*parent_node)(int node);
    int (*first_child)(int node, int element_only);
    int (*next_sibling)(int node, int element_only);
    void (*status)(const char *text);
};

/* Clear all script state before parsing a new page. */
void browser_js_reset(void);

/* Parse and execute one script.  Scripts on the same page share globals and
 * function definitions.  Returns 0 on success, -1 on a syntax/runtime error. */
int browser_js_run(const char *source, const struct browser_js_dom *dom,
                   char *error, int error_max);

/* Execute window.onload handlers after the document and images are ready. */
int browser_js_fire_load(void);

/* Run short deferred callbacks registered with setTimeout. */
int browser_js_fire_timers(void);

/* Number of deferred callbacks still waiting to run. */
int browser_js_pending_timers(void);

/* Register an HTML on* attribute as a click handler. */
int browser_js_set_inline_handler(int node, const char *source);

/* Returns non-zero if clicking this node should call a script. */
int browser_js_has_handler(int node);

/* Run the click handler attached to a node.  Returns 1 if one ran, -1 on a
 * script error, and 0 when the node has no handler. */
int browser_js_click(int node);

const char *browser_js_last_error(void);

#endif
