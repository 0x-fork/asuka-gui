// device.h: what device's Windows and Linux C share (device.c).
#ifndef DEVICE_H
#define DEVICE_H

#include "device.asuka.h"

// device_copy_picked copies the files into the app's caches, each in a
// folder of its own (so its name stays its own), as a phone's pickers
// give them; false with err set.
bool device_copy_picked(char **paths, size_t n, size_t limit, device_list_File *out, asuka_error *err);
// device_write_cache writes data into a new file of the app's caches'
// folder dir, with extension ext: its path (NULL with err set).
char *device_write_cache(const char *dir, const char *ext, const void *data, size_t len, asuka_error *err);
// device_web_url reports whether u is an http(s) URL.
bool device_web_url(const char *u);
// device_photos are the paths of picked files (taking them).
device_list_string device_photos(device_list_File *files);
// device_mime is a file name's MIME type by its extension ("" unknown).
const char *device_mime(const char *name);

#endif
