// asuka/speech on Windows (SAPI, speech_windows.go) and Linux (Speech
// Dispatcher, speech_linux.go); macOS runs the package's Swift.
package speech

import "context"

// waitSpoken waits for an utterance's end, or the task's: then stop
// stops speaking (on the main thread, as stop is).
func waitSpoken(ctx context.Context, done <-chan error, stop func()) error {
	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		stop()
		return ctx.Err()
	}
}
