//go:build windows

package camera

import (
	"context"
	stdimage "image"
	"sync"

	"errors"
	"github.com/egoist/asuka/internal/barcode"
	"github.com/egoist/asuka/internal/l10n"
	"os"
	"path/filepath"
	"strings"
	"time"

	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// asuka/camera on Windows, with Media Foundation's capture engine. The
// "asuka.camera" view is a window of its own the engine's preview sink
// draws the camera into, filling it (cropped to keep its aspect); its
// photo sink takes Capture's photos (JPEG), its record sink
// asuka/sdk/camera/record's videos in video mode (H.264 and AAC in MPEG-4).
// With scan on, the preview comes to the view as frames (the preview
// sink's sample callback), which it draws itself and reads codes in, four
// times a second, with package barcode (scan.go): each code reported as
// it comes into view, again once it's been out of view for a second. "take" has no system camera screen to show: it
// shows a dialog of the host's own over the window in use, a live preview
// with Take Photo and Cancel (Enter and Escape), as on a Mac. Photos and
// videos go to the app's caches (asuka-camera). Settings' camera privacy
// switch refused: "not allowed".
//
// The camera is chosen by its facing as Windows names it ("Front",
// "Rear"/"Back" in its name); else the first one.

// cameraCurrent is the view on screen Capture and record use: the last
// made of those still there.
var cameraCurrent *cameraView

// cameraDevices and cameraDenied ask Windows; a test replaces them.
var (
	cameraDevices = VideoDevices
	cameraDenied  = func() bool { return CapabilityDenied("webcam") }
)

// cameraFile is a new file in the caches' asuka-camera.
func cameraFile(ext string) (string, error) {
	dir := filepath.Join(native.CacheDir(), "asuka-camera")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	f, err := os.CreateTemp(dir, "*"+ext)
	if err != nil {
		return "", err
	}
	f.Close()
	return f.Name(), nil
}

// The view.

type cameraView struct {
	emit    func(string, map[string]any)
	hwnd    uintptr
	engine  *CaptureEngine
	running string // the facing and mode the engine was made for

	facing      string
	video, scan bool
	configuring bool
	previewing  bool
	sheet       bool // the take dialog's: never the current one
	destroyed   bool
	kinds       scanKinds
	seen        codeSeen
	frames      *cameraFrames // the preview's frames, scanning
	failed      bool
	capturing   func([]byte, error)
	photoPath   string
	recording   func([]byte, error)
	recPath     string
	recStop     *time.Timer
}

func newCameraView(parent uintptr, emit func(string, map[string]any), sheet bool) *cameraView {
	v := &cameraView{emit: emit, facing: "back", sheet: sheet}
	if sheet {
		v.facing = "front"
	}
	v.hwnd = NewProcWindow(0, "", WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN, 0, 0, 0, 0, parent, func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool) {
		switch m {
		case WM_PAINT:
			if v.frames != nil {
				var ps PaintStruct
				dc := BeginPaint(hwnd, &ps)
				v.frames.paint(dc, ClientRect(hwnd))
				EndPaint(hwnd, &ps)
				return 0, true
			}
		case WM_SIZE:
			if v.frames != nil {
				Invalidate(hwnd)
			} else if v.engine != nil && v.previewing {
				r := ClientRect(hwnd)
				v.engine.FitPreview(r.W(), r.H())
			}
		case WM_ERASEBKGND:
			if v.frames != nil && v.frames.bmp != 0 {
				return 1, true // the frame covers it
			}
			r := ClientRect(hwnd)
			b := CreateSolidBrush(0)
			FillRect(wp, &r, b)
			DeleteObject(b)
			return 1, true
		}
		return 0, false
	})
	if !sheet {
		cameraCurrent = v
	}
	v.configureLater()
	return v
}

func (v *cameraView) Handle() uintptr { return v.hwnd }

func (v *cameraView) fail(msg string) {
	if !v.destroyed && v.emit != nil {
		v.emit("error", map[string]any{"message": msg})
	}
}

// Set implements native.View: facing, video, scan, codes.
func (v *cameraView) set(name string, value any) {
	switch name {
	case "facing":
		s, _ := value.(string)
		v.facing = map[bool]string{true: "front", false: "back"}[s == "front"]
	case "video":
		v.video, _ = value.(bool)
	case "scan":
		v.scan, _ = value.(bool)
	case "codes":
		s, _ := value.(string)
		v.kinds = parseScanKinds(s)
		if v.frames != nil {
			v.frames.setKinds(v.kinds)
		}
		return
	default:
		return
	}
	v.configureLater()
}

// configureLater configures once for the props set together.
func (v *cameraView) configureLater() {
	if v.configuring {
		return
	}
	v.configuring = true
	native.Post(func() {
		v.configuring = false
		v.configure()
	})
}

// cameraFor is the device of a facing: named so, else the first.
func cameraFor(ds []VideoDevice, facing string) (VideoDevice, bool) {
	if len(ds) == 0 {
		return VideoDevice{}, false
	}
	words := []string{"rear", "back", "world"}
	if facing == "front" {
		words = []string{"front", "user", "integrated"}
	}
	for _, d := range ds {
		n := strings.ToLower(d.Name)
		for _, w := range words {
			if strings.Contains(n, w) {
				return d, true
			}
		}
	}
	return ds[0], true
}

func (v *cameraView) configure() {
	if v.destroyed {
		return
	}
	want := v.facing + map[bool]string{true: "/video", false: "/photo"}[v.video] + map[bool]string{true: "/scan", false: ""}[v.scan]
	if v.engine != nil && v.running == want {
		return
	}
	v.closeEngine()
	if cameraDenied() {
		v.failed = true
		v.fail("not allowed")
		return
	}
	ds, err := cameraDevices()
	defer ReleaseDevices(ds)
	d, ok := cameraFor(ds, v.facing)
	if err != nil || !ok {
		v.failed = true
		v.fail("no camera")
		return
	}
	audio := v.video && HasMicrophone() && !CapabilityDenied("microphone")
	var e *CaptureEngine
	e, err = NewCaptureEngine(d, audio, func(kind GUID, status uintptr) {
		native.Post(func() {
			if v.engine == e && !v.destroyed {
				v.event(kind, status)
			}
		})
	})
	if err != nil {
		v.failed = true
		v.fail("no camera")
		return
	}
	v.engine, v.running, v.failed = e, want, false
}

// event is the engine's event, on the main thread.
func (v *cameraView) event(kind GUID, status uintptr) {
	ok := !Failed(status)
	switch kind {
	case CaptureInitialized:
		if !ok {
			v.failed = true
			if uint32(status) == 0x80070005 { // E_ACCESSDENIED
				v.fail("not allowed")
			} else {
				v.fail("no camera")
			}
			return
		}
		var err error
		if v.scan {
			v.frames = newCameraFrames(v)
			err = v.engine.PreviewFrames(v.frames.frame)
		} else {
			err = v.engine.StartPreview(v.hwnd)
		}
		if err != nil {
			v.failed = true
			v.fail(err.Error())
		}
	case CapturePreviewStarted:
		v.previewing = ok
		if ok && v.frames == nil {
			r := ClientRect(v.hwnd)
			v.engine.FitPreview(r.W(), r.H())
		}
	case CapturePreviewStopped:
		v.previewing = false
	case CapturePhotoTaken:
		reply, path := v.capturing, v.photoPath
		v.capturing, v.photoPath = nil, ""
		if reply == nil {
			return
		}
		if !ok {
			os.Remove(path)
			reply(nil, errors.New("no photo"))
			return
		}
		reply([]byte(path), nil)
	case CaptureRecordStarted:
		if !ok {
			v.recordDone(errors.New("couldn't record"))
		}
	case CaptureRecordStopped:
		if ok {
			v.recordDone(nil)
		} else {
			v.recordDone(errors.New("the recording failed"))
		}
	case CaptureError:
		if v.capturing != nil {
			c := v.capturing
			v.capturing = nil
			c(nil, errors.New("no photo"))
		}
		if v.recording != nil {
			v.recordDone(errors.New("the recording failed"))
		}
		if !v.previewing {
			v.failed = true
			v.fail("the camera stopped")
		}
	}
}

func (v *cameraView) capture(reply func([]byte, error)) {
	if v.engine == nil || !v.previewing {
		reply(nil, errors.New("the camera isn't showing"))
		return
	}
	if c := v.capturing; c != nil {
		v.capturing = nil
		c(nil, errors.New("another photo was taken"))
	}
	path, err := cameraFile(".jpg")
	if err != nil {
		reply(nil, err)
		return
	}
	if err := v.engine.TakePhoto(path); err != nil {
		os.Remove(path)
		reply(nil, err)
		return
	}
	v.capturing, v.photoPath = reply, path
}

// record records until stopRecording (or max, if not 0), with the
// microphone unless mute (or there's none, or it's not allowed).
func (v *cameraView) record(mute bool, max time.Duration, reply func([]byte, error)) {
	switch {
	case v.engine == nil || !v.previewing:
		reply(nil, errors.New("the camera isn't showing"))
		return
	case !v.video:
		reply(nil, errors.New("the camera view isn't in video mode"))
		return
	case v.recording != nil:
		reply(nil, errors.New("already recording"))
		return
	}
	path, err := cameraFile(".mp4")
	if err != nil {
		reply(nil, err)
		return
	}
	if err := v.engine.StartRecording(path, !mute); err != nil {
		os.Remove(path)
		reply(nil, err)
		return
	}
	v.recording, v.recPath = reply, path
	if max > 0 {
		var t *time.Timer
		t = time.AfterFunc(max, func() {
			native.Post(func() {
				if v.recStop == t {
					v.stopRecording()
				}
			})
		})
		v.recStop = t
	}
}

func (v *cameraView) stopRecording() {
	if v.recording != nil && v.engine != nil {
		v.engine.StopRecording()
	}
}

func (v *cameraView) recordDone(err error) {
	reply, path := v.recording, v.recPath
	v.recording, v.recPath = nil, ""
	if v.recStop != nil {
		v.recStop.Stop()
		v.recStop = nil
	}
	if reply == nil {
		return
	}
	if err != nil {
		os.Remove(path)
		reply(nil, err)
		return
	}
	reply([]byte(path), nil)
}

func (v *cameraView) closeEngine() {
	if v.engine == nil {
		return
	}
	if v.previewing {
		v.engine.StopPreview()
	}
	v.engine.Close()
	v.engine, v.previewing, v.running = nil, false, ""
	if v.frames != nil {
		v.frames.close()
		v.frames = nil
	}
}

// Destroy implements native.Destroyer: the camera stops, and what waits
// on it is told.
func (v *cameraView) Destroy() {
	if v.destroyed {
		return
	}
	if c := v.capturing; c != nil {
		v.capturing = nil
		c(nil, errors.New("the camera isn't showing"))
	}
	if v.recording != nil {
		v.engine.StopRecording()
		v.recordDone(errors.New("the camera isn't showing"))
	}
	v.destroyed = true
	if cameraCurrent == v {
		cameraCurrent = nil
	}
	v.closeEngine()
}

// Codes in the camera's frames: package barcode's, in Go (QR codes,
// Data Matrix, EAN-13 and EAN-8, UPC-A and UPC-E, Code 128, Code 39), as
// the host's.

// scanKinds is what asuka/sdk/scan asks for ("": every kind).
type scanKinds map[string]bool

func parseScanKinds(s string) scanKinds {
	k := scanKinds{}
	for _, x := range strings.Split(s, ",") {
		if x != "" {
			k[x] = true
		}
	}
	return k
}

func (k scanKinds) wants(kind string) bool { return len(k) == 0 || k[kind] }

// barcodeKinds is what to ask package barcode for; ok is false when
// none of the kinds asked for is one it reads (Aztec, PDF417...).
func (k scanKinds) barcodeKinds() (kinds []barcode.Kind, ok bool) {
	if len(k) == 0 {
		return nil, true
	}
	for _, b := range []barcode.Kind{barcode.QR, barcode.DataMatrix, barcode.EAN13, barcode.EAN8, barcode.UPCA, barcode.UPCE, barcode.Code128, barcode.Code39} {
		if k[string(b)] {
			kinds = append(kinds, b)
		}
	}
	return kinds, len(kinds) > 0
}

// scanImage reads the codes of the kinds asked for in img.
func scanImage(img stdimage.Image, k scanKinds) []barcode.Code {
	kinds, ok := k.barcodeKinds()
	if !ok {
		return nil
	}
	return barcode.Decode(img, kinds...)
}

// frameScanEvery is how often the camera view reads a frame's codes.
const frameScanEvery = 250 * time.Millisecond

// codeSeen drops repeats of a code in view: a code is reported as it
// comes into view, and again once it's been out of it for a second (as
// iOS's AsukaCodeSeen).
type codeSeen struct{ last map[string]time.Time }

func (s *codeSeen) isNew(kind, value string, now time.Time) bool {
	if s.last == nil {
		s.last = map[string]time.Time{}
	}
	key := kind + "\x00" + value
	t, ok := s.last[key]
	isNew := !ok || now.Sub(t) > time.Second
	s.last[key] = now
	if len(s.last) > 32 {
		for k, t := range s.last {
			if now.Sub(t) > time.Second {
				delete(s.last, k)
			}
		}
	}
	return isNew
}

// frameGray is a BGRX frame's luminance.
func frameGray(f Frame, dst *stdimage.Gray) *stdimage.Gray {
	if dst == nil || dst.Rect.Dx() != f.Width || dst.Rect.Dy() != f.Height {
		dst = stdimage.NewGray(stdimage.Rect(0, 0, f.Width, f.Height))
	}
	p := f.Pix
	for i, j := 0, 0; j < len(dst.Pix) && i+2 < len(p); i, j = i+4, j+1 {
		// BT.601's weights, in integers.
		dst.Pix[j] = uint8((299*uint32(p[i+2]) + 587*uint32(p[i+1]) + 114*uint32(p[i]) + 500) / 1000)
	}
	return dst
}

// cameraFrames is a scanning camera view's preview: the latest frame,
// drawn by the view, and the reading of codes in one every
// frameScanEvery, off the main thread.
type cameraFrames struct {
	v *cameraView

	mu      sync.Mutex // the fields below, which Media Foundation's threads write
	latest  []byte
	w, h    int
	posted  bool // a redraw waits on the main thread
	closed  bool
	kinds   scanKinds
	last    time.Time
	reading bool // a frame's codes are being read
	gray    *stdimage.Gray

	bmp        uintptr // the frame drawn, main thread
	bits       []byte
	bmpW, bmpH int
}

func newCameraFrames(v *cameraView) *cameraFrames {
	return &cameraFrames{v: v, kinds: v.kinds}
}

func (c *cameraFrames) setKinds(k scanKinds) {
	c.mu.Lock()
	c.kinds = k
	c.mu.Unlock()
}

// frame takes a frame, on Media Foundation's thread: kept for the view
// to draw, and read for codes if it's time.
func (c *cameraFrames) frame(f Frame) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.closed {
		return
	}
	if len(c.latest) != len(f.Pix) {
		c.latest = make([]byte, len(f.Pix))
	}
	copy(c.latest, f.Pix)
	c.w, c.h = f.Width, f.Height
	if !c.posted {
		c.posted = true
		native.Post(func() {
			c.mu.Lock()
			c.posted = false
			closed := c.closed
			c.mu.Unlock()
			if !closed {
				InvalidateRect(c.v.hwnd, nil)
			}
		})
	}
	now := time.Now()
	if c.reading || now.Sub(c.last) < frameScanEvery {
		return
	}
	c.reading, c.last = true, now
	c.gray = frameGray(f, c.gray)
	gray, kinds := c.gray, c.kinds
	go func() {
		codes := scanImage(gray, kinds)
		c.mu.Lock()
		c.reading = false
		c.mu.Unlock()
		native.Post(func() { c.found(codes, kinds, time.Now()) })
	}()
}

// found reports the codes newly in view, on the main thread.
func (c *cameraFrames) found(codes []barcode.Code, kinds scanKinds, now time.Time) {
	v := c.v
	if c.closed || v.destroyed || v.frames != c {
		return
	}
	for _, code := range codes {
		kind := string(code.Kind)
		if !kinds.wants(kind) || !v.seen.isNew(kind, code.Text, now) {
			continue
		}
		v.emit("code", map[string]any{"kind": kind, "value": code.Text})
	}
}

// paint draws the latest frame filling r, cropped to keep its aspect.
func (c *cameraFrames) paint(dc uintptr, r Rect) {
	c.mu.Lock()
	w, h := c.w, c.h
	if w > 0 && h > 0 {
		if c.bmp == 0 || c.bmpW != w || c.bmpH != h {
			if c.bmp != 0 {
				DeleteObject(c.bmp)
			}
			c.bmp, c.bits = DIB(int32(w), int32(h))
			c.bmpW, c.bmpH = w, h
		}
		if c.bmp != 0 {
			copy(c.bits, c.latest)
		}
	}
	c.mu.Unlock()
	if c.bmp == 0 || r.W() <= 0 || r.H() <= 0 {
		FillColor(dc, r, 0)
		return
	}
	// The part of the frame of the view's aspect, centered.
	sx, sy, sw, sh := 0, 0, w, h
	if fa, va := float64(r.W())/float64(r.H()), float64(w)/float64(h); va > fa {
		sw = int(float64(h) * fa)
		sx = (w - sw) / 2
	} else {
		sh = int(float64(w) / fa)
		sy = (h - sh) / 2
	}
	mem := CreateCompatibleDC(dc)
	old := SelectObject(mem, c.bmp)
	StretchBlt(dc, 0, 0, r.W(), r.H(), mem, int32(sx), int32(sy), int32(sw), int32(sh))
	SelectObject(mem, old)
	DeleteDC(mem)
}

func (c *cameraFrames) close() {
	c.mu.Lock()
	c.closed = true
	c.mu.Unlock()
	if c.bmp != 0 {
		DeleteObject(c.bmp)
		c.bmp = 0
	}
}

// The take dialog: a window of the package's over the app's, a live
// preview with Take Photo and Cancel (Enter and Escape), as on a Mac.

type cameraSheet struct {
	reply  func([]byte, error)
	hwnd   uintptr
	owner  uintptr
	view   *cameraView
	button uintptr // Take Photo
	cancel uintptr
	font   uintptr
}

var cameraTaking *cameraSheet

// cameraTake shows the dialog and answers with the photo's path, or ""
// if cancelled.
func cameraTake(reply func([]byte, error)) {
	cameraTaking.finish(nil, nil) // one at a time
	if cameraDenied() {
		reply(nil, ErrNotAllowed)
		return
	}
	ds, err := cameraDevices()
	ReleaseDevices(ds)
	if err != nil || len(ds) == 0 {
		reply(nil, ErrNoCamera)
		return
	}
	s := &cameraSheet{reply: reply, owner: native.Window()}
	scale := float64(DPI(s.owner)) / 96
	if scale <= 0 {
		scale = 1
	}
	px := func(v float64) int32 { return int32(v*scale + 0.5) }
	w, h := px(640), px(532)
	x, y := int32(0), int32(0)
	if s.owner != 0 {
		if r := WindowRect(s.owner); r.W() > 0 {
			x, y = r.Left+(r.W()-w)/2, r.Top+(r.H()-h)/2
		}
	}
	title := takeTitle()
	s.hwnd = NewProcWindow(0, title, WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN, x, y, w, h, s.owner, func(hwnd uintptr, m uint32, wp, lp uintptr) (uintptr, bool) {
		switch m {
		case WM_COMMAND:
			switch lp {
			case s.button:
				s.take()
			case s.cancel:
				s.finish(nil, nil)
			}
			return 0, true
		case WM_KEYDOWN:
			switch wp {
			case 0x0D: // VK_RETURN
				s.take()
				return 0, true
			case 0x1B: // VK_ESCAPE
				s.finish(nil, nil)
				return 0, true
			}
		case WM_SIZE:
			s.layout(px)
		case WM_CLOSE:
			s.finish(nil, nil)
			return 0, true
		}
		return 0, false
	})
	s.view = newCameraView(s.hwnd, func(name string, values map[string]any) {
		if name == "error" {
			msg, _ := values["message"].(string)
			s.finish(nil, errors.New(msg))
		}
	}, true)
	lf := LogFont{Height: -px(14), Weight: 400, Quality: 5} // CLEARTYPE_QUALITY
	copy(lf.FaceName[:], UTF16z("Segoe UI"))
	s.font = CreateFont(&lf)
	button := func(title string, id uintptr) uintptr {
		b := CreateWindow(0, "BUTTON", title, WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, s.hwnd, id)
		SendMessage(b, WM_SETFONT, s.font, 1)
		return b
	}
	s.button = button(title, 1)
	s.cancel = button(cancelTitle(), 2)
	s.layout(px)
	cameraTaking = s
	if s.owner != 0 {
		EnableWindow(s.owner, false) // modal over it
	}
	ShowWindow(s.hwnd, SW_SHOW)
	SetFocus(s.hwnd)
}

// layout places the preview above the bar, the buttons in it, from the
// trailing edge.
func (s *cameraSheet) layout(px func(float64) int32) {
	if s.view == nil {
		return
	}
	r := ClientRect(s.hwnd)
	bar, bw, bh, gap := px(52), px(120), px(32), px(12)
	SetWindowPos(s.view.hwnd, 0, 0, 0, r.W(), r.H()-bar, SWP_NOZORDER|SWP_NOACTIVATE)
	y := r.H() - bar + (bar-bh)/2
	x := r.W() - gap*2 - bw
	SetWindowPos(s.button, 0, x, y, bw, bh, SWP_NOZORDER|SWP_NOACTIVATE)
	SetWindowPos(s.cancel, 0, x-gap-bw, y, bw, bh, SWP_NOZORDER|SWP_NOACTIVATE)
}

func (s *cameraSheet) take() {
	if s == nil || cameraTaking != s || !IsWindowEnabled(s.button) {
		return
	}
	EnableWindow(s.button, false)
	s.view.capture(func(b []byte, err error) {
		if err != nil && err.Error() == "the camera isn't showing" {
			EnableWindow(s.button, true) // not ready yet: try again
			return
		}
		s.finish(b, err)
	})
}

// finish closes the dialog and answers (nil, nil: cancelled).
func (s *cameraSheet) finish(b []byte, err error) {
	if s == nil || cameraTaking != s {
		return
	}
	cameraTaking = nil
	s.view.Destroy()
	if s.owner != 0 {
		EnableWindow(s.owner, true)
	}
	DestroyWindow(s.hwnd)
	DeleteObject(s.font)
	if b == nil && err == nil {
		b = []byte{}
	}
	s.reply(b, err)
}

// takeTitle and cancelTitle are the dialog's words, in the user's
// language.
func takeTitle() string   { return l10n.TakePhoto(locale()) }
func cancelTitle() string { return l10n.Cancel(locale()) }

// locale is the user's language, as Windows says it.
func locale() string {
	if ls := PreferredLanguages(); len(ls) > 0 {
		return ls[0]
	}
	return "en"
}

// The package's: the View, and the camera's calls.

type mfCameraView struct {
	*cameraView
}

func newView(events ViewEvents) ViewNative {
	parent := events.Context.Parent()
	v := &mfCameraView{newCameraView(parent, func(name string, values map[string]any) {
		switch name {
		case "error":
			m, _ := values["message"].(string)
			events.Error(m)
		case "code":
			k, _ := values["kind"].(string)
			val, _ := values["value"].(string)
			events.Code(Code{Kind: k, Value: val})
		}
	}, false)}
	return v
}

func (v *mfCameraView) SetFacing(f Facing) {
	v.set("facing", map[bool]string{true: "front", false: "back"}[f == FacingFront])
}
func (v *mfCameraView) SetMode(m Mode)      { v.set("video", m == ModeVideo) }
func (v *mfCameraView) SetScan(on bool)     { v.set("scan", on) }
func (v *mfCameraView) SetCodes(c []string) { v.set("codes", strings.Join(c, ",")) }

type mfCamera struct{}

func newCamera() (Camera, error) { return mfCamera{}, nil }

// onMain runs start on the main thread and waits for its answer, or ctx.
func onMain(ctx context.Context, start func(reply func([]byte, error))) (string, error) {
	type answer struct {
		b   []byte
		err error
	}
	got := make(chan answer, 1)
	native.Post(func() { start(func(b []byte, err error) { got <- answer{b, err} }) })
	select {
	case a := <-got:
		return string(a.b), a.err
	case <-ctx.Done():
		return "", ctx.Err()
	}
}

func (mfCamera) Take(ctx context.Context) (string, error) { return onMain(ctx, cameraTake) }

func (mfCamera) Capture(ctx context.Context) (string, error) {
	return onMain(ctx, func(reply func([]byte, error)) {
		if v := cameraCurrent; v != nil {
			v.capture(reply)
			return
		}
		reply(nil, ErrNoView)
	})
}

func (mfCamera) Record(ctx context.Context, mute bool, maxDuration time.Duration) (string, error) {
	return onMain(ctx, func(reply func([]byte, error)) {
		if v := cameraCurrent; v != nil {
			v.record(mute, maxDuration, reply)
			return
		}
		reply(nil, ErrNoView)
	})
}

func (mfCamera) StopRecording() error {
	if v := cameraCurrent; v != nil {
		v.stopRecording()
	}
	return nil
}
