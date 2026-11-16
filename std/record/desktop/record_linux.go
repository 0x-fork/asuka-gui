//go:build linux && !android

package record

import (
	"context"
	"errors"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: GStreamer, a pipeline from the default source (PipeWire's,
// PulseAudio's) through an AAC encoder into an MPEG-4 file, its level
// element's messages the meter. A desktop app records out of sight
// anyway: background and title change nothing.

// recordSource is the pipeline's source (tests use a test tone).
var recordSource = "autoaudiosrc"

type recording struct {
	pipe, bus Ptr
	handlers  []uint64
	path      string
	reply     func([]byte, error)
	level     byte
	paused    bool
	kept      time.Duration // before the last start
	since     time.Time     // recording since (zero: paused)
	timer     uint32
	stopping  bool
}

var rec *recording

// aacEncoders are GStreamer's AAC encoders, best first.
var aacEncoders = []string{"fdkaacenc", "avenc_aac", "voaacenc", "faac"}

func (r *recording) elapsed() time.Duration {
	if r.since.IsZero() {
		return r.kept
	}
	return r.kept + time.Since(r.since)
}

func startRecording(music bool, maxMS uint32, path string, reply func([]byte, error)) error {
	if err := LoadGst(); err != nil {
		return errors.New("record: " + err.Error())
	}
	enc := ""
	for _, e := range aacEncoders {
		if GstHas(e) {
			enc = e
			break
		}
	}
	if enc == "" {
		return errors.New("record: no AAC encoder (install GStreamer's libav or bad plugins)")
	}
	if path == "" {
		var err error
		if path, err = newFile(filepath.Join(native.CacheDir(), "asuka-audio"), ".m4a"); err != nil {
			return err
		}
	} else if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	caps, bitrate := "audio/x-raw,rate=22050,channels=1", 64000
	if music {
		caps, bitrate = "audio/x-raw,rate=44100,channels=2", 128000
	}
	mux := "mp4mux"
	desc := recordSource + " ! queue ! audioconvert ! audioresample ! " + caps +
		" ! level name=meter interval=100000000 post-messages=true ! " + enc + " name=enc ! aacparse ! " + mux + " ! filesink name=sink"
	var gerr Ptr
	pipe := Gst.ParseLaunch(desc, &gerr)
	if pipe == 0 {
		return errors.New("record: " + GError(gerr).Error())
	}
	if gerr != 0 { // a warning: an element it could make without
		ErrorFree(gerr)
	}
	ObjectRefSink(pipe)
	if e := Gst.BinGetByName(pipe, "enc"); e != 0 {
		if enc == "faac" || enc == "fdkaacenc" || enc == "voaacenc" || enc == "avenc_aac" {
			SetIntProperty(e, "bitrate", int32(bitrate))
		}
		Gst.ObjectUnref(e)
	}
	if s := Gst.BinGetByName(pipe, "sink"); s != 0 {
		SetStringProperty(s, "location", path)
		Gst.ObjectUnref(s)
	}
	r := &recording{pipe: pipe, path: path, reply: reply, since: time.Now()}
	r.bus = Gst.ElementGetBus(pipe)
	Gst.BusAddSignalWatch(r.bus)
	r.handlers = append(r.handlers,
		Connect(r.bus, "message::element", func(msg uintptr) {
			if name, text := GstMessageStructure(msg); name == "level" {
				r.level = levelOf(text)
			}
		}),
		Connect(r.bus, "message::eos", func(uintptr) { r.finish(nil) }),
		Connect(r.bus, "message::error", func(msg uintptr) { r.finish(GstMessageError(msg)) }),
	)
	if Gst.ElementSetState(pipe, GstStatePlaying) == GstStateChangeFailure {
		r.release()
		return errors.New("record: the microphone can't record")
	}
	rec = r
	if maxMS > 0 {
		r.timer = TimeoutAdd(maxMS, func() bool {
			r.timer = 0
			r.stop()
			return false
		})
	}
	return nil
}

// levelRMS finds a level message's RMS values (dB, a channel each): an
// array, "rms=(GValueArray)< -20.1, -20.3 >" ("(double){ ... }" in some
// versions).
var levelRMS = regexp.MustCompile(`rms=\([^)]*\)\s*[<{]([^>}]*)[>}]`)

// levelOf is a level message's loudest channel's RMS as iOS's meter
// reads: 0 below -60 dB, else its amplitude, 0 to 255.
func levelOf(text string) byte {
	m := levelRMS.FindStringSubmatch(text)
	if m == nil {
		return 0
	}
	db := math.Inf(-1)
	for _, f := range strings.Split(m[1], ",") {
		if v, err := strconv.ParseFloat(strings.TrimSpace(f), 64); err == nil && v > db {
			db = v
		}
	}
	if db <= -60 || math.IsNaN(db) {
		return 0
	}
	return byte(min(max(math.Pow(10, db/20), 0), 1) * 255)
}

// stop ends the recording: the encoder and the muxer finish the file
// (the end of the stream), then start's reply gets it.
func (r *recording) stop() {
	if r.stopping {
		return
	}
	r.stopping = true
	if r.timer != 0 {
		SourceRemove(r.timer)
		r.timer = 0
	}
	if !r.since.IsZero() {
		r.kept += time.Since(r.since)
		r.since = time.Time{}
	}
	if r.paused {
		// A paused pipeline doesn't carry the end through: playing, it
		// does at once.
		Gst.ElementSetState(r.pipe, GstStatePlaying)
	}
	Gst.SendEvent(r.pipe, Gst.EventNewEOS())
}

func (r *recording) finish(err error) {
	if rec == r {
		rec = nil
	}
	reply := r.reply
	r.reply = nil
	if r.timer != 0 {
		SourceRemove(r.timer)
		r.timer = 0
	}
	r.release()
	if reply == nil {
		return
	}
	if err != nil {
		reply(nil, errors.New("the recording failed: "+err.Error()))
		return
	}
	reply([]byte(r.path), nil)
}

func (r *recording) release() {
	for _, hd := range r.handlers {
		Disconnect(r.bus, hd)
	}
	r.handlers = nil
	if r.bus != 0 {
		Gst.BusRemoveSignalWat(r.bus)
		Gst.ObjectUnref(r.bus)
		r.bus = 0
	}
	if r.pipe != 0 {
		Gst.ElementSetState(r.pipe, GstStateNull)
		Gst.ObjectUnref(r.pipe)
		r.pipe = 0
	}
}

type gstRecorder struct{}

func newRecorder() (Recorder, error) { return gstRecorder{}, nil }

func (gstRecorder) Record(ctx context.Context, quality Quality, maxDuration time.Duration, path string, background bool, title string) (string, error) {
	type answer struct {
		path string
		err  error
	}
	got := make(chan answer, 1)
	native.Post(func() {
		if rec != nil {
			got <- answer{"", ErrBusy}
			return
		}
		if err := startRecording(quality == QualityMusic, uint32(maxDuration/time.Millisecond), path, func(b []byte, err error) { got <- answer{string(b), err} }); err != nil {
			got <- answer{"", err}
		}
	})
	select {
	case a := <-got:
		return a.path, a.err
	case <-ctx.Done():
		// The task that waits went: the recording ends.
		native.Post(func() {
			if r := rec; r != nil {
				r.stop()
			}
		})
		return "", ctx.Err()
	}
}

func (gstRecorder) Stop() error {
	if r := rec; r != nil {
		r.stop()
	}
	return nil
}

func (gstRecorder) Pause() error {
	if r := rec; r != nil && !r.paused && !r.stopping {
		r.paused = true
		r.kept += time.Since(r.since)
		r.since = time.Time{}
		r.level = 0
		Gst.ElementSetState(r.pipe, GstStatePaused)
	}
	return nil
}

func (gstRecorder) Resume() error {
	if r := rec; r != nil && r.paused && !r.stopping {
		r.paused = false
		r.since = time.Now()
		Gst.ElementSetState(r.pipe, GstStatePlaying)
	}
	return nil
}

func (gstRecorder) Status() (Status, error) {
	r := rec
	if r == nil {
		return Status{}, nil
	}
	s := Status{Recording: !r.paused && !r.stopping, Elapsed: r.elapsed()}
	if !r.paused {
		s.Level = float64(r.level) / 255
	}
	return s, nil
}

