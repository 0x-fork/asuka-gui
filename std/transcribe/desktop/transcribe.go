// asuka/transcribe on Windows and Linux: no recognizer of files on the
// device is part of either that apps can count on (Windows' speech
// recognition is the microphone's, Linux has none), so check says so and
// file fails with ErrNotAvailable, rather than sending the sound to a
// server. macOS runs the package's Swift.
package transcribe

import (
	"context"
	"os"
	"strings"
)

type none struct{}

func newRecognizer() (Recognizer, error) { return none{}, nil }

func (none) Check(ctx context.Context, language string) (Availability, error) {
	if language == "" {
		v, _, _ := strings.Cut(os.Getenv("LANG"), ".")
		language = strings.ReplaceAll(v, "_", "-")
	}
	return Availability{OnDevice: false, Language: language}, nil
}

func (none) File(ctx context.Context, path, language string) (Result, error) {
	return Result{}, ErrNotAvailable
}
