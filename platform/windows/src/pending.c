// pending.c: what the host's parts not yet ported answer meanwhile:
// each goes as its part is written. Nothing here is the host's behavior.
#include "win.h"

bool wh_accelerate(HWND win, MSG *m) { (void)win; (void)m; return false; }
bool wh_accelerator(const char *key, uint8_t mods, ACCEL *out) { (void)key; (void)mods; (void)out; return false; }
void wh_accept_drops(HWND win) { (void)win;  }
bool wh_actions_menu(node *n, HWND hwnd, LPARAM lp) { (void)n; (void)hwnd; (void)lp; return false; }
bool wh_adjust_key(node *n, WPARAM vk) { (void)n; (void)vk; return false; }
HBRUSH wh_alert_dialog_colors(HDC dc) { (void)dc; return NULL; }
void wh_alert_dialog_command(node *n, HWND ctl, UINT code) { (void)n; (void)ctl; (void)code;  }
bool wh_alert_dialog_default_button(node *n, HWND ctl) { (void)n; (void)ctl; return false; }
LRESULT wh_alert_dialog_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_alert_field_event(node *n, ak_event *e) { (void)n; (void)e;  }
void wh_allow_gestures(node *n) { (void)n;  }
bool wh_animate_frame(node *n, const float f[4]) { (void)n; (void)f; return false; }
void wh_animation_step(void) {  }
HICON wh_app_icon(void) { return NULL; }
void wh_back(node *root) { (void)root;  }
void wh_badge_message(UINT m) { (void)m;  }
void wh_bars_reset(node *n) { (void)n;  }
void wh_build_menu_bar(void) {  }
void wh_cancel_alert_dialog(node *n) { (void)n;  }
void wh_cancel_popover(node *n) { (void)n;  }
void wh_cancel_sheet(node *n) { (void)n;  }
void wh_cell_set_height(node *n, float h) { (void)n; (void)h;  }
void wh_cell_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
bool wh_choose_menu_item(const wchar_t *title) { (void)title; return false; }
void wh_clear_accessible_name(HWND hwnd) { (void)hwnd;  }
void wh_clear_menu_styles(void) {  }
void wh_clip_tree(node *n, HDC dc) { (void)n; (void)dc;  }
void wh_combo_command(node *n, HWND ctl, UINT code) { (void)n; (void)ctl; (void)code;  }
bool wh_combo_draw_item(node *n, DRAWITEMSTRUCT *d) { (void)n; (void)d; return false; }
rgba wh_combo_list_bg(void) { return H.theme.bg; }
rgba wh_combo_list_text(void) { return H.theme.bg; }
void wh_command(node *n, uint16_t cmd, const uint8_t *args, size_t len) { (void)n; (void)cmd; (void)args; (void)len;  }
uint16_t wh_command_bar_button(node *n, HWND ctl, bool rtl, wchar_t *title, int cap) { (void)n; (void)ctl; (void)rtl; (void)title; (void)cap; return 0; }
void wh_command_bar_command(node *n, HWND ctl) { (void)n; (void)ctl;  }
bool wh_command_bar_is_primary(node *n, HWND ctl) { (void)n; (void)ctl; return false; }
void wh_corners_painted(HWND hwnd) { (void)hwnd;  }
void wh_crash_start(void) {  }
bool wh_create_nav(node *n) { (void)n; return false; }
LRESULT wh_date_notify(node *n, NMHDR *hdr, LPARAM lp, bool *handled) { (void)n; (void)hdr; (void)lp; *handled = false; return 0; }
void wh_destroy_native(node *n) { (void)n;  }
bool wh_destroy_nav(node *n) { (void)n; return false; }
bool wh_dock_item_launch(int argc, wchar_t **argv) { (void)argc; (void)argv; return false; }
void wh_dock_message(UINT m) { (void)m;  }
void wh_drop_picture(node *n) { (void)n;  }
void wh_drop_provider(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_end_wheel_pinch(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
bool wh_enter_transition(node *n, const float f[4]) { (void)n; (void)f; return false; }
bool wh_exit_transition(node *n) { (void)n; return false; }
void wh_exiting_forget(HWND hwnd) { (void)hwnd;  }
LRESULT wh_field_notify(node *n, NMHDR *hdr, LPARAM lp, bool *handled) { (void)n; (void)hdr; (void)lp; *handled = false; return 0; }
void wh_fit_covers(HWND hwnd) { (void)hwnd;  }
void wh_flush_routes(void) {  }
void wh_forget_interaction(node *n) { (void)n;  }
LRESULT wh_get_object(node *n, HWND hwnd, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)hwnd; (void)wp; (void)lp; *handled = false; return 0; }
void wh_gone_later(node *n) { (void)n;  }
uint16_t wh_header_button(node *n, HWND ctl, bool rtl, wchar_t *title, int cap) { (void)n; (void)ctl; (void)rtl; (void)title; (void)cap; return 0; }
void wh_header_command(node *n, HWND ctl) { (void)n; (void)ctl;  }
void wh_hide_bars(node *n) { (void)n;  }
void wh_hover_at(HWND top, POINT screen) { (void)top; (void)screen;  }
void wh_hover_views(int delta) { (void)delta;  }
void wh_image_close(node *n) { (void)n;  }
void wh_image_new(node *n) { (void)n;  }
void wh_image_size(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
void wh_image_tint_changed(node *n) { (void)n;  }
bool wh_in_exiting(HWND hwnd) { (void)hwnd; return false; }
bool wh_insert_nav(node *p, node *c, uint32_t index) { (void)p; (void)c; (void)index; return false; }
void wh_intl_env(wh_env *e) { (void)e;  }
float wh_k_for(float k, float zoom) { (void)k; (void)zoom; return k; }
void wh_layout_combo(node *n, RECT r) { (void)n; (void)r;  }
void wh_layout_date(node *n, RECT r) { (void)n; (void)r;  }
void wh_layout_nav(void) {  }
void wh_layout_screens(node *n) { (void)n;  }
void wh_layout_split(node *n) { (void)n;  }
void wh_list_dump(node *n, ak_buf *b, int depth, void (*walk)(ak_buf *, node *, int)) { (void)n; (void)b; (void)depth; (void)walk;  }
void wh_list_new(node *n) { (void)n;  }
void wh_list_rebind(node *n) { (void)n;  }
void wh_list_release(node *n) { (void)n;  }
void wh_list_resized(node *n) { (void)n;  }
void wh_list_set_direction(node *n, uint8_t d) { (void)n; (void)d;  }
void wh_list_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_make_combo(node *n) { (void)n;  }
void wh_make_date_controls(node *n) { (void)n;  }
void wh_make_rich(node *n) { (void)n;  }
void wh_make_split(node *n) { (void)n;  }
void wh_make_web(node *n) { (void)n;  }
void wh_match_batch_done(void) {  }
void wh_match_in(node *n) { (void)n;  }
bool wh_match_out(node *n) { (void)n; return false; }
void wh_materials_behind(HWND hwnd) { (void)hwnd;  }
void wh_measure_combo(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
void wh_measure_date(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
void wh_measure_native(node *n, float w, float h, float *ow, float *oh) { (void)n; (void)w; (void)h; (void)ow; (void)oh;  }
void wh_measure_pips(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
LRESULT wh_menu_bar_message(HWND hwnd, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)hwnd; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
bool wh_menu_command(int id) { (void)id; return false; }
LRESULT wh_menu_item_message(HWND hwnd, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)hwnd; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
LRESULT wh_module_message(UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_mouse_left(HWND hwnd) { (void)hwnd;  }
void wh_msg_timer(WPARAM id) { (void)id; if (id == TIMER_SCRIPT) wh_automation_timer(); }
void wh_native_host_callbacks(asuka_host_callbacks *cb) { (void)cb;  }
LRESULT wh_native_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
HWND wh_nav_bar_window(node *p) { (void)p; return NULL; }
wh_wctl * wh_nav_ctl(node *n) { (void)n; return NULL; }
void wh_nav_fitted(node *n, float w, float h) { (void)n; (void)w; (void)h;  }
bool wh_nav_flyout(node *n, ak_flyout *f) { (void)n; (void)f; return false; }
void wh_nav_min_size(node *n, float out[2]) { (void)n; (void)out;  }
void wh_nav_set_direction(node *n, uint8_t d) { (void)n; (void)d;  }
bool wh_nav_sized_by_host(node *n) { (void)n; return false; }
HWND wh_nav_window(node *n) { (void)n; return NULL; }
void wh_nav_window_dark(node *n, bool dark) { (void)n; (void)dark;  }
void wh_nav_window_redraw(node *n) { (void)n;  }
bool wh_open_bar_menu(const wchar_t *title) { (void)title; return false; }
float wh_own_zoom(const node *n) { (void)n; return 0; }
bool wh_page_wheel(node *n, bool horizontal, int delta) { (void)n; (void)horizontal; (void)delta; return false; }
void wh_paint_alert_dialog(node *n, gfx g, RECT r) { (void)n; (void)g; (void)r;  }
void wh_paint_canvas(node *n, gfx g) { (void)n; (void)g;  }
void wh_paint_clip_corners(node *n, HWND hwnd, HDC dc) { (void)n; (void)hwnd; (void)dc;  }
void wh_paint_command_bar(node *n, gfx g, RECT r) { (void)n; (void)g; (void)r;  }
void wh_paint_header(node *n, HDC dc, gfx g, RECT r) { (void)n; (void)dc; (void)g; (void)r;  }
void wh_paint_image(node *n, gfx g, float w, float h, float radius) { (void)n; (void)g; (void)w; (void)h; (void)radius;  }
void wh_paint_material(node *n, HDC dc, int w, int h) { (void)n; (void)dc; (void)w; (void)h;  }
void wh_paint_pane(node *n, HDC dc, gfx g, RECT r) { (void)n; (void)dc; (void)g; (void)r;  }
void wh_paint_picture(node *n, HDC dc, bool own) { (void)n; (void)dc; (void)own;  }
void wh_paint_pips(node *n, gfx g, float w, float h) { (void)n; (void)g; (void)w; (void)h;  }
void wh_paint_row(node *n, gfx g, float w, float h) { (void)n; (void)g; (void)w; (void)h;  }
void wh_paint_search_icon(node *n, HDC dc, RECT r) { (void)n; (void)dc; (void)r;  }
void wh_paint_shader(node *n, gfx g, float w, float h, float radius) { (void)n; (void)g; (void)w; (void)h; (void)radius;  }
void wh_paint_shape(node *n, gfx g) { (void)n; (void)g;  }
void wh_paint_snack(node *n, gfx g, float w, float h) { (void)n; (void)g; (void)w; (void)h;  }
void wh_paint_snack_text(node *n, HDC dc) { (void)n; (void)dc;  }
void wh_paint_tab_bar(node *n, HDC dc, gfx g, RECT r) { (void)n; (void)dc; (void)g; (void)r;  }
bool wh_paste_content(node *n) { (void)n; return false; }
void wh_pictures_stale(HWND hwnd) { (void)hwnd;  }
int wh_pip_at(node *n, int x) { (void)n; (void)x; return 0; }
void wh_place_picture(node *n) { (void)n;  }
void wh_pointer_moved(HWND hwnd, POINT screen, bool track) { (void)hwnd; (void)screen; (void)track;  }
void wh_pop_up(node *n, HWND anchor, bool below) { (void)n; (void)anchor; (void)below;  }
LRESULT wh_popover_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_read_intl(void) {  }
void wh_read_saved_locale(void) {  }
void wh_refresh_hover(void) {  }
bool wh_refresh_key(MSG *m) { (void)m; return false; }
void wh_remake_combo(node *n) { (void)n;  }
bool wh_remove_nav(node *p, node *c) { (void)p; (void)c; return false; }
void wh_remove_tray_icons(void) {  }
void wh_render_shader(node *n) { (void)n;  }
void wh_report_screen(node *n, float w, float h) { (void)n; (void)w; (void)h;  }
void wh_restart_scroll(node *n) { (void)n;  }
void wh_retheme_rich(node *n) { (void)n;  }
void wh_rich_edited(node *n) { (void)n;  }
const wchar_t * wh_rich_field_class(node *n) { (void)n; return NULL; }
void wh_rich_invalidate(node *n) { (void)n;  }
void wh_rich_measure(node *n, float cw, float ch, float aw, float ah, float *w, float *h) { (void)n; (void)cw; (void)ch; (void)aw; (void)ah; (void)w; (void)h;  }
LRESULT wh_rich_notify(node *n, NMHDR *hdr, LPARAM lp, bool *handled) { (void)n; (void)hdr; (void)lp; *handled = false; return 0; }
void wh_rich_place(node *n) { (void)n;  }
void wh_rich_place_inlines(node *n) { (void)n;  }
void wh_rich_release(node *n) { (void)n;  }
void wh_rich_resized(node *n) { (void)n;  }
void wh_rich_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
node * wh_root_of(HWND top) { (void)top; return NULL; }
void wh_round_covers(HWND hwnd) { (void)hwnd;  }
bool wh_route_key(MSG *m) { (void)m; return false; }
bool wh_row_menu(node *n) { (void)n; return false; }
void wh_scroll_bar_message(node *n, bool horizontal, int code) { (void)n; (void)horizontal; (void)code;  }
void wh_scroll_by(node *n, float dx, float dy) { (void)n; (void)dx; (void)dy;  }
void wh_scroll_to_offset(node *n, float x, float y) { (void)n; (void)x; (void)y;  }
void wh_search_event(node *n, ak_event *e) { (void)n; (void)e;  }
bool wh_select_click(node *n) { (void)n; return false; }
void wh_select_pip(node *n, int i) { (void)n; (void)i;  }
void wh_set_a11y(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_accessible_name(HWND hwnd, const wchar_t *name) { (void)hwnd; (void)name;  }
void wh_set_combo_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_date_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_image_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_keys(node *n, const uint8_t *b, size_t len) { (void)n; (void)b; (void)len;  }
void wh_set_match(node *n, ak_span key) { (void)n; (void)key;  }
void wh_set_material(node *n, int32_t m) { (void)n; (void)m;  }
void wh_set_native_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_nav_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_refreshable(node *n, bool on) { (void)n; (void)on;  }
void wh_set_refreshing(node *n, bool on) { (void)n; (void)on;  }
void wh_set_rich_text(node *n, const uint8_t *b, size_t len) { (void)n; (void)b; (void)len;  }
void wh_set_shader_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_split_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_tooltip(node *n, const wchar_t *tip) { (void)n; (void)tip;  }
void wh_set_transform(node *n, const uint8_t *b, size_t len) { (void)n; (void)b; (void)len;  }
void wh_setup_rich_edit(node *n) { (void)n;  }
void wh_shader_new(node *n) { (void)n;  }
void wh_shader_release(node *n) { (void)n;  }
void wh_shape_clip(node *n) { (void)n;  }
void wh_shape_new(node *n) { (void)n;  }
void wh_shape_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
LRESULT wh_sheet_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_snack_clicked(node *n, HWND ctl) { (void)n; (void)ctl;  }
bool wh_snack_is_close(node *n, HWND ctl) { (void)n; (void)ctl; return false; }
void wh_start_drag(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_start_toasts(void) {  }
void wh_style_combo_list(node *n) { (void)n;  }
bool wh_tap_nav(const wchar_t *text) { (void)text; return false; }
void wh_theme_date(node *n) { (void)n;  }
void wh_tick(void) {  }
bool wh_touch_gesture(node *n, HWND hwnd, LPARAM lp) { (void)n; (void)hwnd; (void)lp; return false; }
void wh_transformed(node *n, float x, float y, float w, float h, int *x0, int *y0, int *x1, int *y1) { (void)n; (void)x; (void)y; (void)w; (void)h; (void)x0; (void)y0; (void)x1; (void)y1;  }
bool wh_tray_icon_rect(uint32_t id, RECT *r) { (void)id; (void)r; return false; }
bool wh_tree_in_picture(node *n) { (void)n; return false; }
void wh_uia_range_changed(node *n, double old, double now) { (void)n; (void)old; (void)now;  }
void wh_update_bars(node *n) { (void)n;  }
void wh_update_picture(node *n) { (void)n;  }
void wh_update_scroll(node *n) { (void)n;  }
void wh_view_timer(node *n, HWND hwnd, WPARAM id) { (void)n; (void)hwnd; (void)id;  }
int wh_visible_text(node *n, wchar_t *out, int cap) { (void)n; (void)out; (void)cap; return 0; }
void wh_wait_for_toast(void) {  }
void wh_web_close(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_web_close_all(void) {  }
void wh_web_color_scheme(node *n, bool dark) { (void)n; (void)dark;  }
void wh_web_resized(node *n) { (void)n;  }
void wh_web_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
bool wh_wheel(node *n, bool horizontal, int delta) { (void)n; (void)horizontal; (void)delta; return false; }
bool wh_wheel_pinch(node *n, HWND hwnd, int delta, LPARAM lp) { (void)n; (void)hwnd; (void)delta; (void)lp; return false; }
void wh_window_destroyed(HWND hwnd) { (void)hwnd;  }
void wh_window_dpi_changed(node *frame, int dpi, const RECT *suggested) { (void)frame; (void)dpi; (void)suggested;  }
LRESULT wh_window_frame_message(node *frame, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)frame; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
const wchar_t * wh_word(const char *key) { (void)key; return L""; }
float wh_zoom_up_to(HWND hwnd, HWND top) { (void)hwnd; (void)top; return 1; }
