// login.c: platform/login on Linux: an XDG autostart entry of the user's
// ($XDG_CONFIG_HOME/autostart/<id>.desktop), which GNOME's and KDE's
// sessions start as the user logs in, and GNOME Tweaks and KDE's settings
// list. In a Flatpak the sandbox can't write it: the Background portal
// asks the user and writes it (RequestBackground with autostart), and the
// app keeps the answer, which it can't read back. The entry starts the
// app with --asuka-opened-at-login, which main.c takes out of its
// arguments (openedAtLogin).
#include "host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

void lx_desktop_exec(ak_buf *b, const char *const *args, size_t n);
void lx_desktop_value(ak_buf *b, const char *s);

#define LOGIN_ARG "--asuka-opened-at-login"

// lx_in_flatpak reports whether the app runs in a Flatpak sandbox.
bool lx_in_flatpak(void) {
	const char *f = getenv("FLATPAK_ID");
	struct stat st;
	return (f && *f) || stat("/.flatpak-info", &st) == 0;
}

static char *autostart_file(void) {
	char name[300];
	snprintf(name, sizeof name, "autostart/%s.desktop", S(H.app_id));
	return dsk_join(g_get_user_config_dir(), name);
}

static char *grant_file(void) { return dsk_join(lx_data_dir(), "login-autostart"); }

// autostart_enabled reports whether an autostart entry starts the app:
// neither hidden nor turned off (GNOME's key, which its settings write).
static bool autostart_enabled(char *entry) {
	bool in_entry = false, on = true;
	for (char *line = strtok(entry, "\n"); line; line = strtok(NULL, "\n")) {
		while (*line == ' ' || *line == '\t') line++;
		size_t n = strlen(line);
		while (n && (line[n - 1] == ' ' || line[n - 1] == '\r')) line[--n] = 0;
		if (line[0] == '[') {
			in_entry = !strcmp(line, "[Desktop Entry]");
			continue;
		}
		char *eq = strchr(line, '=');
		if (!in_entry || !eq) continue;
		*eq = 0;
		char *k = line, *v = eq + 1;
		size_t kn = strlen(k);
		while (kn && k[kn - 1] == ' ') k[--kn] = 0;
		while (*v == ' ') v++;
		if (!strcmp(k, "Hidden") && !strcmp(v, "true")) on = false;
		if (!strcmp(k, "X-GNOME-Autostart-enabled") && !strcmp(v, "false")) on = false;
	}
	return on;
}

static char *set_autostart(bool on) {
	char *path = autostart_file(), *err = NULL;
	if (!on) {
		if (remove(path) != 0 && errno != ENOENT) err = g_strdup_printf("remove %s: %s", path, g_strerror(errno));
		free(path);
		return err;
	}
	char *exe = dsk_exe_path();
	const char *argv[] = {S(exe), LOGIN_ARG};
	ak_buf exec = {0}, e = {0};
	lx_desktop_exec(&exec, argv, 2);
	ak_buf_put(&e, "[Desktop Entry]\nType=Application\nName=", 38);
	lx_desktop_value(&e, S(H.app_name));
	ak_buf_put(&e, "\nExec=", 6);
	lx_desktop_value(&e, lx_buf_cstr(&exec));
	ak_buf_put(&e, "\nIcon=", 6);
	lx_desktop_value(&e, S(H.app_id));
	ak_buf_printf(&e, "\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\n");
	char *dir = dsk_dir(path);
	if (!dsk_mkdirs(dir) || !dsk_write_file(path, e.data, e.len)) err = g_strdup_printf("can't write %s", path);
	free(dir);
	ak_buf_free(&exec);
	ak_buf_free(&e);
	free(exe);
	free(path);
	return err;
}

typedef struct {
	bool on;
	reply *r;
} portal_ask;

static GVariant *background_params(const char *token, void *ctx) {
	portal_ask *a = ctx;
	char reason[512];
	snprintf(reason, sizeof reason, "Open %s when you log in", S(H.app_name));
	char *exe = dsk_exe_path();
	const char *cmd[] = {S(exe), LOGIN_ARG, NULL};
	GVariantBuilder o;
	g_variant_builder_init(&o, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&o, "{sv}", "handle_token", g_variant_new_string(token));
	g_variant_builder_add(&o, "{sv}", "reason", g_variant_new_string(reason));
	g_variant_builder_add(&o, "{sv}", "autostart", g_variant_new_boolean(a->on));
	g_variant_builder_add(&o, "{sv}", "commandline", g_variant_new_strv(cmd, -1));
	free(exe);
	return g_variant_new("(sa{sv})", "", &o);
}

static void background_answer(void *ctx, uint32_t code, GVariant *results, const char *err) {
	portal_ask *a = ctx;
	gboolean granted = 0;
	if (results) g_variant_lookup(results, "autostart", "b", &granted);
	if (err)
		reply_err(a->r, "%s", err);
	else if (code == 1)
		reply_err(a->r, "cancelled");
	else if (code)
		reply_err(a->r, "login: the Background portal refused");
	else if (a->on && !granted)
		reply_err(a->r, "login: the user didn't allow the app to start at login");
	else {
		char *g = grant_file(), *dir = dsk_dir(g);
		dsk_mkdirs(dir);
		dsk_write_file(g, a->on ? "1" : "0", 1);
		free(dir);
		free(g);
		reply_ok(a->r, NULL, 0);
	}
	free(a);
}

// set: u8 on.
static void login_set(ak_span args, reply *r) {
	bool on = args.len > 0 && args.p[0] == 1;
	if (lx_in_flatpak()) {
		portal_ask *a = malloc(sizeof *a);
		if (!a) abort();
		*a = (portal_ask){on, r};
		lx_portal_request("org.freedesktop.portal.Background", "RequestBackground", background_params, background_answer, a);
		return;
	}
	char *err = set_autostart(on);
	if (err)
		reply_err(r, "%s", err);
	else
		reply_ok(r, NULL, 0);
	g_free(err);
}

// enabled: u8.
static void login_enabled(ak_span args, reply *r) {
	(void)args;
	bool on = false;
	size_t n;
	if (lx_in_flatpak()) {
		char *g = grant_file(), *b = dsk_read_file(g, &n);
		on = b && b[0] == '1';
		free(b);
		free(g);
	} else {
		char *p = autostart_file(), *b = dsk_read_file(p, &n);
		on = b && autostart_enabled(b);
		free(b);
		free(p);
	}
	uint8_t v = on;
	reply_ok(r, &v, 1);
}

static void login_opened(ak_span args, reply *r) {
	(void)args;
	uint8_t v = lx_opened_at_login;
	reply_ok(r, &v, 1);
}

static const method_t login_methods[] = {
	{"login", "set", login_set},
	{"login", "enabled", login_enabled},
	{"login", "openedAtLogin", login_opened},
};

static void login_modules(void) { lx_register_methods(login_methods, AK_LEN(login_methods)); }

const area lx_area_login = {
	.name = "login",
	.modules = login_modules,
};
