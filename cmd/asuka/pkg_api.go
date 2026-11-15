package main

// checkPackageAPI compares the package's pub API with the last
// published version's, and refuses a breaking change without a new
// major version. It needs the compiler's view of a package's
// declarations; until a package has a published version to compare
// with, there's nothing to check.
func checkPackageAPI(dir string, pkgJSON map[string]any) error {
	return checkAPI(dir, pkgJSON)
}

// checkAPI is set by the compiler's integration (api.go), which knows a
// package's pub declarations.
var checkAPI = func(dir string, pkgJSON map[string]any) error { return nil }
