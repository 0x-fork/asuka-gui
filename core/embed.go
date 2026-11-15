package core

import "embed"

// Sources are the core's C sources, embedded in the asuka CLI so an
// installed CLI builds apps without the repository: core/include,
// core/src, core/host (the fake host, for tests) and core/runner.
//
//go:embed include src host runner
var Sources embed.FS
