// Command feed serves an update feed's directory on 127.0.0.1 inside the
// Windows VM, for updater_test.go (the update package takes HTTP only to
// this computer): feed <dir> <port> <log>. Each request's path goes to
// log; it ends when <dir>/stop appears, or after ten minutes.
package main

import (
	"fmt"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"
)

func main() {
	dir, port, logPath := os.Args[1], os.Args[2], os.Args[3]
	var mu sync.Mutex
	h := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		if f, err := os.OpenFile(logPath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644); err == nil {
			fmt.Fprintln(f, r.URL.Path)
			f.Close()
		}
		mu.Unlock()
		http.FileServer(http.Dir(dir)).ServeHTTP(w, r)
	})
	go func() {
		for end := time.Now().Add(10 * time.Minute); time.Now().Before(end); time.Sleep(200 * time.Millisecond) {
			if _, err := os.Stat(filepath.Join(dir, "stop")); err == nil {
				break
			}
		}
		os.Exit(0)
	}()
	if err := http.ListenAndServe("127.0.0.1:"+port, h); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
