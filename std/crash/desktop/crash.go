// asuka/crash on the desktop (macOS, Windows, Linux): sdk/crash, whose
// record the hosts start as the app does (crashrun): the host's Go
// panics and fatal errors, signals in the core, macOS's MetricKit, and
// what the app records.
package crash

import (
	"context"
	"errors"
	"time"

	sdkcrash "github.com/egoist/asuka/sdk/crash"
	"github.com/egoist/asuka/native"
)

type reporter struct{}

func newReporter() (Reporter, error) { return reporter{}, nil }

func (reporter) Breadcrumb(message string) error {
	sdkcrash.Breadcrumb(message)
	return nil
}

func (reporter) Set(key, value string) error {
	sdkcrash.Set(key, value)
	return nil
}

// Error is an error an app recorded, its message Asuka's.
type Error struct{ msg string }

func (e Error) Error() string { return e.msg }

func (reporter) Record(message string) error {
	sdkcrash.Record(Error{message})
	return nil
}

func (reporter) Delete(id string) error { return sdkcrash.Delete(id) }

// Follow reads the reports now, then again a moment later each time, for
// what comes (a recorded error, MetricKit's diagnostics): each once.
func (reporter) Follow(ctx context.Context, found func(Report)) error {
	seen := map[string]bool{}
	for {
		rs, err := sdkcrash.Reports()
		if err != nil {
			if errors.Is(ctx.Err(), context.Canceled) {
				return ctx.Err()
			}
			// Not kept (no record started): nothing comes.
			<-ctx.Done()
			return ctx.Err()
		}
		for _, r := range rs {
			if seen[r.ID] {
				continue
			}
			seen[r.ID] = true
			x := report(r)
			native.Post(func() { found(x) })
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(2 * time.Second):
		}
	}
}

func report(r sdkcrash.Report) Report {
	out := Report{Id: r.ID, Kind: string(r.Kind), Fatal: r.Fatal, Message: r.Message, Text: r.Text}
	if !r.Time.IsZero() {
		out.Time = int(r.Time.UnixMilli())
	}
	for _, t := range r.Threads {
		th := Thread{Name: t.Name, Crashed: t.Crashed}
		for _, f := range t.Frames {
			th.Frames = append(th.Frames, Frame{Function: f.Function, File: f.File, Line: f.Line, Binary: f.Binary,
				BinaryId: f.BinaryID, Address: int(f.Address), Offset: int(f.Offset)})
		}
		out.Threads = append(out.Threads, th)
	}
	c := r.Context
	out.Context = Context{AppVersion: c.AppVersion, Build: c.Build, Model: c.Model, System: c.System,
		SystemVersion: c.SystemVersion, Arch: c.Arch, Process: c.Process, Pid: c.PID, Uptime: c.Uptime,
		Foreground: c.Foreground, Values: c.Values}
	if !c.Started.IsZero() {
		out.Context.Started = int(c.Started.UnixMilli())
	}
	for _, b := range c.Breadcrumbs {
		out.Context.Breadcrumbs = append(out.Context.Breadcrumbs, Crumb{Time: int(b.Time.UnixMilli()), Message: b.Message})
	}
	return out
}
