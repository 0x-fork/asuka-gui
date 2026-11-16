package apphost

// The standard library's desktop code (std/<name>/desktop): every app's
// host carries it, as every app may import the package (a package's
// native objects are found by its class, which these register). macOS
// runs a package's Swift instead where it has some; these answer it's
// unsupported there.
import (
	_ "github.com/egoist/asuka/std/bluetooth/desktop"
	_ "github.com/egoist/asuka/std/markdown/desktop"
	_ "github.com/egoist/asuka/std/scan/desktop"
	_ "github.com/egoist/asuka/std/shader/desktop"
	_ "github.com/egoist/asuka/std/svg/desktop"
	_ "github.com/egoist/asuka/std/vision/desktop"
)
