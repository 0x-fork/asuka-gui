package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/egoist/asuka/internal/devserver"
)

// devLoop is asuka dev's side of an app's players (a host built for
// development, running the C core without an app: the simulator's, an
// emulator's, this computer's, a page): it compiles the app, serves its
// module to the players that connect (internal/devserver), compiles it
// again on each save and sends the new one, which each player loads
// keeping its state and views. It prints the players' logs, and how each
// reload went.
type devLoop struct {
	app *asukaApp
	srv *devserver.Server
	out io.Writer

	mu      sync.Mutex
	module  []byte // the last module that compiled
	loaded  map[*devserver.Player]bool
	modules []func(module []byte) // more players (the web's page)
	failed  []func(msg string)    // told what didn't compile
	fixed   []func()              // told when it compiles again
	broken  bool
	// reloads receives each reload a player reports (tests).
	reloads chan devReload
}

// devReload is a player's report of a module it loaded.
type devReload struct {
	first  bool
	kept   uint32
	micros uint32
}

// newDevLoop listens for players on addr (":0": a free port) and
// compiles the app once. A module that doesn't compile is printed and
// waited on: the players get the first that does.
func newDevLoop(app *asukaApp, addr string) (*devLoop, error) {
	d := &devLoop{app: app, out: os.Stdout, loaded: map[*devserver.Player]bool{}, reloads: make(chan devReload, 16)}
	srv, err := devserver.Listen(addr, devserver.Events{
		Hello: func(p *devserver.Player, b []byte) {
			var h struct{ Platform, Device string }
			json.Unmarshal(b, &h)
			who := h.Platform
			if h.Device != "" {
				who += " (" + h.Device + ")"
			}
			fmt.Fprintf(d.out, "asuka: %s connected\n", who)
		},
		Log: func(p *devserver.Player, stderr bool, text []byte) {
			w := os.Stdout
			if stderr {
				w = os.Stderr
			}
			w.Write(text)
		},
		Reloaded: func(p *devserver.Player, kept, micros uint32) { d.report(p, false, kept, micros) },
		Error: func(p *devserver.Player, text []byte) {
			fmt.Fprintf(os.Stderr, "asuka: the player: %s\n", strings.TrimSpace(string(text)))
		},
		Gone: func(p *devserver.Player, err error) {
			d.mu.Lock()
			delete(d.loaded, p)
			d.mu.Unlock()
		},
	})
	if err != nil {
		return nil, err
	}
	d.srv = srv
	d.compile()
	return d, nil
}

// report prints a player's report: the first load, or a reload with the
// state it kept. A player of the dev server's says which by its
// connection; another (a page) says first itself.
func (d *devLoop) report(p *devserver.Player, first bool, kept, micros uint32) {
	d.mu.Lock()
	if p != nil {
		first = !d.loaded[p]
		d.loaded[p] = true
	}
	d.mu.Unlock()
	ms := float64(micros) / 1000
	if first {
		fmt.Fprintf(d.out, "asuka: running (loaded in %s)\n", msText(ms))
	} else {
		fmt.Fprintf(d.out, "asuka: reloaded in %s, %d %s kept\n", msText(ms), kept, plural(int(kept), "state", "states"))
	}
	select {
	case d.reloads <- devReload{first: first, kept: kept, micros: micros}:
	default:
	}
}

func msText(ms float64) string {
	if ms < 10 {
		return fmt.Sprintf("%.1f ms", ms)
	}
	return fmt.Sprintf("%.0f ms", ms)
}

// port is where players connect.
func (d *devLoop) port() int { return d.srv.Port() }

// close disconnects every player.
func (d *devLoop) close() { d.srv.Close() }

// compile compiles the app and sends the module to the players,
// reporting whether it compiled. What didn't is printed, and the
// players keep running what they have.
func (d *devLoop) compile() bool {
	start := time.Now()
	m, err := d.app.compile()
	if err != nil {
		fmt.Fprintln(os.Stderr, "asuka:", err)
		d.mu.Lock()
		d.broken = true
		fs := d.failed
		d.mu.Unlock()
		for _, f := range fs {
			f(err.Error())
		}
		return false
	}
	b := m.Encode()
	d.mu.Lock()
	d.module = b
	wasBroken := d.broken
	d.broken = false
	fs, fixed := d.modules, d.fixed
	d.mu.Unlock()
	if wasBroken {
		for _, f := range fixed {
			f()
		}
	}
	d.srv.Send(b)
	for _, f := range fs {
		f(b)
	}
	fmt.Fprintf(d.out, "asuka: compiled in %s\n", msText(float64(time.Since(start).Microseconds())/1000))
	return true
}

// current is the last module that compiled (nil if none has).
func (d *devLoop) current() []byte {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.module
}

// devPlayer is a platform's player as the loop drives it: launch builds
// it if need be and starts it (again), connected to the loop; stop ends
// it; done is closed when it quits by itself (nil: never).
type devPlayer struct {
	launch func() error
	stop   func()
	done   func() <-chan struct{}
}

// run launches the player and runs the loop until stop (or the player
// quits): a change to the sources is compiled and sent; one to the rest
// of the app (asuka.toml, assets, native code) launches the player
// again, built again. once launches it without watching.
func (d *devLoop) run(p devPlayer, stop <-chan os.Signal, once bool) error {
	defer d.close()
	if err := p.launch(); err != nil {
		return err
	}
	var done <-chan struct{}
	if p.done != nil {
		done = p.done()
	}
	if once {
		select {
		case <-done:
		case <-stop:
		}
		if p.stop != nil {
			p.stop()
		}
		return nil
	}
	fmt.Fprintln(d.out, "asuka: watching for changes (Ctrl-C to stop)")
	w := newWatcher(d.app.proj.WatchRoots()...)
	for {
		select {
		case <-stop:
			if p.stop != nil {
				p.stop()
			}
			return nil
		case <-done:
			fmt.Fprintln(d.out, "asuka: the app quit")
			return nil
		case <-w.wake:
			src, proj := w.changed()
			if proj {
				fmt.Fprintln(d.out, "asuka: the app's settings, assets or native code changed: building the player again")
				if cfg, err := loadConfig(d.app.dir); err != nil {
					fmt.Fprintln(os.Stderr, "asuka:", err)
					continue
				} else {
					d.app.cfg, d.app.bundleID = cfg, cfg.ID
				}
				d.app.refresh()
				if src {
					d.compile()
				}
				if err := p.launch(); err != nil {
					fmt.Fprintln(os.Stderr, "asuka:", err)
				}
				if p.done != nil {
					done = p.done()
				}
				continue
			}
			if src {
				d.app.refresh()
				d.compile()
			}
		}
	}
}
