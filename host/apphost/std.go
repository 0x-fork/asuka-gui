package apphost

// The standard library's desktop code (std/<name>/desktop): every app's
// host carries it, as every app may import the package (a package's
// native objects are found by its class, which these register). macOS
// runs a package's Swift instead where it has some; these answer it's
// unsupported there.
import (
	_ "github.com/egoist/asuka/std/appearance/desktop"
	_ "github.com/egoist/asuka/std/audio/desktop"
	_ "github.com/egoist/asuka/std/bluetooth/desktop"
	_ "github.com/egoist/asuka/std/camera/desktop"
	_ "github.com/egoist/asuka/std/device/desktop"
	_ "github.com/egoist/asuka/std/dialog/desktop"
	_ "github.com/egoist/asuka/std/host/desktop"
	_ "github.com/egoist/asuka/std/imageedit/desktop"
	_ "github.com/egoist/asuka/std/js/desktop"
	_ "github.com/egoist/asuka/std/location/desktop"
	_ "github.com/egoist/asuka/std/maps/desktop"
	_ "github.com/egoist/asuka/std/markdown/desktop"
	_ "github.com/egoist/asuka/std/network/desktop"
	_ "github.com/egoist/asuka/std/nowplaying/desktop"
	_ "github.com/egoist/asuka/std/power/desktop"
	_ "github.com/egoist/asuka/std/print/desktop"
	_ "github.com/egoist/asuka/std/purchase/desktop"
	_ "github.com/egoist/asuka/std/record/desktop"
	_ "github.com/egoist/asuka/std/scan/desktop"
	_ "github.com/egoist/asuka/std/screen/desktop"
	_ "github.com/egoist/asuka/std/secure/desktop"
	_ "github.com/egoist/asuka/std/sensors/desktop"
	_ "github.com/egoist/asuka/std/shader/desktop"
	_ "github.com/egoist/asuka/std/shell/desktop"
	_ "github.com/egoist/asuka/std/speech/desktop"
	_ "github.com/egoist/asuka/std/svg/desktop"
	_ "github.com/egoist/asuka/std/vision/desktop"
	_ "github.com/egoist/asuka/std/watch/desktop"
	_ "github.com/egoist/asuka/std/webauth/desktop"
)
