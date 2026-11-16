// todo.c: what isn't ported yet (stubs, removed as each area lands).
#include "host.h"

bool lx_draggable(node *n) { (void)n; return false; }
void lx_set_image_prop(node *n, ak_prop p, const ak_value *v) { (void)n; (void)p; (void)v; }
void lx_draw_image(node *n, GtkSnapshot *snap, const graphene_rect_t *b) { (void)n; (void)snap; (void)b; }
void lx_draw_symbol(node *n, GtkSnapshot *snap, const graphene_rect_t *b) { (void)n; (void)snap; (void)b; }
void lx_style_symbol(node *n) { lx_queue_draw(n); }
const char *lx_icon_name(const char *symbol) { return S(symbol); }
void lx_place_inline(node *p, node *c, float f[4]) { (void)p; (void)c; (void)f; }
void lx_present_popover(node *n) { (void)n; }
void lx_apply_direction(node *n) { (void)n; }
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
void lx_crash_start(void) {}
void lx_watch_launches(void);
void lx_listen_location(node *n) { (void)n; }
void lx_route_location(ak_span args) { (void)args; }
void lx_init_locale(void) { str_setc(&H.languages, "en-US"); }
void lx_set_locale(ak_span args) { (void)args; }
void lx_save_locale(ak_span tag) { (void)tag; }
const char *lx_posix_locale(void) { return "en_US"; }
const char *lx_time_zone(void) { return "UTC"; }
void lx_watch_signals(void) {}
void lx_watch_memory(void) {}
void lx_watch_app(void) {}
bool lx_ask_quit(bool ending) { (void)ending; return true; }
void lx_quit_command(ak_span args) { (void)args; g_application_quit((GApplication *)H.app); }
bool lx_closing_last(win *w) { (void)w; return true; }
void lx_save_window(win *w) { (void)w; }
void lx_restore_window(win *w) { (void)w; }
void lx_watch_window(win *w) { (void)w; }
const char *lx_word(const char *key) { return key; }
const char *lx_badged(const char *title, const char *badge) { (void)badge; return S(title); }
void lx_run_attrs(node *n, const ak_rich_style *s, void (*add)(PangoAttribute *a, void *ctx), void *ctx) { (void)n; (void)s; (void)add; (void)ctx; }
void lx_read_clipboard_text(void (*done)(const char *text, bool ok, void *ctx), void *ctx) { done("", false, ctx); }
bool lx_list_refreshable(node *n) { (void)n; return false; }
bool lx_list_refresh(node *n) { (void)n; return false; }
node *lx_popover_anchor(node *n) { (void)n; return NULL; }
const char *lx_word_app(const char *key, const char *app) { (void)key; return app; }
const char *lx_symbol_label(const char *symbol) { return S(symbol); }
void lx_request_quit(void) { g_application_quit((GApplication *)H.app); }
