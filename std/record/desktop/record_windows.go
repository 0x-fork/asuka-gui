//go:build windows

package record

import (
	"context"
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"sync/atomic"
	"time"

	"github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the default microphone (waveIn) encoded to AAC by Media
// Foundation as it records, in an MPEG-4 file (.m4a), on a thread of its
// own in COM's multithreaded apartment. Windows's AAC encoder takes 44.1
// kHz: voice is mono at 96 kbit/s, music stereo at 128. A Windows app
// keeps recording out of sight: background and title change nothing.
// Settings' microphone privacy switch refused: not allowed.

type recording struct {
	cmds      chan byte // 'p' pause, 'r' resume, 's' stop
	level     atomic.Uint32
	recording atomic.Bool
	ms        atomic.Uint32
}

var now atomic.Pointer[recording]

type waveRecorder struct{}

func newRecorder() (Recorder, error) { return waveRecorder{}, nil }

func (waveRecorder) Record(ctx context.Context, quality Quality, maxDuration time.Duration, path string, background bool, title string) (string, error) {
	if windows.CapabilityDenied("microphone") {
		return "", ErrNotAllowed
	}
	r := &recording{cmds: make(chan byte, 8)}
	if !now.CompareAndSwap(nil, r) {
		return "", ErrBusy
	}
	defer now.CompareAndSwap(r, nil)
	if path == "" {
		var err error
		if path, err = newFile(filepath.Join(native.CacheDir(), "asuka-audio"), ".m4a"); err != nil {
			return "", err
		}
	}
	channels, bps := 1, 12000
	if quality == QualityMusic {
		channels, bps = 2, 16000
	}
	// The task that waits went: the recording ends.
	stop := context.AfterFunc(ctx, func() { r.send('s') })
	defer stop()
	var err error
	windows.WithCOM(func() { err = r.run(path, channels, bps, uint32(maxDuration/time.Millisecond)) })
	if err != nil {
		os.Remove(path)
		return "", err
	}
	return path, nil
}

func (r *recording) send(c byte) {
	select {
	case r.cmds <- c:
	default:
	}
}

func (waveRecorder) Stop() error {
	if r := now.Load(); r != nil {
		r.send('s')
	}
	return nil
}

func (waveRecorder) Pause() error {
	if r := now.Load(); r != nil {
		r.send('p')
	}
	return nil
}

func (waveRecorder) Resume() error {
	if r := now.Load(); r != nil {
		r.send('r')
	}
	return nil
}

func (waveRecorder) Status() (Status, error) {
	r := now.Load()
	if r == nil {
		return Status{}, nil
	}
	s := Status{Recording: r.recording.Load(), Elapsed: time.Duration(r.ms.Load()) * time.Millisecond}
	if s.Recording {
		s.Level = float64(r.level.Load()) / 255
	}
	return s, nil
}

// run records until stopped (or max milliseconds).
func (r *recording) run(path string, channels, bps int, maxMs uint32) error {
	const rate = 44100
	src, err := windows.OpenWaveIn(channels, rate)
	if err != nil {
		return err
	}
	w, err := windows.NewAudioWriter(path, channels, rate, bps, false)
	if err != nil {
		src.Close(func([]byte) {})
		return err
	}
	frameBytes := 2 * channels
	limit := int64(maxMs) * rate / 1000 * int64(frameBytes) // bytes; 0: none
	var kept int64
	var werr error
	write := func(pcm []byte) {
		if werr != nil || (limit > 0 && kept >= limit) {
			return
		}
		if limit > 0 && kept+int64(len(pcm)) > limit {
			pcm = pcm[:limit-kept]
		}
		werr = w.Write(pcm)
		kept += int64(len(pcm))
		r.ms.Store(uint32(kept / int64(frameBytes) * 1000 / rate))
		r.level.Store(uint32(pcmLevel(pcm)))
	}
	if err := src.Start(); err != nil {
		src.Close(func([]byte) {})
		w.Close()
		return err
	}
	r.recording.Store(true)
loop:
	for werr == nil && (limit == 0 || kept < limit) {
		select {
		case c := <-r.cmds:
			switch c {
			case 's':
				break loop
			case 'p':
				if r.recording.Load() {
					src.Stop()
					r.recording.Store(false)
				}
			case 'r':
				if !r.recording.Load() {
					src.Start()
					r.recording.Store(true)
				}
			}
		default:
		}
		src.Wait(50)
		src.Read(write)
	}
	r.recording.Store(false)
	src.Close(write)
	if err := w.Close(); werr == nil {
		werr = err
	}
	return werr
}

// pcmLevel is 16-bit PCM's level, 0 to 255: its RMS amplitude, silence
// below -60 dB (as AVAudioRecorder's average power reads).
func pcmLevel(pcm []byte) byte {
	n := len(pcm) / 2
	if n == 0 {
		return 0
	}
	var sum float64
	for i := 0; i < n; i++ {
		v := float64(int16(binary.LittleEndian.Uint16(pcm[2*i:]))) / 32768
		sum += v * v
	}
	rms := math.Sqrt(sum / float64(n))
	if rms < 0.001 {
		return 0
	}
	return byte(min(rms, 1) * 255)
}
