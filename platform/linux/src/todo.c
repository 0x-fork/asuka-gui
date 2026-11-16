// todo.c: what isn't ported yet (stubs, removed as each area lands).
#include "host.h"

int lx_notify_headless(const char *id) { (void)id; return 0; }
void lx_install_notify_actions(void) {}
void lx_notify_action_at_start(void) {}
void lx_watch_launches(void);
