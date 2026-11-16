package apphost

// The standard library's desktop code (std/<name>/desktop): every app's
// host carries it, as every app may import the package (a package's
// native objects are found by its class, which these register). macOS
// runs a package's Swift instead where it has some; these answer it's
// unsupported there.
import (
	_ "github.com/egoist/asuka/std/http/desktop"
	_ "github.com/egoist/asuka/std/network/desktop"
	_ "github.com/egoist/asuka/std/sqlite/desktop"
	_ "github.com/egoist/asuka/std/store/desktop"
)
