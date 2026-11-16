//go:build windows

package audio

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: each sound an MCI mpegvideo device (DirectShow's playback),
// opened on its source's file (an asset, a path, a URL downloaded to the
// app's caches first, off the main thread). A play asks to be told when
// it ends (MM_MCINOTIFY, to a message window of the package's): looping
// plays again, once lets the sound go.

type mciSound struct {
	alias        string
	open         bool
	loop, once   bool
	ended        bool
	playing      bool
	pending      []func() // what was asked before its file was open
	rate, volume float64
	serial       int // its open, which a later open or close outdates
}

var (
	aliases int
	notify  uintptr                // the message window, made the first time
	sounds  = map[*mciSound]bool{} // the open ones, main thread only
	once    = map[*mciSound]bool{} // the ones playing once, kept until they end
)

func newSound() (Sound, error) {
	if notify == 0 {
		notify = windows.NewMessageWindow(func(msg uint32, wparam, lparam uintptr) {
			if msg == 0x03B9 && wparam == windows.MCI_NOTIFY_SUCCESSFUL { // MM_MCINOTIFY
				notified()
			}
		})
	}
	aliases++
	return &mciSound{alias: fmt.Sprintf("asukasound%d", aliases), rate: 1, volume: 1}, nil
}

func (p *mciSound) do(f func()) {
	if p.open {
		f()
	} else {
		p.pending = append(p.pending, f)
	}
}

func (p *mciSound) mci(cmd string, wantNotify bool) string {
	var hwnd uintptr
	if wantNotify {
		hwnd = notify
		cmd += " notify"
	}
	out, _ := windows.MCI(strings.ReplaceAll(cmd, "@", p.alias), hwnd)
	return out
}

func (p *mciSound) Open(source string) error {
	p.Close()
	p.serial++
	serial := p.serial
	go func() {
		path, err := native.LocalFile(source)
		native.Post(func() {
			if p.serial != serial {
				return // closed, or opened again, meanwhile
			}
			if err == nil {
				_, err = windows.MCI(`open "`+path+`" type mpegvideo alias `+p.alias, 0)
			}
			if err != nil {
				p.pending = nil
				fmt.Printf("asuka: audio: %s: %v\n", source, err)
				return
			}
			p.open = true
			sounds[p] = true
			p.mci("set @ time format milliseconds", false)
			for _, f := range p.pending {
				f()
			}
			p.pending = nil
		})
	}()
	return nil
}

func (p *mciSound) Play() error {
	p.do(func() {
		if p.ended {
			p.ended = false
			p.mci("seek @ to start", false)
		}
		p.playing = true
		p.mci("play @", true)
	})
	return nil
}

func (p *mciSound) PlayOnce() error {
	p.once = true
	once[p] = true
	return p.Play()
}

func (p *mciSound) Pause() error {
	p.do(func() {
		p.mci("pause @", false)
		p.playing = false
	})
	return nil
}

func (p *mciSound) Seek(seconds float64) error {
	ms := int(seconds * 1000)
	p.do(func() {
		p.ended = false
		was := p.playing
		p.mci("seek @ to "+strconv.Itoa(max(ms, 0)), false)
		if was {
			p.mci("play @", true)
		}
	})
	return nil
}

func (p *mciSound) SetVolume(v float64) error {
	p.volume = min(max(v, 0), 1)
	p.do(func() { p.mci("setaudio @ volume to "+strconv.Itoa(int(p.volume*1000)), false) })
	return nil
}

func (p *mciSound) SetRate(r float64) error {
	p.rate = r
	p.do(func() { p.mci("set @ speed "+strconv.Itoa(int(max(p.rate, 0.1)*1000)), false) })
	return nil
}

func (p *mciSound) SetLoop(on bool) error { p.loop = on; return nil }

func (p *mciSound) Status() (Status, error) {
	if !p.open {
		return Status{}, nil
	}
	pos, _ := strconv.ParseFloat(p.mci("status @ position", false), 64)
	dur, _ := strconv.ParseFloat(p.mci("status @ length", false), 64)
	return Status{Position: pos / 1000, Duration: dur / 1000, Playing: p.mci("status @ mode", false) == "playing", Ended: p.ended}, nil
}

func (p *mciSound) Close() error {
	p.serial++
	p.pending = nil
	if p.open {
		p.mci("close @", false)
		p.open = false
	}
	delete(sounds, p)
	p.playing, p.ended = false, false
	return nil
}

// Released is the core letting it go: it stops, unless it's playing once.
func (p *mciSound) Released() {
	if !once[p] {
		p.Close()
	}
}

// notified follows an MCI notification: a play that ended (looping:
// played again; once: let go).
func notified() {
	for p := range sounds {
		if !p.playing || p.mci("status @ mode", false) == "playing" {
			continue
		}
		pos, _ := strconv.ParseFloat(p.mci("status @ position", false), 64)
		dur, _ := strconv.ParseFloat(p.mci("status @ length", false), 64)
		if dur > 0 && pos < dur-50 {
			continue // paused or stopped, not at its end
		}
		switch {
		case p.loop:
			p.mci("seek @ to start", false)
			p.mci("play @", true)
		case p.once:
			p.Close()
			delete(once, p)
		default:
			p.playing, p.ended = false, true
		}
	}
}
