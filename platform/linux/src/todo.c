// todo.c: what isn't ported yet (stubs, removed as each area lands).
#include "host.h"

void lx_tray_style_changed(void) {}
bool lx_tray_click(uint32_t icon, int *x, int *y) { (void)icon; (void)x; (void)y; return false; }
void lx_tray_flyout_closed(uint32_t icon) { (void)icon; }
int lx_notify_headless(const char *id) { (void)id; return 0; }
void lx_install_notify_actions(void) {}
void lx_notify_action_at_start(void) {}
void lx_watch_launches(void);
