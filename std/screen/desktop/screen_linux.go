//go:build linux && !android

package screen

import (
	"math"

	gtk "github.com/egoist/asuka/internal/gtk"
)

// Linux: the default GdkDisplay's monitors (gdk_display_get_monitors,
// which changes as one's plugged in or out), each one's geometry in
// application pixels (points, in the desktop's layout), its scale, refresh
// rate, connector (the ID: DP-1, eDP-1) and description. GTK 4 has no
// primary monitor of its own: on X11 it's the X server's and the work area
// the window manager's (_NET_WORKAREA); on Wayland the compositor keeps
// both, so the work area is the bounds, and the primary monitor is GNOME's
// (Mutter's DisplayConfig, its logical monitor marked primary), else the
// one at the layout's origin, else the first. GTK can't keep a window out
// of captures: preventCapture does nothing.

var dw struct {
	started  bool
	list     gtk.Ptr          // the display's monitors (GListModel)
	monitors map[gtk.Ptr]bool // those whose changes are followed (a reference each)
	primary  string           // Mutter's primary monitor's connector, "" if not known
	mutter   bool             // Mutter's state is being asked
	next     int
	changed  map[int]func()
}

func supported() error { return nil }

func preventCapture(bool) {}

func displaysChanged() {
	for _, f := range dw.changed {
		f()
	}
}

func follow(changed func()) (stop func()) {
	start()
	dw.next++
	id := dw.next
	dw.changed[id] = changed
	return func() { delete(dw.changed, id) }
}

// start follows the monitors, once. Main thread.
func start() bool {
	if dw.started {
		return true
	}
	disp := gtk.DisplayGetDefault()
	if disp == 0 || gtk.Desk.DisplayGetMonitors == nil {
		return false
	}
	dw.started = true
	dw.monitors = map[gtk.Ptr]bool{}
	dw.changed = map[int]func(){}
	dw.list = gtk.Desk.DisplayGetMonitors(disp)
	gtk.Connect(dw.list, "items-changed", func(_, _, _ uintptr) { followMonitors(); displaysChanged() })
	if !gtk.IsA(disp, "GdkX11Display") {
		// GNOME on Wayland says which monitor is primary.
		if conn, err := gtk.Bus(gtk.BusSession); err == nil {
			gtk.DBusSubscribe(conn, "org.gnome.Mutter.DisplayConfig", "org.gnome.Mutter.DisplayConfig", "MonitorsChanged", "/org/gnome/Mutter/DisplayConfig", func(gtk.DBusSignal) {
				askMutter()
			})
			askMutter()
		}
	}
	followMonitors()
	return true
}

// askMutter reads GNOME's primary monitor (GetCurrentState's logical
// monitors), then reports the displays again.
func askMutter() {
	conn, err := gtk.Bus(gtk.BusSession)
	if err != nil || dw.mutter {
		return
	}
	dw.mutter = true
	gtk.DBusCall(conn, "org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig", "org.gnome.Mutter.DisplayConfig", "GetCurrentState",
		0, 2000, func(r gtk.Ptr, err error) {
			dw.mutter = false
			dw.primary = ""
			if err == nil {
				dw.primary = mutterPrimary(gtk.VariantGo(r))
			}
			displaysChanged()
		})
}

// mutterPrimary is the connector of GetCurrentState's primary logical
// monitor's first monitor ("" if none says).
func mutterPrimary(state any) string {
	a, _ := state.([]any)
	if len(a) < 3 {
		return ""
	}
	logical, _ := a[2].([]any)
	for _, l := range logical {
		lm, _ := l.([]any)
		if len(lm) < 6 {
			continue
		}
		if p, _ := lm[4].(bool); !p {
			continue
		}
		mons, _ := lm[5].([]any)
		for _, m := range mons {
			if spec, _ := m.([]any); len(spec) > 0 {
				if c, _ := spec[0].(string); c != "" {
					return c
				}
			}
		}
	}
	return ""
}

// followMonitors follows the changes of the monitors there are, and lets
// go of those gone.
func followMonitors() {
	n := gtk.Sys.ListModelGetNItems(dw.list)
	seen := make(map[gtk.Ptr]bool, n)
	for i := range n {
		m := gtk.Sys.ListModelGetItem(dw.list, i)
		if m == 0 {
			continue
		}
		seen[m] = true
		if !dw.monitors[m] {
			dw.monitors[m] = true
			gtk.ObjectRef(m)
			for _, s := range []string{"notify::geometry", "notify::scale", "notify::scale-factor", "notify::refresh-rate", "notify::description"} {
				gtk.Connect(m, s, func(uintptr) { displaysChanged() })
			}
		}
		gtk.ObjectUnref(m)
	}
	for m := range dw.monitors {
		if !seen[m] {
			delete(dw.monitors, m)
			gtk.ObjectUnref(m) // its handlers go with it
		}
	}
}

// read is the monitors, the primary first.
func read() []Display {
	if !start() {
		return []Display{}
	}
	disp := gtk.DisplayGetDefault()
	x11 := gtk.IsA(disp, "GdkX11Display")
	var x11Primary gtk.Ptr
	if x11 && gtk.Desk.X11DisplayGetPrimary != nil {
		x11Primary = gtk.Desk.X11DisplayGetPrimary(disp)
	}
	n := gtk.Sys.ListModelGetNItems(dw.list)
	out := []Display{}
	var mons []gtk.Ptr
	for i := range n {
		m := gtk.Sys.ListModelGetItem(dw.list, i)
		if m == 0 {
			continue
		}
		mons = append(mons, m)
		out = append(out, monitorDisplay(m, x11))
		gtk.ObjectUnref(m)
	}
	primary := -1
	for i, m := range mons {
		if x11 && m == x11Primary || !x11 && dw.primary != "" && out[i].Id == dw.primary {
			primary = i
			break
		}
	}
	if primary < 0 {
		for i := range out {
			if out[i].Bounds.X == 0 && out[i].Bounds.Y == 0 {
				primary = i
				break
			}
		}
	}
	if primary < 0 && len(out) > 0 {
		primary = 0
	}
	if primary > 0 {
		p := out[primary]
		copy(out[1:primary+1], out[:primary])
		out[0] = p
	}
	if len(out) > 0 {
		out[0].Primary = true
	}
	return out
}

func rectOf(r gtk.Rectangle) Rect {
	return Rect{X: float64(r.X), Y: float64(r.Y), Width: float64(r.Width), Height: float64(r.Height)}
}

// monitorDisplay describes a monitor (not yet whether it's primary).
func monitorDisplay(m gtk.Ptr, x11 bool) Display {
	var g gtk.Rectangle
	gtk.Desk.MonitorGetGeometry(m, &g)
	o := Display{Bounds: rectOf(g), WorkArea: rectOf(g)}
	o.Id = gtk.GoString(gtk.Desk.MonitorGetConnector(m))
	switch {
	case gtk.Desk.MonitorGetDescription != nil && gtk.Desk.MonitorGetDescription(m) != 0:
		o.Name = gtk.GoString(gtk.Desk.MonitorGetDescription(m))
	default:
		maker, model := gtk.GoString(gtk.Desk.MonitorGetManufacturer(m)), gtk.GoString(gtk.Desk.MonitorGetModel(m))
		o.Name = maker
		if model != "" {
			if o.Name != "" {
				o.Name += " "
			}
			o.Name += model
		}
	}
	if gtk.Desk.MonitorGetScale != nil {
		o.Scale = gtk.Desk.MonitorGetScale(m)
	}
	if o.Scale <= 0 {
		o.Scale = float64(gtk.Desk.MonitorGetScaleFactor(m))
	}
	if hz := gtk.Desk.MonitorGetRefreshRate(m); hz > 0 {
		o.RefreshRate = math.Round(float64(hz)/10) / 100 // milli-Hertz, to 0.01
	}
	if x11 && gtk.Desk.X11MonitorGetWorkarea != nil {
		var w gtk.Rectangle
		gtk.Desk.X11MonitorGetWorkarea(m, &w)
		if w.Width > 0 && w.Height > 0 {
			o.WorkArea = rectOf(w)
		}
	}
	return o
}
