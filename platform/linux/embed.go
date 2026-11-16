// Package linux embeds the Linux host's C (platform/linux: GTK 4 and
// libadwaita, declared in gtk.h and syms.def, resolved as it starts) for
// the asuka CLI, which compiles it with the core, the shared desktop C
// (platform/desktop) and an app into the app's executable.
package linux

import "embed"

// Sources holds the host's C files and headers.
//
//go:embed src
var Sources embed.FS
