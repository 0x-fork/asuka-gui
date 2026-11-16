// files.c (to come): launches; for now the first activation starts.
#include "host.h"

void lx_start(void);
static bool started;

static void on_activate(GApplication *a, gpointer d) {
	(void)a;
	(void)d;
	if (!started) {
		started = true;
		lx_start();
	}
}

static int on_command_line(GApplication *a, GApplicationCommandLine *cl, gpointer d) {
	(void)cl;
	on_activate(a, d);
	return 0;
}

void lx_watch_launches(void) {
	g_signal_connect(H.app, "activate", G_CALLBACK(on_activate), NULL);
	g_signal_connect(H.app, "command-line", G_CALLBACK(on_command_line), NULL);
}
