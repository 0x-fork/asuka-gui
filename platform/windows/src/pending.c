// pending.c: what the host's parts not yet ported answer meanwhile:
// each goes as its part is written. Nothing here is the host's behavior.
#include "nav.h"

void wh_accept_drops(HWND win) { (void)win;  }
bool wh_actions_menu(node *n, HWND hwnd, LPARAM lp) { (void)n; (void)hwnd; (void)lp; return false; }
bool wh_adjust_key(node *n, WPARAM vk) { (void)n; (void)vk; return false; }
HBRUSH wh_alert_dialog_colors(HDC dc) { (void)dc; return NULL; }
void wh_alert_dialog_command(node *n, HWND ctl, UINT code) { (void)n; (void)ctl; (void)code;  }
bool wh_alert_dialog_default_button(node *n, HWND ctl) { (void)n; (void)ctl; return false; }
LRESULT wh_alert_dialog_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
HWND wh_alert_dialog_window(node *n) { (void)n; return NULL; }
void wh_alert_field_event(node *n, ak_event *e) { (void)n; (void)e;  }
void wh_allow_gestures(node *n) { (void)n;  }
void wh_announce(const char *s, size_t len) { (void)s; (void)len;  }
void wh_announce_choice(node *b) { (void)b;  }
HICON wh_app_icon(void) { return NULL; }
void wh_badge_message(UINT m) { (void)m;  }
void wh_call(node *root, const uint8_t *args, size_t len) { (void)root; (void)args; (void)len;  }
void wh_cancel_alert_dialog(node *n) { (void)n;  }
void wh_cancel_popover(node *n) { (void)n;  }
void wh_clear_accessible_name(HWND hwnd) { (void)hwnd;  }
void wh_clip_tree(node *n, HDC dc) { (void)n; (void)dc;  }
void wh_close_alert_dialog(node *n) { (void)n;  }
void wh_close_popover(node *n) { (void)n;  }
void wh_combo_command(node *n, HWND ctl, UINT code) { (void)n; (void)ctl; (void)code;  }
bool wh_combo_draw_item(node *n, DRAWITEMSTRUCT *d) { (void)n; (void)d; return false; }
rgba wh_combo_list_bg(void) { return H.theme.bg; }
rgba wh_combo_list_text(void) { return H.theme.bg; }
void wh_corners_painted(HWND hwnd) { (void)hwnd;  }
void wh_crash_start(void) {  }
LRESULT wh_date_notify(node *n, NMHDR *hdr, LPARAM lp, bool *handled) { (void)n; (void)hdr; (void)lp; *handled = false; return 0; }
void wh_destroy_native(node *n) { (void)n;  }
void wh_destroy_snack(node *n) { (void)n;  }
bool wh_dock_item_launch(int argc, wchar_t **argv) { (void)argc; (void)argv; return false; }
void wh_dock_message(UINT m) { (void)m;  }
void wh_drop_picture(node *n) { (void)n;  }
void wh_drop_provider(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_end_wheel_pinch(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_forget_interaction(node *n) { (void)n;  }
LRESULT wh_get_object(node *n, HWND hwnd, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)hwnd; (void)wp; (void)lp; *handled = false; return 0; }
void wh_hover_at(HWND top, POINT screen) { (void)top; (void)screen;  }
void wh_hover_views(int delta) { (void)delta;  }
void wh_image_close(node *n) { (void)n;  }
void wh_image_new(node *n) { (void)n;  }
void wh_image_size(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
void wh_image_tint_changed(node *n) { (void)n;  }
void wh_layout_combo(node *n, RECT r) { (void)n; (void)r;  }
void wh_layout_date(node *n, RECT r) { (void)n; (void)r;  }
void wh_layout_split(node *n) { (void)n;  }
void wh_make_combo(node *n) { (void)n;  }
void wh_make_date_controls(node *n) { (void)n;  }
void wh_make_split(node *n) { (void)n;  }
void wh_make_web(node *n) { (void)n;  }
void wh_materials_behind(HWND hwnd) { (void)hwnd;  }
void wh_measure_combo(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
void wh_measure_date(node *n, float *w, float *h) { (void)n; (void)w; (void)h;  }
void wh_measure_native(node *n, float w, float h, float *ow, float *oh) { (void)n; (void)w; (void)h; (void)ow; (void)oh;  }
LRESULT wh_module_message(UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_module_timer(WPARAM id) { (void)id;  }
void wh_mouse_left(HWND hwnd) { (void)hwnd;  }
void wh_native_host_callbacks(asuka_host_callbacks *cb) { (void)cb;  }
LRESULT wh_native_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_open_alert_dialog(node *n) { (void)n;  }
void wh_open_popover(node *n) { (void)n;  }
void wh_paint_alert_dialog(node *n, gfx g, RECT r) { (void)n; (void)g; (void)r;  }
void wh_paint_canvas(node *n, gfx g) { (void)n; (void)g;  }
void wh_paint_clip_corners(node *n, HWND hwnd, HDC dc) { (void)n; (void)hwnd; (void)dc;  }
void wh_paint_image(node *n, gfx g, float w, float h, float radius) { (void)n; (void)g; (void)w; (void)h; (void)radius;  }
void wh_paint_material(node *n, HDC dc, int w, int h) { (void)n; (void)dc; (void)w; (void)h;  }
void wh_paint_pane(node *n, HDC dc, gfx g, RECT r) { (void)n; (void)dc; (void)g; (void)r;  }
void wh_paint_picture(node *n, HDC dc, bool own) { (void)n; (void)dc; (void)own;  }
void wh_paint_shader(node *n, gfx g, float w, float h, float radius) { (void)n; (void)g; (void)w; (void)h; (void)radius;  }
void wh_paint_shape(node *n, gfx g) { (void)n; (void)g;  }
void wh_paint_snack(node *n, gfx g, float w, float h) { (void)n; (void)g; (void)w; (void)h;  }
void wh_paint_snack_text(node *n, HDC dc) { (void)n; (void)dc;  }
bool wh_paste_content(node *n) { (void)n; return false; }
void wh_pictures_stale(HWND hwnd) { (void)hwnd;  }
void wh_place_picture(node *n) { (void)n;  }
void wh_place_snacks(void) {  }
void wh_pointer_moved(HWND hwnd, POINT screen, bool track) { (void)hwnd; (void)screen; (void)track;  }
LRESULT wh_popover_message(node *n, UINT m, WPARAM wp, LPARAM lp, bool *handled) { (void)n; (void)m; (void)wp; (void)lp; *handled = false; return 0; }
void wh_present_snack(node *n) { (void)n;  }
void wh_refresh_hover(void) {  }
void wh_remake_combo(node *n) { (void)n;  }
void wh_remove_tray_icons(void) {  }
void wh_render_shader(node *n) { (void)n;  }
bool wh_route_key(MSG *m) { (void)m; return false; }
void wh_set_a11y(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_accessible_name(HWND hwnd, const wchar_t *name) { (void)hwnd; (void)name;  }
void wh_set_combo_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_date_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_image_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_keys(node *n, const uint8_t *b, size_t len) { (void)n; (void)b; (void)len;  }
void wh_set_material(node *n, int32_t m) { (void)n; (void)m;  }
void wh_set_native_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_popover_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_shader_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_snack_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_split_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_set_tooltip(node *n, const wchar_t *tip) { (void)n; (void)tip;  }
void wh_shader_new(node *n) { (void)n;  }
void wh_shader_release(node *n) { (void)n;  }
void wh_shape_clip(node *n) { (void)n;  }
void wh_shape_new(node *n) { (void)n;  }
void wh_shape_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
void wh_share_text(const char *s, size_t n) { (void)s; (void)n;  }
void wh_snack_clicked(node *n, HWND ctl) { (void)n; (void)ctl;  }
bool wh_snack_is_close(node *n, HWND ctl) { (void)n; (void)ctl; return false; }
void wh_start_drag(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_start_toasts(void) {  }
void wh_style_combo_list(node *n) { (void)n;  }
void wh_theme_date(node *n) { (void)n;  }
bool wh_touch_gesture(node *n, HWND hwnd, LPARAM lp) { (void)n; (void)hwnd; (void)lp; return false; }
bool wh_tray_icon_rect(uint32_t id, RECT *r) { (void)id; (void)r; return false; }
bool wh_tree_in_picture(node *n) { (void)n; return false; }
void wh_uia_range_changed(node *n, double old, double now) { (void)n; (void)old; (void)now;  }
void wh_update_alert_dialog(node *n) { (void)n;  }
void wh_update_picture(node *n) { (void)n;  }
void wh_view_timer(node *n, HWND hwnd, WPARAM id) { (void)n; (void)hwnd; (void)id;  }
void wh_wait_for_toast(void) {  }
void wh_web_close(node *n, HWND hwnd) { (void)n; (void)hwnd;  }
void wh_web_close_all(void) {  }
void wh_web_color_scheme(node *n, bool dark) { (void)n; (void)dark;  }
void wh_web_command(node *n, uint16_t cmd, const uint8_t *args, size_t len) { (void)n; (void)cmd; (void)args; (void)len;  }
void wh_web_resized(node *n) { (void)n;  }
void wh_web_set_prop(node *n, uint16_t p, const ak_value *v) { (void)n; (void)p; (void)v;  }
bool wh_wheel_pinch(node *n, HWND hwnd, int delta, LPARAM lp) { (void)n; (void)hwnd; (void)delta; (void)lp; return false; }
void wh_window_destroyed(HWND hwnd) { (void)hwnd;  }
