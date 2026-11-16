//go:build linux && !android

package appearance

import (
	"math"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: the settings portal's appearance (org.freedesktop.appearance:
// accent-color, GNOME 47's and KDE's, an sRGB triple; contrast, 1 for
// high), read and followed by its SettingChanged while watched. Without
// the portal (or a contrast in it), high contrast is libadwaita's (its
// style manager's, which reads the portal or a HighContrast theme), and
// there's no accent. Linux desktops have no Reduce Transparency.

const (
	settingsIface = "org.freedesktop.portal.Settings"
	appearanceNS  = "org.freedesktop.appearance"
)

// portal is what the portal said, kept while watched (and after: the
// last known).
var portal struct {
	accent        [3]int
	hasAccent     bool
	contrast      bool
	contrastKnown bool
}

func supported() error { return nil }

func settings() Settings {
	s := Settings{HasAccent: portal.hasAccent, Red: portal.accent[0], Green: portal.accent[1], Blue: portal.accent[2]}
	if portal.contrastKnown {
		s.IncreaseContrast = portal.contrast
	} else if m := AdwStyleManagerGetDefault(); m != 0 {
		s.IncreaseContrast = AdwStyleManagerGetHighContrast(m)
	}
	return s
}

func follow(changed func()) (stop func()) {
	var stops []func()
	if m := AdwStyleManagerGetDefault(); m != 0 {
		h := Connect(m, "notify::high-contrast", func(uintptr) { changed() })
		stops = append(stops, func() { Disconnect(m, h) })
	}
	if conn, err := Bus(BusSession); err == nil {
		sub := DBusSubscribe(conn, PortalDest, settingsIface, "SettingChanged", PortalPath, func(s DBusSignal) {
			args, _ := VariantGo(s.Params).([]any)
			if len(args) < 3 {
				return
			}
			if ns, _ := args[0].(string); ns != appearanceNS {
				return
			}
			key, _ := args[1].(string)
			setting(key, args[2])
			changed()
		})
		stops = append(stops, func() { DBusUnsubscribe(conn, sub) })
		for _, key := range []string{"accent-color", "contrast"} {
			key := key
			readSetting(conn, key, func(v any) {
				setting(key, v)
				changed()
			})
		}
	}
	return func() {
		for _, f := range stops {
			f()
		}
	}
}

// readSetting reads one of the portal's appearance settings (ReadOne,
// version 2; Read, which wraps the value in one more variant, before
// it): nil when it has none.
func readSetting(conn Ptr, key string, done func(any)) {
	params := func() Ptr { return VTuple(VStr(appearanceNS), VStr(key)) }
	value := func(r Ptr) any {
		a, _ := VariantGo(r).([]any) // variants are unwrapped: one level or two
		if len(a) == 0 {
			return nil
		}
		return a[0]
	}
	DBusCall(conn, PortalDest, PortalPath, settingsIface, "ReadOne", params(), 2000, func(r Ptr, err error) {
		if err == nil {
			done(value(r))
			return
		}
		DBusCall(conn, PortalDest, PortalPath, settingsIface, "Read", params(), 2000, func(r Ptr, err error) {
			if err != nil {
				done(nil)
				return
			}
			done(value(r))
		})
	})
}

// setting takes a setting's value (nil: none).
func setting(key string, v any) {
	switch key {
	case "accent-color":
		portal.hasAccent = false
		rgb, _ := v.([]any)
		if len(rgb) != 3 {
			return
		}
		var c [3]int
		for i, x := range rgb {
			f, ok := x.(float64)
			// Out of the range: the user set none.
			if !ok || f < 0 || f > 1 || math.IsNaN(f) {
				return
			}
			c[i] = int(math.Round(f * 255))
		}
		portal.accent, portal.hasAccent = c, true
	case "contrast":
		u, ok := v.(uint32)
		portal.contrast, portal.contrastKnown = u == 1, ok
	}
}
