// std/sqlite on Windows and Linux: where a database's relative name is,
// the app's data directory (the host's). SQLite itself is std/sqlite's C
// (its [c]), linked into the app, which the core calls.
#include "sqlite.asuka.h"

#include <stdlib.h>

static char *data_dir(sqlite_Place *self, asuka_error *err) { return asuka_strdup(asuka_desktop_data_dir()); }

static const sqlite_Place_vtable vt = {.dataDir = data_dir};

sqlite_Place *sqlite_make_Place(asuka_error *err) {
	sqlite_Place *p = malloc(sizeof *p);
	p->vt = &vt;
	return p;
}
