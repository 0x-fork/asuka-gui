// asuka/record on Windows (the microphone, waveIn, encoded by Media
// Foundation, record_windows.go) and Linux (GStreamer, record_linux.go);
// macOS runs the package's Swift. Video is asuka/camera's.
package record

import "os"

// newFile is a new file in the app's caches' asuka-audio.
func newFile(dir, ext string) (string, error) {
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
