// asuka_linux.h: GLib, GObject, GIO, GTK 4 and libadwaita for native
// packages' Linux C (RFC 0008): our own declarations of the subset the
// packages use, their symbols resolved with dlopen and dlsym the first
// time asuka_linux() is called (platform/desktop/src/linux.c), so a
// package builds from any system with Zig, no GTK sysroot, as the host
// does. A call is G(name)(args): G(g_variant_new)("(s)", s).
//
// Note for the Linux host (platform/linux): these are the same kind of
// declarations it makes for itself; when it has its own, the two should
// become one header, the host's.
#ifndef ASUKA_LINUX_H
#define ASUKA_LINUX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asuka_desktop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int gboolean;
typedef char gchar;
typedef void *gpointer;
typedef const void *gconstpointer;
typedef unsigned int guint;
typedef int gint;
typedef unsigned long gulong;
typedef uint32_t guint32;
typedef int32_t gint32;
typedef uint64_t guint64;
typedef int64_t gint64;
typedef uint8_t guchar;
typedef int16_t gint16;
typedef uint16_t guint16;
typedef double gdouble;
typedef size_t gsize;
typedef unsigned long GType;
typedef guint32 GQuark;

typedef struct {
	GQuark domain;
	gint code;
	gchar *message;
} GError;

typedef struct _GVariant GVariant;
typedef struct _GVariantType GVariantType;
typedef struct _GVariantIter GVariantIter;
// A GVariantBuilder is a stack object of GLib's size (16 pointers' worth).
typedef struct {
	gsize x[16];
} GVariantBuilder;
// A GVariantIter likewise.
struct _GVariantIter {
	gsize x[16];
};
typedef struct _GObject GObject;
typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;
typedef struct _GCancellable GCancellable;
typedef struct _GAsyncResult GAsyncResult;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GDBusProxy GDBusProxy;
typedef struct _GDBusMessage GDBusMessage;
typedef struct _GUnixFDList GUnixFDList;
typedef struct _GNetworkMonitor GNetworkMonitor;
typedef struct _GPowerProfileMonitor GPowerProfileMonitor;
typedef struct _GSettings GSettings;
typedef struct _GSettingsSchemaSource GSettingsSchemaSource;
typedef struct _GSettingsSchema GSettingsSchema;
typedef struct _GFile GFile;
typedef struct _GFileMonitor GFileMonitor;
typedef struct _GFileInfo GFileInfo;
typedef struct _GAppInfo GAppInfo;
typedef struct _GAppLaunchContext GAppLaunchContext;
typedef struct _GBytes GBytes;
typedef struct _GHashTable GHashTable;
typedef struct _GList GList;
struct _GList {
	gpointer data;
	GList *next, *prev;
};
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GdkDisplay GdkDisplay;
typedef struct _GdkMonitor GdkMonitor;
typedef struct _GdkClipboard GdkClipboard;
typedef struct _GdkTexture GdkTexture;
typedef struct _GdkPaintable GdkPaintable;
typedef struct _GListModel GListModel;

typedef void (*GCallback)(void);
typedef void (*GDestroyNotify)(gpointer data);
typedef void (*GClosureNotify)(gpointer data, gpointer closure);
typedef gboolean (*GSourceFunc)(gpointer data);
typedef void (*GAsyncReadyCallback)(GObject *source, GAsyncResult *res, gpointer data);
typedef void (*GDBusSignalCallback)(GDBusConnection *conn, const gchar *sender, const gchar *path, const gchar *iface,
                                    const gchar *signal, GVariant *params, gpointer data);

enum { G_BUS_TYPE_SYSTEM = 1, G_BUS_TYPE_SESSION = 2 };
enum { G_DBUS_CALL_FLAGS_NONE = 0 };
enum { G_DBUS_SIGNAL_FLAGS_NONE = 0 };
enum { G_DBUS_PROXY_FLAGS_NONE = 0 };
enum { G_NETWORK_CONNECTIVITY_LOCAL = 1, G_NETWORK_CONNECTIVITY_LIMITED, G_NETWORK_CONNECTIVITY_PORTAL, G_NETWORK_CONNECTIVITY_FULL };
enum { G_PRIORITY_DEFAULT = 0, G_PRIORITY_DEFAULT_IDLE = 200 };
enum { G_FILE_MONITOR_NONE = 0, G_FILE_MONITOR_WATCH_MOVES = 8 };
enum { G_FILE_QUERY_INFO_NONE = 0 };

#define G_VARIANT_TYPE(s) ((const GVariantType *)(s))

// The functions: lib (glib, gobject, gio, gtk, adw), result, name, params.
#define ASUKA_LINUX_FUNCS(X)                                                                                   \
	X(glib, void, g_free, (gpointer p))                                                                         \
	X(glib, gchar *, g_strdup, (const gchar *s))                                                                \
	X(glib, void, g_error_free, (GError * e))                                                                   \
	X(glib, guint, g_idle_add_full, (gint priority, GSourceFunc fn, gpointer data, GDestroyNotify notify))       \
	X(glib, guint, g_timeout_add_full, (gint priority, guint ms, GSourceFunc fn, gpointer data, GDestroyNotify n)) \
	X(glib, gboolean, g_source_remove, (guint id))                                                              \
	X(glib, GMainContext *, g_main_context_default, (void))                                                     \
	X(glib, gboolean, g_main_context_iteration, (GMainContext * c, gboolean may_block))                         \
	X(glib, GMainLoop *, g_main_loop_new, (GMainContext * c, gboolean running))                                 \
	X(glib, void, g_main_loop_run, (GMainLoop * l))                                                             \
	X(glib, void, g_main_loop_quit, (GMainLoop * l))                                                            \
	X(glib, void, g_main_loop_unref, (GMainLoop * l))                                                           \
	X(glib, GVariant *, g_variant_new, (const gchar *format, ...))                                              \
	X(glib, void, g_variant_get, (GVariant * v, const gchar *format, ...))                                      \
	X(glib, GVariant *, g_variant_ref_sink, (GVariant * v))                                                     \
	X(glib, void, g_variant_unref, (GVariant * v))                                                              \
	X(glib, GVariant *, g_variant_get_child_value, (GVariant * v, gsize i))                                     \
	X(glib, gsize, g_variant_n_children, (GVariant * v))                                                        \
	X(glib, GVariant *, g_variant_get_variant, (GVariant * v))                                                  \
	X(glib, const gchar *, g_variant_get_type_string, (GVariant * v))                                           \
	X(glib, gboolean, g_variant_is_of_type, (GVariant * v, const GVariantType *t))                              \
	X(glib, const gchar *, g_variant_get_string, (GVariant * v, gsize * len))                                   \
	X(glib, gboolean, g_variant_get_boolean, (GVariant * v))                                                    \
	X(glib, guchar, g_variant_get_byte, (GVariant * v))                                                         \
	X(glib, gint32, g_variant_get_int32, (GVariant * v))                                                        \
	X(glib, guint32, g_variant_get_uint32, (GVariant * v))                                                      \
	X(glib, gint64, g_variant_get_int64, (GVariant * v))                                                        \
	X(glib, guint64, g_variant_get_uint64, (GVariant * v))                                                      \
	X(glib, gint16, g_variant_get_int16, (GVariant * v))                                                        \
	X(glib, guint16, g_variant_get_uint16, (GVariant * v))                                                      \
	X(glib, gdouble, g_variant_get_double, (GVariant * v))                                                      \
	X(glib, GVariant *, g_variant_lookup_value, (GVariant * dict, const gchar *key, const GVariantType *t))     \
	X(glib, gconstpointer, g_variant_get_fixed_array, (GVariant * v, gsize * n, gsize size))                    \
	X(glib, GVariant *, g_variant_new_string, (const gchar *s))                                                 \
	X(glib, GVariant *, g_variant_new_boolean, (gboolean b))                                                    \
	X(glib, GVariant *, g_variant_new_uint32, (guint32 v))                                                      \
	X(glib, GVariant *, g_variant_new_int32, (gint32 v))                                                        \
	X(glib, GVariant *, g_variant_new_uint64, (guint64 v))                                                      \
	X(glib, GVariant *, g_variant_new_double, (gdouble v))                                                      \
	X(glib, GVariant *, g_variant_new_variant, (GVariant * v))                                                  \
	X(glib, GVariant *, g_variant_new_object_path, (const gchar *p))                                            \
	X(glib, GVariant *, g_variant_new_fixed_array, (const GVariantType *t, gconstpointer p, gsize n, gsize size)) \
	X(glib, void, g_variant_builder_init, (GVariantBuilder * b, const GVariantType *t))                         \
	X(glib, void, g_variant_builder_add, (GVariantBuilder * b, const gchar *format, ...))                       \
	X(glib, GVariant *, g_variant_builder_end, (GVariantBuilder * b))                                           \
	X(glib, void, g_variant_builder_clear, (GVariantBuilder * b))                                               \
	X(glib, gsize, g_variant_iter_init, (GVariantIter * it, GVariant *v))                                       \
	X(glib, gboolean, g_variant_iter_next, (GVariantIter * it, const gchar *format, ...))                       \
	X(glib, GVariant *, g_variant_iter_next_value, (GVariantIter * it))                                         \
	X(glib, GBytes *, g_bytes_new, (gconstpointer p, gsize n))                                                  \
	X(glib, gconstpointer, g_bytes_get_data, (GBytes * b, gsize * n))                                           \
	X(glib, void, g_bytes_unref, (GBytes * b))                                                                  \
	X(glib, void, g_list_free, (GList * l))                                                                     \
	X(glib, const gchar *, g_get_user_name, (void))                                                             \
	X(glib, const gchar *, g_get_real_name, (void))                                                             \
	X(glib, const gchar *, g_get_host_name, (void))                                                             \
	X(gobject, gpointer, g_object_ref, (gpointer o))                                                            \
	X(gobject, void, g_object_unref, (gpointer o))                                                              \
	X(gobject, void, g_object_get, (gpointer o, const gchar *first, ...))                                       \
	X(gobject, void, g_object_set, (gpointer o, const gchar *first, ...))                                       \
	X(gobject, gulong, g_signal_connect_data, (gpointer o, const gchar *signal, GCallback cb, gpointer data, GClosureNotify notify, int flags)) \
	X(gobject, void, g_signal_handler_disconnect, (gpointer o, gulong id))                                      \
	X(gio, GDBusConnection *, g_bus_get_sync, (int bus, GCancellable *c, GError **e))                           \
	X(gio, GVariant *, g_dbus_connection_call_sync, (GDBusConnection * c, const gchar *name, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, const GVariantType *reply, int flags, gint timeout, GCancellable *cancel, GError **e)) \
	X(gio, void, g_dbus_connection_call, (GDBusConnection * c, const gchar *name, const gchar *path, const gchar *iface, const gchar *method, GVariant *params, const GVariantType *reply, int flags, gint timeout, GCancellable *cancel, GAsyncReadyCallback cb, gpointer data)) \
	X(gio, GVariant *, g_dbus_connection_call_finish, (GDBusConnection * c, GAsyncResult *res, GError **e))     \
	X(gio, guint, g_dbus_connection_signal_subscribe, (GDBusConnection * c, const gchar *sender, const gchar *iface, const gchar *member, const gchar *path, const gchar *arg0, int flags, GDBusSignalCallback cb, gpointer data, GDestroyNotify notify)) \
	X(gio, void, g_dbus_connection_signal_unsubscribe, (GDBusConnection * c, guint id))                         \
	X(gio, const gchar *, g_dbus_connection_get_unique_name, (GDBusConnection * c))                             \
	X(gio, GCancellable *, g_cancellable_new, (void))                                                           \
	X(gio, void, g_cancellable_cancel, (GCancellable * c))                                                      \
	X(gio, gboolean, g_cancellable_is_cancelled, (GCancellable * c))                                            \
	X(gio, GNetworkMonitor *, g_network_monitor_get_default, (void))                                            \
	X(gio, gboolean, g_network_monitor_get_network_available, (GNetworkMonitor * m))                            \
	X(gio, gboolean, g_network_monitor_get_network_metered, (GNetworkMonitor * m))                              \
	X(gio, int, g_network_monitor_get_connectivity, (GNetworkMonitor * m))                                      \
	X(gio, GPowerProfileMonitor *, g_power_profile_monitor_dup_default, (void))                                 \
	X(gio, gboolean, g_power_profile_monitor_get_power_saver_enabled, (GPowerProfileMonitor * m))               \
	X(gio, GSettingsSchemaSource *, g_settings_schema_source_get_default, (void))                               \
	X(gio, GSettingsSchema *, g_settings_schema_source_lookup, (GSettingsSchemaSource * s, const gchar *id, gboolean recursive)) \
	X(gio, void, g_settings_schema_unref, (GSettingsSchema * s))                                                \
	X(gio, gboolean, g_settings_schema_has_key, (GSettingsSchema * s, const gchar *key))                        \
	X(gio, GSettings *, g_settings_new, (const gchar *schema))                                                  \
	X(gio, gchar *, g_settings_get_string, (GSettings * s, const gchar *key))                                   \
	X(gio, gboolean, g_settings_get_boolean, (GSettings * s, const gchar *key))                                 \
	X(gio, gint, g_settings_get_enum, (GSettings * s, const gchar *key))                                        \
	X(gio, gdouble, g_settings_get_double, (GSettings * s, const gchar *key))                                   \
	X(gio, GFile *, g_file_new_for_path, (const gchar *path))                                                   \
	X(gio, GFile *, g_file_new_for_uri, (const gchar *uri))                                                     \
	X(gio, gchar *, g_file_get_uri, (GFile * f))                                                                \
	X(gio, gchar *, g_file_get_path, (GFile * f))                                                               \
	X(gio, gboolean, g_app_info_launch_default_for_uri, (const gchar *uri, GAppLaunchContext *ctx, GError **e))  \
	X(glib, GQuark, g_quark_from_static_string, (const gchar *s))                                               \
	X(gio, GQuark, g_io_error_quark, (void))                                                                    \
	X(gtk, GQuark, gtk_dialog_error_quark, (void))                                                              \
	X(gtk, gpointer, gtk_file_launcher_new, (GFile * f))                                                        \
	X(gtk, void, gtk_file_launcher_launch, (gpointer l, GtkWindow *parent, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, gboolean, gtk_file_launcher_launch_finish, (gpointer l, GAsyncResult *res, GError **e))              \
	X(gtk, gpointer, gtk_uri_launcher_new, (const gchar *uri))                                                  \
	X(gtk, void, gtk_uri_launcher_launch, (gpointer l, GtkWindow *parent, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, gboolean, gtk_uri_launcher_launch_finish, (gpointer l, GAsyncResult *res, GError **e))               \
	X(gtk, GdkDisplay *, gdk_display_get_default, (void))                                                       \
	X(gtk, GListModel *, gdk_display_get_monitors, (GdkDisplay * d))                                            \
	X(gtk, gpointer, g_list_model_get_item, (GListModel * m, guint i))                                          \
	X(gtk, guint, g_list_model_get_n_items, (GListModel * m))

#define X(lib, ret, name, params) ret(*name) params;
typedef struct {
	ASUKA_LINUX_FUNCS(X)
} asuka_linux_t;
#undef X

// asuka_linux is the functions, loaded the first time (any thread): NULL
// when GLib isn't there. A function this system's libraries lack is NULL.
const asuka_linux_t *asuka_linux(void);

// G(name) is a function of asuka_linux's: it must be loaded (asuka_linux()
// isn't NULL) first.
#define G(name) (asuka_linux_fns.name)
extern asuka_linux_t asuka_linux_fns;

// asuka_linux_fail fails err with a GError's message, and frees it:
// asuka_error_failed with the domain "gio" and its code (what for none).
void asuka_linux_fail(asuka_error *err, GError *e, const char *what);

// asuka_linux_main runs fn(ctx) on the main thread through GLib's main
// context (the GTK host's main loop): for what must run where GLib's
// signals come.
void asuka_linux_main(void (*fn)(void *ctx), void *ctx);

// asuka_linux_cancelled reports whether a GError says the user cancelled
// (G_IO_ERROR_CANCELLED, a GTK dialog cancelled or dismissed); it
// doesn't free it.
bool asuka_linux_cancelled(const GError *e);

#define g_signal_connect(o, sig, cb, data) G(g_signal_connect_data)((o), (sig), (GCallback)(cb), (data), NULL, 0)

#ifdef __cplusplus
}
#endif

#endif
