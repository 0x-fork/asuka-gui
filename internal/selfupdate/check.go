package selfupdate

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// The check is npm's update-notifier's: a command that finds the day's
// check due (Due) starts it in a process of its own, detached (asuka's
// hidden _update-check, which runs Check), and never waits for it: a
// goroutine would die with the many commands that end before GitHub
// answers. Its answer is kept in a file, and a command's end says what
// the file has (Notice): this run's check's answer if it's in by then,
// else the last one's; a release newer than this CLI, in one line on
// stderr, at most once a day.

// Every is how often the check runs, and how often a newer release is
// said.
const Every = 24 * time.Hour

// retry is how soon a check is started again when the last one didn't
// finish (offline, or GitHub didn't answer in Wait).
const retry = time.Hour

// Wait is how long a check may take.
var Wait = 10 * time.Second

// state is what the check keeps between runs.
type state struct {
	Checked   time.Time `json:"checked,omitzero"`   // the last check answered (started then)
	Attempted time.Time `json:"attempted,omitzero"` // one last started
	Latest    string    `json:"latest,omitempty"`   // what it found
	Shown     time.Time `json:"shown,omitzero"`     // a newer release last said
}

// Enabled reports whether a CLI of version checks for releases: not
// with ASUKA_NO_UPDATE_CHECK, not in CI (CI set), not when stderr, where
// it's said, isn't a terminal, not a CLI built from a checkout.
func Enabled(version string, getenv func(string) string, stderrIsTerminal bool) bool {
	return Released(version) && getenv("ASUKA_NO_UPDATE_CHECK") == "" && getenv("CI") == "" && stderrIsTerminal
}

// Due reports whether a check should start now, and if so marks it
// started in file: a run at the same moment, or within the hour, starts
// none of its own.
func Due(file string, now time.Time) bool {
	st := load(file)
	if now.Sub(st.Checked) < Every || now.Sub(st.Attempted) < retry {
		return false
	}
	st.Attempted = now
	return save(file, st) == nil
}

// Check asks GitHub for the latest release (in Wait at most) and keeps
// it in file, checked now.
func Check(file string, now time.Time) error {
	ctx, cancel := context.WithTimeout(context.Background(), Wait)
	defer cancel()
	latest, err := Latest(ctx)
	if err != nil {
		return err
	}
	st := load(file)
	st.Checked, st.Latest = now, latest
	return save(file, st)
}

// Notice is the line to say at a command's end, or "": a release newer
// than current, as the last check found, unless it was said within the
// day.
func Notice(file, current string, now time.Time) string {
	st := load(file)
	if !Newer(st.Latest, current) || now.Sub(st.Shown) < Every {
		return ""
	}
	st.Shown = now
	if save(file, st) != nil {
		return "" // said every run if it can't be kept: not at all
	}
	return fmt.Sprintf("asuka %s is out (this is %s): asuka upgrade installs it", st.Latest, current)
}

func load(file string) state {
	var st state
	if b, err := os.ReadFile(file); err == nil {
		json.Unmarshal(b, &st)
	}
	return st
}

// save writes the state whole (a temporary file renamed): runs at once
// each keep one or the other's.
func save(file string, st state) error {
	b, err := json.Marshal(st)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(file), 0o755); err != nil {
		return err
	}
	f, err := os.CreateTemp(filepath.Dir(file), ".update-check-*")
	if err != nil {
		return err
	}
	_, err = f.Write(b)
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(f.Name(), file)
	}
	if err != nil {
		os.Remove(f.Name())
	}
	return err
}
