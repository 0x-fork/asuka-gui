//go:build windows

package nowplaying

import (
	"context"

	"github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the SystemMediaTransportControls of the app's window
// (ISystemMediaTransportControlsInterop::GetForWindow, as a desktop app
// gets them; a hidden window of the package's when the app has none, a
// background app's), which the volume flyout (Windows 10), quick
// settings' media controls (Windows 11), the lock screen and the
// keyboard's media keys show and drive. Its buttons are Play, Pause,
// Stop, Next, Previous, FastForward and Rewind (the skips), and the
// timeline's position requests (seeks); the play/pause key sends Play or
// Pause as the status is. The artwork is read off the main thread and
// handed over as a stream.

type smtc struct {
	controls *windows.MediaControls
	failed   error // why there are none
	info     *Info
	artSrc   string
	art      []byte // the artwork's bytes, once read
}

var center smtc

func newCenter() (Center, error) { return &center, nil }

// open gets the controls (and hooks them up) on first use. Main thread.
func (s *smtc) open() (*windows.MediaControls, error) {
	if s.controls != nil || s.failed != nil {
		return s.controls, s.failed
	}
	hwnd := native.Window()
	if hwnd == 0 {
		hwnd = windows.NewHiddenWindow("Now Playing", func(uint32, uintptr, uintptr) {})
	}
	c, err := windows.GetMediaControls(hwnd, func(b windows.MediaButton) {
		native.Post(func() { s.button(b) })
	}, func(at float64) {
		native.Post(func() { send(s.info, CommandSeek, at) })
	})
	if err != nil {
		s.failed = err
		return nil, err
	}
	s.controls = c
	return c, nil
}

func (s *smtc) button(b windows.MediaButton) {
	c := map[windows.MediaButton]Command{
		windows.MediaButtonPlay: CommandPlay, windows.MediaButtonPause: CommandPause, windows.MediaButtonStop: CommandStop,
		windows.MediaButtonNext: CommandNext, windows.MediaButtonPrevious: CommandPrevious,
		windows.MediaButtonFastForward: CommandSkipForward, windows.MediaButtonRewind: CommandSkipBackward,
	}[b]
	if c != CommandUnknown {
		send(s.info, c, 0)
	}
}

func (s *smtc) Set(info Info) error {
	c, err := s.open()
	if err != nil {
		return err
	}
	s.info = &info
	s.artwork(info.Artwork)
	c.Set(s.shown())
	return nil
}

func (s *smtc) Clear() error {
	s.info, s.artSrc, s.art = nil, "", nil
	if s.controls != nil {
		s.controls.Clear()
	}
	return nil
}

func (s *smtc) Watch(ctx context.Context, command func(Command, float64)) error {
	return watch(ctx, command)
}

func has(i *Info, c Command) bool { return i.Commands&(1<<int(c)) != 0 }

// shown is what the controls show of info.
func (s *smtc) shown() windows.MediaInfo {
	i := s.info
	m := windows.MediaInfo{Title: i.Title, Artist: i.Artist, Album: i.Album, Thumbnail: s.art,
		Duration: i.Duration, Position: i.Position, Rate: i.Rate,
		Play: has(i, CommandPlay), Pause: has(i, CommandPause), Stop: has(i, CommandStop),
		Next: has(i, CommandNext), Previous: has(i, CommandPrevious),
		FastForward: has(i, CommandSkipForward), Rewind: has(i, CommandSkipBackward),
		Seek: has(i, CommandSeek) && i.Duration > 0}
	switch i.State {
	case StatePlaying:
		m.Status = windows.MediaPlaying
	case StateStopped:
		m.Status = windows.MediaStopped
	default:
		m.Status = windows.MediaPaused
	}
	return m
}

// artwork reads the artwork's bytes off the main thread, and shows them
// once they're in.
func (s *smtc) artwork(src string) {
	if src == s.artSrc {
		return
	}
	s.artSrc, s.art = src, nil
	if src == "" {
		return
	}
	go func() {
		data, err := readArtwork(src)
		if err != nil {
			return
		}
		native.Post(func() {
			if s.artSrc != src || s.info == nil || s.controls == nil {
				return
			}
			s.art = data
			s.controls.Set(s.shown())
		})
	}()
}
