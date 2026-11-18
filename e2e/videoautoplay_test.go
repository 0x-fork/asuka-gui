//go:build e2e

package e2e

import "testing"

// A Video with autoPlay plays when it appears: the source is a prop before
// autoPlay, so the host has loaded it when it's told to play. The clip is
// a second long, and the app logs its end.
func TestVideoAutoPlayIOS(t *testing.T) {
	s := iosApp(t, "e2e/testdata/video-autoplay", "dev.asuka.videoautoplay")
	waitFor(t, s, []string{"Video autoplay"})
	waitFor(t, s, []string{"log: ended"})
}

func TestVideoAutoPlayAndroid(t *testing.T) {
	s := androidApp(t, "e2e/testdata/video-autoplay", "dev.asuka.videoautoplay")
	waitFor(t, s, []string{"Video autoplay"})
	waitFor(t, s, []string{"log: ended"})
}
