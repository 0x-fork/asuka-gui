// asuka/nowplaying on Windows (the SystemMediaTransportControls,
// nowplaying_windows.go) and Linux (an MPRIS player on the session bus,
// nowplaying_linux.go); macOS runs the package's Swift. Its methods come
// on the main thread, where the commands are heard and sent on.
package nowplaying

import (
	"context"
	"io"
	"net/http"
	"os"
	"strings"

	"github.com/egoist/asuka/native"
)

// watchers hear the commands, main thread only.
var watchers = map[*func(Command, float64)]bool{}

// send gives each watcher a command, if it's enabled (main thread).
func send(info *Info, c Command, v float64) {
	if info == nil || info.Commands&(1<<int(c)) == 0 {
		return
	}
	if (c == CommandSkipForward || c == CommandSkipBackward) && v == 0 {
		v = info.Skip
	}
	for w := range watchers {
		(*w)(c, v)
	}
}

// watch adds a watcher until ctx is done.
func watch(ctx context.Context, command func(Command, float64)) error {
	w := &command
	native.Post(func() { watchers[w] = true })
	<-ctx.Done()
	native.Post(func() { delete(watchers, w) })
	return ctx.Err()
}

// readArtwork reads an image's bytes: an http(s) URL's, a file's, an
// asset's. Not on the main thread.
func readArtwork(src string) ([]byte, error) {
	if strings.HasPrefix(src, "http://") || strings.HasPrefix(src, "https://") {
		resp, err := http.Get(src)
		if err != nil {
			return nil, err
		}
		defer resp.Body.Close()
		return io.ReadAll(resp.Body)
	}
	return os.ReadFile(native.Asset(src))
}
