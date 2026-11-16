// todo.c: what isn't ported yet (stubs, removed as each area lands).
#include "host.h"

void lx_route_keys(win *w) { (void)w; }
void lx_set_keys(node *n, ak_span b) { (void)n; (void)b; }
void lx_update_a11y(node *n) { (void)n; }
void lx_set_a11y(node *n, ak_prop p, const ak_value *v) { (void)n; (void)p; (void)v; }
void lx_announce(ak_span text) { (void)text; }
void lx_set_a11y_actions(node *n, ak_span b) { (void)n; (void)b; }
void lx_set_adjustable(node *n, bool on) { (void)n; (void)on; }
void lx_set_a11y_string(gpointer w, int prop, const char *s) { (void)w; (void)prop; (void)s; }
void lx_set_role(gpointer w, const char *nick) { (void)w; (void)nick; }
bool lx_draggable(node *n) { (void)n; return false; }
void lx_menu_changed(node *n) { (void)n; }
void lx_popup_menu(node *n, double x, double y) { (void)n; (void)x; (void)y; }
void lx_set_menu_bar(ak_span args) { (void)args; }
GMenuModel *lx_menu_model(const action_vec *items, const char *prefix) { (void)items; (void)prefix; return (GMenuModel *)g_menu_new(); }
void lx_insert_menu_actions(GtkWidget *w, const char *group, ak_id target, const action_vec *items) { (void)w; (void)group; (void)target; (void)items; }
void lx_make_scroll(node *n, bool horizontal) { (void)n; (void)horizontal; }
void lx_scroll_set_content(node *n, node *c) { (void)n; (void)c; }
bool lx_set_scroll_prop(node *n, ak_prop p, const ak_value *v) { (void)n; (void)p; (void)v; return false; }
void lx_scroll_to(node *n, double x, double y, bool animated) { (void)n; (void)x; (void)y; (void)animated; }
void lx_make_field(node *n) { (void)n; }
void lx_set_field_prop(node *n, ak_prop p, const ak_value *v) { (void)n; (void)p; (void)v; }
void lx_style_field(node *n) { (void)n; }
void lx_focus_field(node *n) { (void)n; }
void lx_insert_text(node *n, ak_span text) { (void)n; (void)text; }
float lx_measure_multiline(node *n, float width) { (void)n; (void)width; return -1; }
void lx_field_command(node *n, ak_cmd c, ak_span args) { (void)n; (void)c; (void)args; }
void lx_watch_preedit(GtkWidget *text) { (void)text; }
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
bool lx_key_to_nodes(const char *key, uint8_t mods) { (void)key; (void)mods; return false; }
char *lx_accelerator(const char *key, uint8_t mods) { (void)key; (void)mods; return NULL; }
bool lx_activate_app_item(const char *title) { (void)title; return false; }
node *lx_field_of_widget(GtkWidget *w) { (void)w; return NULL; }
const char *lx_badged(const char *title, const char *badge) { (void)badge; return S(title); }
