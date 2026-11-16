package core

import "embed"

// Sources are the core's C sources, embedded in the asuka CLI so an
// installed CLI builds apps without the repository: core/include,
// core/src, core/host (the fake host, for tests), core/runner and
// core/third_party (what core/src includes of libraries it vendors).
//
//go:embed include src host runner third_party
var Sources embed.FS
