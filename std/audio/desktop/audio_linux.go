//go:build linux && !android

package audio

import (
	"errors"
	"path/filepath"
	"strings"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: GStreamer (which GTK's media plays with too), a sound a playbin
// whose pitch scaletempo keeps at any rate, its bus's messages heard on
// the main thread (GLib's loop, the host's).

type gstSound struct {
	pipe, bus   Ptr
	handlers    []uint64
	loop, once  bool
	playing     bool
	ended       bool
	rate        float64
	prerolled   bool
	pendingSeek float64 // < 0: none
	rateDue     bool    // the rate waits for the pipeline to preroll
}

// playingOnce keeps the sounds playing once until they end.
var playingOnce = map[*gstSound]bool{}

func newSound() (Sound, error) {
	if err := LoadGst(); err != nil {
		return nil, errors.New("audio: " + err.Error())
	}
	return &gstSound{rate: 1, pendingSeek: -1}, nil
}

// uri is a source as a URI: a URL, a file, an asset.
func uri(source string) string {
	if strings.Contains(source, "://") && !strings.HasPrefix(source, "file://") {
		return source
	}
	path := native.Asset(source)
	if abs, err := filepath.Abs(path); err == nil {
		path = abs
	}
	f := Sys.FileNewForPath(path)
	defer ObjectUnref(f)
	return GoStringFree(Sys.FileGetURI(f))
}

func (p *gstSound) Open(source string) error {
	p.Close()
	pipe := Gst.ElementFactoryMake("playbin", 0)
	if pipe == 0 {
		return errors.New("audio: GStreamer's playbin isn't installed (gst-plugins-base)")
	}
	ObjectRefSink(pipe)
	SetStringProperty(pipe, "uri", uri(source))
	// Sound only: a video's picture goes nowhere.
	if sink := Gst.ElementFactoryMake("fakesink", 0); sink != 0 {
		SetObjectProperty(pipe, "video-sink", sink)
	}
	// A voice keeps its pitch at any rate.
	if GstHas("scaletempo") {
		if f := Gst.ElementFactoryMake("scaletempo", 0); f != 0 {
			SetObjectProperty(pipe, "audio-filter", f)
		}
	}
	p.pipe, p.prerolled, p.pendingSeek, p.ended, p.playing = pipe, false, -1, false, false
	p.bus = Gst.ElementGetBus(pipe)
	Gst.BusAddSignalWatch(p.bus)
	p.handlers = append(p.handlers,
		Connect(p.bus, "message::async-done", func(uintptr) {
			if p.prerolled {
				return
			}
			p.prerolled = true
			switch {
			case p.pendingSeek >= 0:
				s := p.pendingSeek
				p.pendingSeek = -1
				p.seek(s)
			case p.rateDue && p.rate != 1:
				p.seek(p.position())
			}
			p.rateDue = false
		}),
		Connect(p.bus, "message::eos", func(uintptr) { p.reachedEnd() }),
		Connect(p.bus, "message::error", func(uintptr) {
			p.playing = false
			p.ended = true
		}),
	)
	Gst.ElementSetState(pipe, GstStatePaused) // preroll: the duration's known
	return nil
}

func (p *gstSound) position() float64 {
	var pos int64
	if p.pipe != 0 && Gst.QueryPosition(p.pipe, GstFormatTime, &pos) && pos > 0 {
		return float64(pos) / 1e9
	}
	return 0
}

// seek goes to a time, at the sound's rate.
func (p *gstSound) seek(sec float64) {
	if !p.prerolled {
		p.pendingSeek = sec
		return
	}
	Gst.Seek(p.pipe, p.rate, GstFormatTime, GstSeekFlagFlush|GstSeekFlagAccurate,
		GstSeekTypeSet, int64(max(sec, 0)*1e9), GstSeekTypeNone, -1)
}

// reachedEnd is its end: again (loop), let go (once), or ended.
func (p *gstSound) reachedEnd() {
	switch {
	case p.loop:
		p.seek(0)
	case p.once:
		p.Close()
		delete(playingOnce, p)
	default:
		p.ended = true
		p.playing = false
		Gst.ElementSetState(p.pipe, GstStatePaused)
	}
}

func (p *gstSound) Play() error {
	if p.pipe == 0 {
		return nil
	}
	if p.ended {
		p.ended = false
		p.seek(0)
	}
	p.playing = true
	Gst.ElementSetState(p.pipe, GstStatePlaying)
	return nil
}

func (p *gstSound) PlayOnce() error {
	p.once = true
	playingOnce[p] = true
	return p.Play()
}

func (p *gstSound) Pause() error {
	if p.pipe != 0 {
		p.playing = false
		Gst.ElementSetState(p.pipe, GstStatePaused)
	}
	return nil
}

func (p *gstSound) Seek(seconds float64) error {
	if p.pipe != 0 {
		p.ended = false
		p.seek(seconds)
	}
	return nil
}

func (p *gstSound) SetVolume(v float64) error {
	if p.pipe != 0 {
		SetDoubleProperty(p.pipe, "volume", min(max(v, 0), 1))
	}
	return nil
}

func (p *gstSound) SetRate(r float64) error {
	p.rate = min(max(r, 0.25), 4)
	if p.pipe == 0 {
		return nil
	}
	if p.prerolled {
		p.seek(p.position())
	} else {
		p.rateDue = true
	}
	return nil
}

func (p *gstSound) SetLoop(on bool) error { p.loop = on; return nil }

func (p *gstSound) Status() (Status, error) {
	if p.pipe == 0 {
		return Status{}, nil
	}
	s := Status{Position: p.position(), Playing: p.playing, Ended: p.ended}
	var d int64
	if Gst.QueryDuration(p.pipe, GstFormatTime, &d) && d > 0 {
		s.Duration = float64(d) / 1e9
	}
	if s.Ended && s.Duration > 0 {
		s.Position = s.Duration
	}
	return s, nil
}

func (p *gstSound) Close() error {
	if p.pipe == 0 {
		return nil
	}
	for _, hd := range p.handlers {
		Disconnect(p.bus, hd)
	}
	p.handlers = nil
	Gst.BusRemoveSignalWat(p.bus)
	Gst.ObjectUnref(p.bus)
	Gst.ElementSetState(p.pipe, GstStateNull)
	Gst.ObjectUnref(p.pipe)
	p.pipe, p.bus = 0, 0
	return nil
}

// Released is the core letting it go: it stops, unless it's playing once.
func (p *gstSound) Released() {
	if !playingOnce[p] {
		p.Close()
	}
}
