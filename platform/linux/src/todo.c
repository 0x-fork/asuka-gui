// todo.c: what isn't ported yet (stubs, removed as each area lands).
#include "host.h"

bool lx_draggable(node *n) { (void)n; return false; }
void lx_tray_style_changed(void) {}
bool lx_tray_click(uint32_t icon, int *x, int *y) { (void)icon; (void)x; (void)y; return false; }
void lx_tray_flyout_closed(uint32_t icon) { (void)icon; }
void lx_call(node *root, ak_span args) { (void)root; (void)args; }
void lx_open_url(const char *u) { (void)u; }
void lx_share(node *n, ak_span text) { (void)n; (void)text; }
void lx_modules_init(void) {}
void lx_watch_desktop(void) {}
int lx_notify_headless(const char *id) { (void)id; return 0; }
void lx_install_notify_actions(void) {}
void lx_notify_action_at_start(void) {}
void lx_watch_launches(void);
void lx_read_clipboard_text(void (*done)(const char *text, bool ok, void *ctx), void *ctx) { done("", false, ctx); }
