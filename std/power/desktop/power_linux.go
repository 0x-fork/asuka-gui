//go:build linux && !android

package power

import (
	"context"
	"math"
	"os"
	"time"

	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux, through the system's services over D-Bus: UPower for the
// battery (its OnBattery, and the display device: the batteries as one,
// its percentage and whether it charges), GIO's power profile monitor for
// the power saver profile (power-profiles-daemon, through the portal in a
// sandbox), logind for sleep (PrepareForSleep) and the session's lock
// (LockedHint), and the screen saver's ActiveChanged (GNOME's, KDE's),
// which comes as the screen locks. Linux has no thermal state for apps:
// nominal. The idle time is GNOME's idle monitor's (Mutter), else the
// screen saver's (KDE). What's followed starts with the first watcher
// and goes on (the subscriptions are cheap), answering each watcher.

const (
	upowerDest    = "org.freedesktop.UPower"
	upowerPath    = "/org/freedesktop/UPower"
	upowerDisplay = "/org/freedesktop/UPower/devices/DisplayDevice"
	login1Dest    = "org.freedesktop.login1"
	login1Path    = "/org/freedesktop/login1"
	propsIface    = "org.freedesktop.DBus.Properties"
)

var p struct {
	started            bool
	reading            int  // UPower reads in progress
	again              bool // read again once they're done
	onBattery, present bool
	percentage         float64
	charging           bool
	saver              bool
	profiles           Ptr // GPowerProfileMonitor
	locked, lockKnown  bool

	next     int
	changed  map[int]func()
	happened map[int]func(Event)
}

func supported() error { return nil }

func state() Status {
	start()
	s := Status{Level: -1, LowPower: p.saver}
	if p.present {
		s.Battery, s.OnBattery, s.Charging = true, p.onBattery, p.charging
		s.Level = math.Round(min(max(p.percentage, 0), 100)) / 100
	}
	return s
}

func follow(changed func(), happened func(Event)) (stop func()) {
	start()
	p.next++
	id := p.next
	if changed != nil {
		p.changed[id] = changed
	}
	if happened != nil {
		p.happened[id] = happened
	}
	return func() {
		delete(p.changed, id)
		delete(p.happened, id)
	}
}

func powerChanged() {
	for _, f := range p.changed {
		f()
	}
}

func emit(e Event) {
	for _, f := range p.happened {
		f(e)
	}
}

// start follows the power's sources, once. Main thread.
func start() {
	if p.started {
		return
	}
	p.started = true
	p.changed, p.happened = map[int]func(){}, map[int]func(Event){}
	if Desk.PowerProfileMonitorDup != nil {
		p.profiles = Desk.PowerProfileMonitorDup()
		if p.profiles != 0 {
			p.saver = Desk.PowerProfileSaverOn(p.profiles)
			Connect(p.profiles, "notify::power-saver-enabled", func(uintptr) {
				p.saver = Desk.PowerProfileSaverOn(p.profiles)
				powerChanged()
			})
		}
	}
	if conn, err := Bus(BusSystem); err == nil {
		for _, path := range []string{upowerPath, upowerDisplay} {
			DBusSubscribe(conn, upowerDest, propsIface, "PropertiesChanged", path, func(DBusSignal) { readUPower() })
		}
		DBusSubscribe(conn, login1Dest, "org.freedesktop.login1.Manager", "PrepareForSleep", login1Path, func(s DBusSignal) {
			if a, _ := VariantGo(s.Params).([]any); len(a) > 0 {
				if sleeping, _ := a[0].(bool); sleeping {
					emit(EventSuspend)
				} else {
					emit(EventResume)
				}
			}
		})
		followSession(conn)
		readUPower()
	}
	if conn, err := Bus(BusSession); err == nil {
		for _, iface := range []string{"org.gnome.ScreenSaver", "org.freedesktop.ScreenSaver"} {
			DBusSubscribe(conn, "", iface, "ActiveChanged", "", func(s DBusSignal) {
				if a, _ := VariantGo(s.Params).([]any); len(a) > 0 {
					on, _ := a[0].(bool)
					setLocked(on)
				}
			})
		}
	}
}

// followSession finds the app's logind session and follows its
// LockedHint.
func followSession(conn Ptr) {
	follow := func(path string) {
		DBusSubscribe(conn, login1Dest, propsIface, "PropertiesChanged", path, func(s DBusSignal) {
			a, _ := VariantGo(s.Params).([]any)
			if len(a) < 3 {
				return
			}
			if changed, _ := a[1].(map[string]any); changed != nil {
				if v, ok := changed["LockedHint"].(bool); ok {
					setLocked(v)
					return
				}
			}
			if inv, _ := a[2].([]any); len(inv) > 0 {
				for _, k := range inv {
					if k == "LockedHint" {
						readLockedHint(conn, path)
					}
				}
			}
		})
		readLockedHint(conn, path)
	}
	sessionPath := func(r Ptr, err error) string {
		if err != nil {
			return ""
		}
		if a, _ := VariantGo(r).([]any); len(a) > 0 {
			s, _ := a[0].(string)
			return s
		}
		return ""
	}
	DBusCall(conn, login1Dest, login1Path, "org.freedesktop.login1.Manager", "GetSessionByPID", VTuple(VU32(uint32(os.Getpid()))), 2000, func(r Ptr, err error) {
		if path := sessionPath(r, err); path != "" {
			follow(path)
			return
		}
		// A process outside the session (a user service): the session the
		// environment names.
		if id := os.Getenv("XDG_SESSION_ID"); id != "" {
			DBusCall(conn, login1Dest, login1Path, "org.freedesktop.login1.Manager", "GetSession", VTuple(VStr(id)), 2000, func(r Ptr, err error) {
				if path := sessionPath(r, err); path != "" {
					follow(path)
				}
			})
		}
	})
}

func readLockedHint(conn Ptr, path string) {
	DBusCall(conn, login1Dest, path, propsIface, "Get", VTuple(VStr("org.freedesktop.login1.Session"), VStr("LockedHint")), 2000, func(r Ptr, err error) {
		if err != nil {
			return
		}
		if a, _ := VariantGo(r).([]any); len(a) > 0 {
			if v, ok := a[0].(bool); ok {
				setLocked(v)
			}
		}
	})
}

// setLocked notes the screen locked or unlocked: an event when it
// changes (the first state known isn't one).
func setLocked(on bool) {
	if p.lockKnown && p.locked == on {
		return
	}
	known := p.lockKnown
	p.locked, p.lockKnown = on, true
	if !known {
		return
	}
	if on {
		emit(EventLock)
	} else {
		emit(EventUnlock)
	}
}

// readUPower reads UPower's OnBattery and its display device.
func readUPower() {
	if p.reading > 0 {
		p.again = true
		return
	}
	conn, err := Bus(BusSystem)
	if err != nil {
		powerChanged()
		return
	}
	p.reading = 2
	done := func() {
		p.reading--
		if p.reading > 0 {
			return
		}
		if p.again {
			p.again = false
			readUPower()
			return
		}
		powerChanged()
	}
	getAll := func(path, iface string, f func(map[string]any)) {
		DBusCall(conn, upowerDest, path, propsIface, "GetAll", VTuple(VStr(iface)), 2000, func(r Ptr, err error) {
			var m map[string]any
			if err == nil {
				if a, _ := VariantGo(r).([]any); len(a) > 0 {
					m, _ = a[0].(map[string]any)
				}
			}
			f(m)
			done()
		})
	}
	getAll(upowerPath, "org.freedesktop.UPower", func(m map[string]any) {
		p.onBattery, _ = m["OnBattery"].(bool)
	})
	getAll(upowerDisplay, "org.freedesktop.UPower.Device", func(m map[string]any) {
		p.present, _ = m["IsPresent"].(bool)
		// Type: 2 battery, 3 UPS; the display device of a desktop without
		// one isn't present.
		if t, _ := m["Type"].(uint32); t != 2 && t != 3 {
			p.present = false
		}
		p.percentage, _ = m["Percentage"].(float64)
		st, _ := m["State"].(uint32)
		p.charging = st == 1 || st == 5 // charging, pending charge
	})
}

// Idle is the time since the user's last input, from GNOME's idle
// monitor or the screen saver (KDE).
func (monitor) Idle(ctx context.Context) (time.Duration, error) {
	type answer struct {
		d  time.Duration
		ok bool
	}
	ch := make(chan answer, 1)
	native.Post(func() {
		conn, err := Bus(BusSession)
		if err != nil {
			ch <- answer{}
			return
		}
		DBusCall(conn, "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core", "org.gnome.Mutter.IdleMonitor", "GetIdletime", 0, 2000, func(r Ptr, err error) {
			if err == nil {
				if a, _ := VariantGo(r).([]any); len(a) > 0 {
					if v, ok := a[0].(uint64); ok {
						ch <- answer{time.Duration(v) * time.Millisecond, true}
						return
					}
				}
			}
			DBusCall(conn, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "GetSessionIdleTime", 0, 2000, func(r Ptr, err error) {
				if err == nil {
					if a, _ := VariantGo(r).([]any); len(a) > 0 {
						if v, ok := a[0].(uint32); ok {
							ch <- answer{time.Duration(v) * time.Second, true}
							return
						}
					}
				}
				ch <- answer{}
			})
		})
	})
	select {
	case a := <-ch:
		if !a.ok {
			return 0, ErrIdleUnknown
		}
		return a.d, nil
	case <-ctx.Done():
		return 0, ctx.Err()
	}
}
