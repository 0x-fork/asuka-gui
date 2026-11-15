package boot

import (
	"os"
	"time"
)

// WaitFor waits until a file exists at path, a minute at most: the
// desktop hosts' automation step waitfor:<path> (ASUKA_SCRIPT), with which
// a test that drives the app says when the script goes on (after it
// reloaded the app, say).
func WaitFor(path string) {
	for deadline := time.Now().Add(time.Minute); time.Now().Before(deadline); time.Sleep(20 * time.Millisecond) {
		if _, err := os.Stat(path); err == nil {
			return
		}
	}
}
