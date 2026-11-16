package apphost

// The standard library's desktop code (std/<name>/desktop): every app's
// host carries it, as every app may import the package (a package's
// native objects are found by its class, which these register). macOS
// runs a package's Swift instead where it has some; these answer it's
// unsupported there.
import (
	_ "github.com/egoist/asuka/std/appearance/desktop"
	_ "github.com/egoist/asuka/std/biometric/desktop"
	_ "github.com/egoist/asuka/std/bluetooth/desktop"
	_ "github.com/egoist/asuka/std/crash/desktop"
	_ "github.com/egoist/asuka/std/device/desktop"
	_ "github.com/egoist/asuka/std/dialog/desktop"
	_ "github.com/egoist/asuka/std/document/desktop"
	_ "github.com/egoist/asuka/std/host/desktop"
	_ "github.com/egoist/asuka/std/http/desktop"
	_ "github.com/egoist/asuka/std/js/desktop"
	_ "github.com/egoist/asuka/std/location/desktop"
	_ "github.com/egoist/asuka/std/network/desktop"
	_ "github.com/egoist/asuka/std/power/desktop"
	_ "github.com/egoist/asuka/std/preview/desktop"
	_ "github.com/egoist/asuka/std/print/desktop"
	_ "github.com/egoist/asuka/std/purchase/desktop"
	_ "github.com/egoist/asuka/std/screen/desktop"
	_ "github.com/egoist/asuka/std/secure/desktop"
	_ "github.com/egoist/asuka/std/sensors/desktop"
	_ "github.com/egoist/asuka/std/shell/desktop"
	_ "github.com/egoist/asuka/std/sqlite/desktop"
	_ "github.com/egoist/asuka/std/store/desktop"
	_ "github.com/egoist/asuka/std/tracking/desktop"
	_ "github.com/egoist/asuka/std/watch/desktop"
	_ "github.com/egoist/asuka/std/webauth/desktop"
)
