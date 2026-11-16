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
typedef long gssize;
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
typedef struct {
	int x, y, width, height;
} GdkRectangle;

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

// The desktop portal's bus name and object.
#define ASUKA_PORTAL "org.freedesktop.portal.Desktop"
#define ASUKA_PORTAL_PATH "/org/freedesktop/portal/desktop"

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
	X(glib, GHashTable *, g_hash_table_new_full, (guint(*hash)(gconstpointer), gboolean(*eq)(gconstpointer, gconstpointer), GDestroyNotify kf, GDestroyNotify vf)) \
	X(glib, gboolean, g_hash_table_insert, (GHashTable * t, gpointer k, gpointer v))                            \
	X(glib, void, g_hash_table_unref, (GHashTable * t))                                                         \
	X(glib, guint, g_str_hash, (gconstpointer s))                                                               \
	X(glib, gboolean, g_str_equal, (gconstpointer a, gconstpointer b))                                                                     \
	X(glib, const gchar *, g_get_user_name, (void))                                                             \
	X(glib, const gchar *, g_get_real_name, (void))                                                             \
	X(glib, const gchar *, g_get_host_name, (void))                                                             \
	X(gobject, gpointer, g_object_ref, (gpointer o))                                                            \
	X(gobject, gpointer, g_object_ref_sink, (gpointer o))                                                       \
	X(gobject, gpointer, g_object_new, (GType t, const gchar *first, ...))                                      \
	X(glib, const gchar *, g_dgettext, (const gchar *domain, const gchar *msgid))                               \
	X(gtk, GtkWidget *, gtk_button_new_with_mnemonic, (const char *label))                                      \
	X(gtk, void, gtk_widget_set_vexpand, (GtkWidget * w, gboolean e))                                           \
	X(adw, GtkWidget *, adw_dialog_new, (void))                                                                 \
	X(adw, void, adw_dialog_set_content_width, (GtkWidget * d, int w))                                          \
	X(adw, void, adw_dialog_set_content_height, (GtkWidget * d, int h))                                         \
	X(adw, void, adw_dialog_set_child, (GtkWidget * d, GtkWidget *c))                                           \
	X(adw, void, adw_dialog_present, (GtkWidget * d, GtkWidget *parent))                                        \
	X(adw, gboolean, adw_dialog_force_close, (GtkWidget * d))                                                   \
	X(adw, GtkWidget *, adw_toolbar_view_new, (void))                                                           \
	X(adw, void, adw_toolbar_view_add_top_bar, (GtkWidget * v, GtkWidget *bar))                                 \
	X(adw, void, adw_toolbar_view_set_content, (GtkWidget * v, GtkWidget *c))                                  \
	X(adw, GtkWidget *, adw_header_bar_new, (void))                                                             \
	X(adw, void, adw_header_bar_set_show_start_title_buttons, (GtkWidget * b, gboolean s))                      \
	X(adw, void, adw_header_bar_set_show_end_title_buttons, (GtkWidget * b, gboolean s))                        \
	X(adw, void, adw_header_bar_pack_start, (GtkWidget * b, GtkWidget *c))                                      \
	X(adw, void, adw_header_bar_set_title_widget, (GtkWidget * b, GtkWidget *c))                                \
	X(adw, GtkWidget *, adw_window_title_new, (const char *title, const char *subtitle))                        \
	X(adw, void, adw_window_title_set_title, (GtkWidget * t, const char *s))                                    \
	X(adw, void, adw_window_title_set_subtitle, (GtkWidget * t, const char *s))                                                            \
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
	X(adw, gpointer, adw_style_manager_get_default, (void))                                                     \
	X(adw, gboolean, adw_style_manager_get_high_contrast, (gpointer m))                                         \
	X(adw, gboolean, adw_style_manager_get_dark, (gpointer m))                                                  \
	X(gio, void, g_file_trash_async, (GFile * f, int priority, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gio, gboolean, g_file_trash_finish, (GFile * f, GAsyncResult *res, GError **e))                          \
	X(gtk, void, gdk_display_beep, (GdkDisplay * d))                                                            \
	X(gtk, gpointer, gtk_file_dialog_new, (void))                                                               \
	X(gtk, void, gtk_file_dialog_set_title, (gpointer d, const char *t))                                        \
	X(gtk, void, gtk_file_dialog_set_modal, (gpointer d, gboolean m))                                           \
	X(gtk, void, gtk_file_dialog_set_filters, (gpointer d, GListModel *f))                                      \
	X(gtk, void, gtk_file_dialog_set_default_filter, (gpointer d, gpointer f))                                  \
	X(gtk, void, gtk_file_dialog_set_initial_name, (gpointer d, const char *n))                                 \
	X(gtk, void, gtk_file_dialog_set_initial_folder, (gpointer d, GFile *f))                                    \
	X(gtk, void, gtk_file_dialog_open, (gpointer d, GtkWindow *p, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, GFile *, gtk_file_dialog_open_finish, (gpointer d, GAsyncResult *r, GError **e))                     \
	X(gtk, void, gtk_file_dialog_open_multiple, (gpointer d, GtkWindow *p, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, GListModel *, gtk_file_dialog_open_multiple_finish, (gpointer d, GAsyncResult *r, GError **e))       \
	X(gtk, void, gtk_file_dialog_save, (gpointer d, GtkWindow *p, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, GFile *, gtk_file_dialog_save_finish, (gpointer d, GAsyncResult *r, GError **e))                     \
	X(gtk, void, gtk_file_dialog_select_folder, (gpointer d, GtkWindow *p, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, GFile *, gtk_file_dialog_select_folder_finish, (gpointer d, GAsyncResult *r, GError **e))            \
	X(gtk, void, gtk_file_dialog_set_accept_label, (gpointer d, const char *l))                                 \
	X(gtk, void, gtk_file_dialog_select_multiple_folders, (gpointer d, GtkWindow *p, GCancellable *c, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, GListModel *, gtk_file_dialog_select_multiple_folders_finish, (gpointer d, GAsyncResult *r, GError **e)) \
	X(gtk, void, gtk_file_filter_add_suffix, (gpointer f, const char *s))                                       \
	X(gtk, gpointer, gtk_file_filter_new, (void))                                                               \
	X(gtk, void, gtk_file_filter_set_name, (gpointer f, const char *n))                                         \
	X(gtk, void, gtk_file_filter_add_mime_type, (gpointer f, const char *m))                                    \
	X(gtk, void, gtk_file_filter_add_pattern, (gpointer f, const char *p))                                      \
	X(gtk, GType, gtk_file_filter_get_type, (void))                                                             \
	X(gio, gpointer, g_list_store_new, (GType t))                                                               \
	X(gio, void, g_list_store_append, (gpointer s, gpointer item))                                              \
	X(gtk, GdkClipboard *, gdk_display_get_clipboard, (GdkDisplay * d))                                         \
	X(gtk, void, gdk_clipboard_read_text_async, (GdkClipboard * c, GCancellable *x, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, char *, gdk_clipboard_read_text_finish, (GdkClipboard * c, GAsyncResult *r, GError **e))            \
	X(gtk, void, gdk_clipboard_read_texture_async, (GdkClipboard * c, GCancellable *x, GAsyncReadyCallback cb, gpointer data)) \
	X(gtk, GdkTexture *, gdk_clipboard_read_texture_finish, (GdkClipboard * c, GAsyncResult *r, GError **e))   \
	X(gtk, void, gdk_clipboard_set_texture, (GdkClipboard * c, GdkTexture *t))                                  \
	X(gtk, void, gdk_clipboard_set_text, (GdkClipboard * c, const char *t))                                     \
	X(gtk, gpointer, gdk_clipboard_get_formats, (GdkClipboard * c))                                             \
	X(gtk, gboolean, gdk_content_formats_contain_mime_type, (gpointer f, const char *m))                        \
	X(gtk, gboolean, gdk_content_formats_contain_gtype, (gpointer f, GType t))                                  \
	X(gtk, GType, gdk_texture_get_type, (void))                                                                 \
	X(gtk, GType, gdk_file_list_get_type, (void))                                                               \
	X(gio, GType, g_file_get_type, (void))                                                                      \
	X(gtk, GdkTexture *, gdk_texture_new_from_filename, (const char *path, GError **e))                         \
	X(gtk, GBytes *, gdk_texture_save_to_png_bytes, (GdkTexture * t))                                           \
	X(gtk, int, gdk_texture_get_width, (GdkTexture * t))                                                        \
	X(gtk, int, gdk_texture_get_height, (GdkTexture * t))                                                       \
	X(gio, gpointer, g_application_get_default, (void))                                                         \
	X(gtk, guint, gtk_application_inhibit, (gpointer app, GtkWindow *w, int flags, const char *reason))         \
	X(gtk, void, gtk_application_uninhibit, (gpointer app, guint cookie))                                       \
	X(gtk, void, gtk_init, (void))                                                                              \
	X(gtk, GdkDisplay *, gdk_display_get_default, (void))                                                       \
	X(gtk, void, gdk_monitor_get_geometry, (GdkMonitor * m, GdkRectangle *r))                                   \
	X(gtk, const char *, gdk_monitor_get_connector, (GdkMonitor * m))                                           \
	X(gtk, const char *, gdk_monitor_get_description, (GdkMonitor * m))                                         \
	X(gtk, const char *, gdk_monitor_get_manufacturer, (GdkMonitor * m))                                        \
	X(gtk, const char *, gdk_monitor_get_model, (GdkMonitor * m))                                               \
	X(gtk, double, gdk_monitor_get_scale, (GdkMonitor * m))                                                     \
	X(gtk, int, gdk_monitor_get_scale_factor, (GdkMonitor * m))                                                 \
	X(gtk, int, gdk_monitor_get_refresh_rate, (GdkMonitor * m))                                                 \
	X(gtk, GdkMonitor *, gdk_x11_display_get_primary_monitor, (GdkDisplay * d))                                 \
	X(gtk, void, gdk_x11_monitor_get_workarea, (GdkMonitor * m, GdkRectangle *r))                               \
	X(gobject, GType, g_type_from_name, (const gchar *name))                                                    \
	X(gobject, gboolean, g_type_check_instance_is_a, (gpointer instance, GType t))                                                       \
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

// asuka_linux_sym is a function of GLib's, GTK's or libadwaita's (or of
// a library a package dlopened with RTLD_GLOBAL) that asuka_linux_t
// doesn't declare, by name: NULL where there's none. For what one
// package calls (its widget's own functions), declared and resolved in
// the package; what two share belongs in asuka_linux_t.
void *asuka_linux_sym(const char *name);

// asuka_linux_fail fails err with a GError's message, and frees it:
// asuka_error_failed with the domain "gio" and its code (what for none).
void asuka_linux_fail(asuka_error *err, GError *e, const char *what);

// asuka_linux_main runs fn(ctx) on the main thread through GLib's main
// context (the GTK host's main loop): for what must run where GLib's
// signals come.
void asuka_linux_main(void (*fn)(void *ctx), void *ctx);

// asuka_linux_bus is the session's or the system's bus (G_BUS_TYPE_*),
// connected once and kept; NULL with *e set when there's none.
GDBusConnection *asuka_linux_bus(int kind, GError **e);

// asuka_linux_is_a reports whether obj is of the type named (a GType a
// library registers: GdkX11Display), false when there's no such type.
bool asuka_linux_is_a(gpointer obj, const char *type);

// asuka_linux_cancelled reports whether a GError says the user cancelled
// (G_IO_ERROR_CANCELLED, a GTK dialog cancelled or dismissed); it
// doesn't free it.
bool asuka_linux_cancelled(const GError *e);

#define g_signal_connect(o, sig, cb, data) G(g_signal_connect_data)((o), (sig), (GCallback)(cb), (data), NULL, 0)

#ifdef __cplusplus
}
#endif

#endif
