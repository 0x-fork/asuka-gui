//go:build windows

package speech

import (
	"context"
	"fmt"
	"html"
	"math"
	"strings"

	"github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: SAPI 5's voice, made on the main thread (its apartment), its
// events (an utterance's stream ending) signaled on an event a goroutine
// waits for, then read on the main thread. The pitch is SAPI's XML's;
// the rate and volume the voice's.

type sapi struct {
	voice  *windows.Voice
	spoken map[uint32]chan error // by stream, main thread only
}

var voice sapi

func newSynthesizer() (Synthesizer, error) { return &voice, nil }

// open makes the voice the first time. Main thread.
func (s *sapi) open() error {
	if s.voice != nil {
		return nil
	}
	v, ev, err := windows.NewVoiceEvent()
	if err != nil {
		return fmt.Errorf("speech: %w", err)
	}
	s.voice, s.spoken = v, map[uint32]chan error{}
	go func() {
		for {
			windows.WaitEvent(ev, 0xFFFFFFFF)
			native.Post(s.events)
		}
	}()
	return nil
}

// events answers the utterances whose streams ended. Main thread.
func (s *sapi) events() {
	for _, stream := range s.voice.Ended() {
		if c := s.spoken[stream]; c != nil {
			delete(s.spoken, stream)
			c <- nil
		}
	}
}

func (s *sapi) Speak(ctx context.Context, text string, o Options) error {
	done := make(chan error, 1)
	var err error
	native.OnMain(func() {
		if err = s.open(); err != nil {
			return
		}
		v := s.voice
		id := o.Voice
		if id == "" && o.Language != "" {
			id = voiceFor(o.Language)
		}
		if id == "" || !v.SetVoice(id) {
			v.SetVoice("") // the default
		}
		rate := o.Rate
		if rate <= 0 {
			rate = 1
		}
		// SAPI's rate is -10 to 10, 10 three times as fast.
		v.SetRate(int(math.Round(10 * math.Log(rate) / math.Log(3))))
		v.SetVolume(int(math.Round(min(max(o.Volume, 0), 1) * 100)))
		pitch := 0
		if o.Pitch > 0 {
			pitch = min(max(int(math.Round(10*math.Log2(o.Pitch))), -10), 10)
		}
		var stream uint32
		stream, err = v.Speak(fmt.Sprintf(`<pitch absmiddle="%d">%s</pitch>`, pitch, html.EscapeString(text)), true, false)
		if err == nil {
			s.spoken[stream] = done
		}
	})
	if err != nil {
		return err
	}
	return waitSpoken(ctx, done, func() { native.Post(s.stop) })
}

// stop stops speaking: each utterance waiting ends stopped. Main thread.
func (s *sapi) stop() {
	if s.voice == nil {
		return
	}
	s.voice.Stop()
	cs := s.spoken
	s.spoken = map[uint32]chan error{}
	for _, c := range cs {
		c <- ErrStopped
	}
}

func (s *sapi) Stop() error { s.stop(); return nil }

func (s *sapi) Pause() error {
	if s.voice != nil {
		s.voice.Pause()
	}
	return nil
}

func (s *sapi) Resume() error {
	if s.voice != nil {
		s.voice.Resume()
	}
	return nil
}

func (s *sapi) Speaking() (bool, error) { return s.voice != nil && len(s.spoken) > 0, nil }

func (s *sapi) Voices(ctx context.Context) ([]Voice, error) {
	vs := []Voice{}
	for _, v := range windows.Voices() {
		vs = append(vs, Voice{Id: v.ID, Name: v.Name, Language: v.Language})
	}
	return vs, nil
}

// voiceFor is the ID of an installed voice of a language (a BCP 47 tag):
// of the language and region, else of the language.
func voiceFor(lang string) string {
	lang = strings.ToLower(lang)
	base, _, _ := strings.Cut(lang, "-")
	match := ""
	for _, v := range windows.Voices() {
		l := strings.ToLower(v.Language)
		if l == lang {
			return v.ID
		}
		if b, _, _ := strings.Cut(l, "-"); b == base && match == "" {
			match = v.ID
		}
	}
	return match
}
