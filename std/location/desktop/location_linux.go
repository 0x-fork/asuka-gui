//go:build linux && !android

package location

import (
	"errors"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: the location portal (org.freedesktop.portal.Location, which
// asks the user and works in a sandbox), else GeoClue2 on the system bus;
// a place's name through geocode-glib, where it's installed. Neither says
// what the user allowed before an app asks for a position, so it's what
// this run has seen: notAsked until a position comes (always: a desktop
// app runs out of sight) or a refusal (denied). No geofences: the
// package computes the regions from the positions. Main thread.

var seen struct{ known, allowed bool }

func access() Authorization {
	switch {
	case !seen.known:
		return Authorization{Level: LevelNotAsked}
	case seen.allowed:
		return Authorization{Level: LevelAlways, Precise: true}
	}
	return Authorization{Level: LevelDenied}
}

func saw(allowed bool) {
	if seen.known && seen.allowed == allowed {
		return
	}
	seen.known, seen.allowed = true, allowed
	authorizationChanged()
}

func requestAccess(done func()) {
	if seen.known {
		done()
		return
	}
	fix(func(Position, error) { done() })
}

// fix is one position: a source started for it.
func fix(done func(Position, error)) {
	finished := false
	var stop func()
	stop = start(func(p Position) {
		if finished {
			return
		}
		finished = true
		if stop != nil {
			stop()
		}
		done(p, nil)
	}, func(err error) {
		if finished {
			return
		}
		finished = true
		done(Position{}, err)
	})
	if finished {
		stop()
	}
}

func watchFixes(moved func(Position), failed func(error)) func() { return start(moved, failed) }

// start starts a source: the portal's, else GeoClue's.
func start(update func(Position), fail func(error)) func() {
	up := func(p Position) {
		saw(true)
		update(p)
	}
	failed := func(err error) {
		if errors.Is(err, ErrDenied) {
			saw(false)
		}
		fail(err)
	}
	return portal(up, failed)
}

func geocode(lat, lon float64, done func(string, error)) {
	GeocodeReverse(lat, lon, func(p GeocodePlace, err error) {
		if err != nil {
			done("", err)
			return
		}
		var parts []string
		have := map[string]bool{}
		for _, k := range [][]string{{p.Area, p.Name}, {p.Town, p.State, p.Country}} {
			for _, v := range k {
				if v != "" {
					if !have[v] {
						have[v] = true
						parts = append(parts, v)
					}
					break
				}
			}
		}
		done(strings.Join(parts, ", "), nil)
	})
}

func openSettings() error {
	if err := exec.Command("gnome-control-center", "location").Start(); err != nil {
		return errors.New("unsupported: no GNOME Settings")
	}
	return nil
}

func support() Support { return Support{Dwell: true} }

func addFence(Region) error                           { return ErrUnavailable }
func removeFence(string)                              {}
func fences() []Region                                { return nil }
func watchFences(func(string, Transition, *Position)) {}

// position is the portal's or GeoClue's properties as a Position.
func position(p map[string]any) Position {
	f := func(k string) float64 {
		v, _ := p[k].(float64)
		return v
	}
	alt := f("Altitude")
	if alt < -1e300 || math.IsNaN(alt) { // -DBL_MAX: not known
		alt = 0
	}
	t := time.Now().UnixNano()
	if ts, ok := p["Timestamp"].([]any); ok && len(ts) == 2 {
		s, _ := ts[0].(uint64)
		us, _ := ts[1].(uint64)
		if s > 0 {
			t = int64(s)*1e9 + int64(us)*1e3
		}
	}
	known := func(k string) float64 {
		if v, ok := p[k].(float64); ok && v >= 0 && !math.IsNaN(v) {
			return v
		}
		return -1
	}
	return Position{Latitude: f("Latitude"), Longitude: f("Longitude"), Accuracy: f("Accuracy"), Altitude: alt,
		Speed: known("Speed"), Course: known("Heading"), Time: int(t)}
}

const (
	portalDest = "org.freedesktop.portal.Desktop"
	portalPath = "/org/freedesktop/portal/desktop"
	geoclue    = "org.freedesktop.GeoClue2"
)

var tokens int

func token() string {
	tokens++
	return "asukaloc" + strconv.Itoa(tokens)
}

func noService(err error) bool {
	if err == nil {
		return false
	}
	for _, e := range []string{"ServiceUnknown", "UnknownMethod", "UnknownInterface", "UnknownObject", "No such interface", "NameHasNoOwner"} {
		if strings.Contains(err.Error(), e) {
			return true
		}
	}
	return false
}

// portal starts a location portal session (positions come as its
// LocationUpdated signals), else GeoClue.
func portal(update func(Position), fail func(error)) func() {
	conn, err := Bus(BusSession)
	if err != nil {
		return geoclue2(update, fail)
	}
	stopped := false
	session := ""
	var sub, respSub uint32
	var fallback func()
	opts := VDict(map[string]Ptr{
		"session_handle_token": VStr(token()),
		"distance-threshold":   VU32(0),
		"time-threshold":       VU32(0),
		"accuracy":             VU32(5), // exact
	})
	DBusCall(conn, portalDest, portalPath, "org.freedesktop.portal.Location", "CreateSession", VTuple(opts), -1, func(r Ptr, err error) {
		if stopped {
			return
		}
		if err != nil {
			if noService(err) {
				fallback = geoclue2(update, fail)
				return
			}
			if strings.Contains(err.Error(), "NotAllowed") {
				err = ErrDenied
			}
			fail(err)
			return
		}
		args, _ := VariantGo(r).([]any)
		if len(args) == 0 {
			fail(errors.New("location: a bad reply from the portal"))
			return
		}
		session, _ = args[0].(string)
		sub = DBusSubscribe(conn, portalDest, "org.freedesktop.portal.Location", "LocationUpdated", portalPath, func(sig DBusSignal) {
			a, _ := VariantGo(sig.Params).([]any)
			if len(a) < 2 || stopped {
				return
			}
			if p, _ := a[0].(string); p != session {
				return
			}
			pos, _ := a[1].(map[string]any)
			update(position(pos))
		})
		// Start answers through a Request: its Response says whether the
		// user allowed it.
		tok := token()
		sender := strings.ReplaceAll(strings.TrimPrefix(UniqueName(conn), ":"), ".", "_")
		path := "/org/freedesktop/portal/desktop/request/" + sender + "/" + tok
		respSub = DBusSubscribe(conn, portalDest, "org.freedesktop.portal.Request", "Response", path, func(s DBusSignal) {
			DBusUnsubscribe(conn, respSub)
			respSub = 0
			a, _ := VariantGo(s.Params).([]any)
			var code uint32 = 2
			if len(a) > 0 {
				code, _ = a[0].(uint32)
			}
			if code != 0 && !stopped {
				fail(ErrDenied)
			}
		})
		DBusCall(conn, portalDest, portalPath, "org.freedesktop.portal.Location", "Start",
			VTuple(VPath(session), VStr(""), VDict(map[string]Ptr{"handle_token": VStr(tok)})), -1, func(_ Ptr, err error) {
				if err != nil && !stopped {
					fail(err)
				}
			})
	})
	return func() {
		if fallback != nil {
			fallback()
			return
		}
		if stopped {
			return
		}
		stopped = true
		if sub != 0 {
			DBusUnsubscribe(conn, sub)
		}
		if respSub != 0 {
			DBusUnsubscribe(conn, respSub)
		}
		if session != "" {
			DBusCall(conn, portalDest, session, "org.freedesktop.portal.Session", "Close", 0, -1, func(Ptr, error) {})
		}
	}
}

// geoclue2 asks GeoClue2 itself, on the system bus.
func geoclue2(update func(Position), fail func(error)) func() {
	conn, err := Bus(BusSystem)
	if err != nil {
		fail(errors.New("location: no location service (" + err.Error() + ")"))
		return func() {}
	}
	stopped := false
	client := ""
	var sub uint32
	stop := func() {
		if stopped {
			return
		}
		stopped = true
		if sub != 0 {
			DBusUnsubscribe(conn, sub)
		}
		if client != "" {
			DBusCall(conn, geoclue, client, geoclue+".Client", "Stop", 0, -1, func(Ptr, error) {})
		}
	}
	failed := func(err error) {
		if stopped {
			return
		}
		stop()
		switch {
		case strings.Contains(err.Error(), "AccessDenied"):
			fail(ErrDenied)
		case noService(err):
			fail(errors.New("location: no location service (GeoClue isn't installed)"))
		default:
			fail(err)
		}
	}
	DBusCall(conn, geoclue, "/org/freedesktop/GeoClue2/Manager", geoclue+".Manager", "GetClient", 0, -1, func(r Ptr, err error) {
		if err != nil {
			failed(err)
			return
		}
		args, _ := VariantGo(r).([]any)
		if len(args) == 0 {
			failed(errors.New("location: a bad reply from GeoClue"))
			return
		}
		client, _ = args[0].(string)
		if stopped {
			DBusCall(conn, geoclue, client, geoclue+".Client", "Stop", 0, -1, func(Ptr, error) {})
			return
		}
		set := func(prop string, v Ptr, then func()) {
			DBusCall(conn, geoclue, client, "org.freedesktop.DBus.Properties", "Set",
				VTuple(VStr(geoclue+".Client"), VStr(prop), VVariant(v)), -1, func(_ Ptr, err error) {
					if err != nil {
						failed(err)
						return
					}
					then()
				})
		}
		sub = DBusSubscribe(conn, geoclue, geoclue+".Client", "LocationUpdated", client, func(sig DBusSignal) {
			a, _ := VariantGo(sig.Params).([]any)
			if len(a) < 2 || stopped {
				return
			}
			at, _ := a[1].(string)
			DBusCall(conn, geoclue, at, "org.freedesktop.DBus.Properties", "GetAll", VTuple(VStr(geoclue+".Location")), -1, func(r Ptr, err error) {
				if err != nil || stopped {
					return
				}
				if a, _ := VariantGo(r).([]any); len(a) > 0 {
					if pos, ok := a[0].(map[string]any); ok {
						update(position(pos))
					}
				}
			})
		})
		// GeoClue's agent allows apps by their desktop file's name.
		id := strings.TrimSuffix(filepath.Base(os.Args[0]), filepath.Ext(os.Args[0]))
		set("DesktopId", VStr(id), func() {
			set("RequestedAccuracyLevel", VU32(8), func() { // exact
				DBusCall(conn, geoclue, client, geoclue+".Client", "Start", 0, -1, func(_ Ptr, err error) {
					if err != nil {
						failed(err)
					}
				})
			})
		})
	})
	return stop
}
