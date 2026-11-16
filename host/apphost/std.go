package apphost

// The standard library's desktop code (std/<name>/desktop): every app's
// host carries it, as every app may import the package (a package's
// native objects are found by its class, which these register). macOS
// runs a package's Swift instead where it has some; these answer it's
// unsupported there.
import (
	_ "github.com/egoist/asuka/std/audio/desktop"
	_ "github.com/egoist/asuka/std/camera/desktop"
	_ "github.com/egoist/asuka/std/http/desktop"
	_ "github.com/egoist/asuka/std/imageedit/desktop"
	_ "github.com/egoist/asuka/std/lottie/desktop"
	_ "github.com/egoist/asuka/std/maps/desktop"
	_ "github.com/egoist/asuka/std/network/desktop"
	_ "github.com/egoist/asuka/std/nowplaying/desktop"
	_ "github.com/egoist/asuka/std/record/desktop"
	_ "github.com/egoist/asuka/std/scan/desktop"
	_ "github.com/egoist/asuka/std/speech/desktop"
	_ "github.com/egoist/asuka/std/store/desktop"
	_ "github.com/egoist/asuka/std/transcribe/desktop"
	_ "github.com/egoist/asuka/std/vision/desktop"
)
