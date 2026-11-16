//go:build linux && !android

package nowplaying

import (
	"context"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/egoist/asuka/internal/boot"
	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: an MPRIS player (org.mpris.MediaPlayer2 and its .Player) on the
// session bus, which GNOME Shell's and KDE Plasma's media controls, their
// lock screens, the desktops' media keys (gsd-media-keys, KDE's), and
// playerctl show and drive. The player is exported on a session bus
// connection of its own, with the bus name
// org.mpris.MediaPlayer2.<app>.instance<pid>; clear closes it, so the
// name goes and the desktops drop the player at once. Clients advance
// the position at the rate themselves (they read Position, which this
// extrapolates, when they need it) and hear of jumps through Seeked.

const (
	mprisPath   = "/org/mpris/MediaPlayer2"
	mprisRoot   = "org.mpris.MediaPlayer2"
	mprisPlayer = "org.mpris.MediaPlayer2.Player"
)

const mprisXML = `<node>
<interface name="org.mpris.MediaPlayer2">
<method name="Raise"/>
<method name="Quit"/>
<property name="CanQuit" type="b" access="read"/>
<property name="CanRaise" type="b" access="read"/>
<property name="HasTrackList" type="b" access="read"/>
<property name="Identity" type="s" access="read"/>
<property name="DesktopEntry" type="s" access="read"/>
<property name="SupportedUriSchemes" type="as" access="read"/>
<property name="SupportedMimeTypes" type="as" access="read"/>
</interface>
<interface name="org.mpris.MediaPlayer2.Player">
<method name="Next"/>
<method name="Previous"/>
<method name="Pause"/>
<method name="PlayPause"/>
<method name="Stop"/>
<method name="Play"/>
<method name="Seek"><arg direction="in" name="Offset" type="x"/></method>
<method name="SetPosition"><arg direction="in" name="TrackId" type="o"/><arg direction="in" name="Position" type="x"/></method>
<method name="OpenUri"><arg direction="in" name="Uri" type="s"/></method>
<signal name="Seeked"><arg name="Position" type="x"/></signal>
<property name="PlaybackStatus" type="s" access="read"/>
<property name="Rate" type="d" access="read"/>
<property name="Metadata" type="a{sv}" access="read"/>
<property name="Volume" type="d" access="read"/>
<property name="Position" type="x" access="read"/>
<property name="MinimumRate" type="d" access="read"/>
<property name="MaximumRate" type="d" access="read"/>
<property name="CanGoNext" type="b" access="read"/>
<property name="CanGoPrevious" type="b" access="read"/>
<property name="CanPlay" type="b" access="read"/>
<property name="CanPause" type="b" access="read"/>
<property name="CanSeek" type="b" access="read"/>
<property name="CanControl" type="b" access="read"/>
</interface>
</node>`

// mpris is the player, main thread only.
type mpris struct {
	conn   Ptr // its own session bus connection; 0 while nothing shows
	regs   [2]uint32
	name   string
	info   *Info
	at     time.Time // when info came
	track  int       // counts the tracks, for their ids
	artURL string
}

var player mpris

func newCenter() (Center, error) { return &player, nil }

func (p *mpris) Watch(ctx context.Context, command func(Command, float64)) error {
	return watch(ctx, command)
}

// Set shows info: the player is exported (and its name owned) when
// something first shows; after, what changed is announced.
func (p *mpris) Set(info Info) error {
	old := p.info
	expected := p.position()
	if old == nil || old.Title != info.Title || old.Artist != info.Artist || old.Album != info.Album || old.Duration != info.Duration {
		p.track++
	}
	p.info, p.at = &info, time.Now()
	p.artURL = ""
	if info.Artwork != "" {
		p.artURL = fileURI(info.Artwork)
	}
	if p.conn == 0 {
		if err := p.open(); err != nil {
			p.info = nil
			return err
		}
		return nil
	}
	p.changed(mprisPlayer, "PlaybackStatus", "Rate", "Metadata", "CanGoNext", "CanGoPrevious", "CanPlay", "CanPause", "CanSeek")
	// Clients advance the position at the rate, and hear only of jumps.
	if d := info.Position - expected; old != nil && math.Abs(d) > 0.25 {
		DBusEmit(p.conn, mprisPath, mprisPlayer, "Seeked", VTuple(VI64(micros(info.Position))))
	}
	return nil
}

// fileURI is an artwork's source as a URI: a URL, a file, an asset.
func fileURI(source string) string {
	if strings.Contains(source, "://") {
		return source
	}
	path := native.Asset(source)
	if abs, err := filepath.Abs(path); err == nil {
		path = abs
	}
	f := Sys.FileNewForPath(path)
	defer ObjectUnref(f)
	return GoStringFree(Sys.FileGetURI(f))
}

// open exports the player on a connection of its own and owns its name
// there.
func (p *mpris) open() error {
	conn, err := privateSessionBus()
	if err != nil {
		return err
	}
	for i, iface := range []string{mprisRoot, mprisPlayer} {
		reg, err := DBusRegister(conn, mprisPath, mprisXML, iface, DBusObject{
			Method:   func(c DBusInvocation) { p.method(c) },
			Property: func(name string) Ptr { return p.property(name) },
		})
		if err != nil {
			if i == 1 {
				DBusUnregister(conn, p.regs[0])
			}
			closeBus(conn)
			return err
		}
		p.regs[i] = reg
	}
	p.conn = conn
	p.name = fmt.Sprintf("%s.%s.instance%d", mprisRoot, busNameElement(appName()), os.Getpid())
	// DBUS_NAME_FLAG_DO_NOT_QUEUE: another player of the same app and pid
	// can't be; the name is the connection's until it closes.
	DBusCall(conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
		VTuple(VStr(p.name), VU32(4)), 5000, func(Ptr, error) {})
	return nil
}

// Clear takes the player off the bus: its connection closes, and its
// name with it.
func (p *mpris) Clear() error {
	p.info = nil
	if p.conn == 0 {
		return nil
	}
	DBusUnregister(p.conn, p.regs[0])
	DBusUnregister(p.conn, p.regs[1])
	closeBus(p.conn)
	p.conn, p.regs, p.name = 0, [2]uint32{}, ""
	return nil
}

func privateSessionBus() (Ptr, error) {
	var gerr Ptr
	addr := Call(Sym("g_dbus_address_get_for_bus_sync"), BusSession, 0, Addr(&gerr))
	if addr == 0 {
		return 0, GError(gerr)
	}
	// G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | MESSAGE_BUS_CONNECTION
	conn := Call(Sym("g_dbus_connection_new_for_address_sync"), addr, 1|8, 0, 0, Addr(&gerr))
	Call(FnFree, addr)
	if conn == 0 {
		return 0, GError(gerr)
	}
	return conn, nil
}

func closeBus(conn Ptr) {
	if conn == 0 {
		return
	}
	Call(Sym("g_dbus_connection_close_sync"), conn, 0, 0)
	ObjectUnref(conn)
}

// appName is the app's name: asuka.toml's, else its executable's.
func appName() string {
	if boot.Name != "" {
		return boot.Name
	}
	return filepath.Base(os.Args[0])
}

// appID is the application's ID: asuka.toml's, else one made of its name.
func appID() string {
	if boot.ID != "" {
		return boot.ID
	}
	var b strings.Builder
	for _, r := range appName() {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r >= '0' && r <= '9':
			b.WriteRune(r)
		case r == '-' || r == '_':
			b.WriteRune('_')
		}
	}
	return "dev.asuka.app." + b.String()
}

// busNameElement makes s an element of a bus name: ASCII letters,
// digits, _ and -, not starting with a digit.
func busNameElement(s string) string {
	var b strings.Builder
	for _, r := range s {
		switch {
		case r >= 'a' && r <= 'z', r >= 'A' && r <= 'Z', r == '_', r == '-':
			b.WriteRune(r)
		case r >= '0' && r <= '9':
			if b.Len() == 0 {
				b.WriteByte('_')
			}
			b.WriteRune(r)
		default:
			b.WriteByte('_')
		}
	}
	if b.Len() == 0 {
		return "asuka"
	}
	return b.String()
}

// position is where playback is now, in seconds.
func (p *mpris) position() float64 {
	i := p.info
	if i == nil {
		return 0
	}
	pos := i.Position
	if t := time.Since(p.at).Seconds(); i.State == StatePlaying && t > 0 {
		pos += t * i.Rate
	}
	if i.Duration > 0 {
		pos = min(pos, i.Duration)
	}
	return max(pos, 0)
}

func micros(seconds float64) int64 { return int64(math.Round(seconds * 1e6)) }

func (p *mpris) trackID() string { return fmt.Sprintf("/dev/asuka/track/%d", p.track) }

func (p *mpris) has(c Command) bool { return p.info != nil && p.info.Commands&(1<<int(c)) != 0 }

// property is a property's value, a floating variant (0: none).
func (p *mpris) property(name string) Ptr {
	switch name {
	case "CanQuit", "HasTrackList":
		return VBool(false)
	case "CanRaise":
		return VBool(true)
	case "Identity":
		return VStr(appName())
	case "DesktopEntry":
		return VStr(appID())
	case "SupportedUriSchemes", "SupportedMimeTypes":
		return VStrings(nil)
	case "PlaybackStatus":
		s := "Stopped"
		if p.info != nil {
			switch p.info.State {
			case StatePlaying:
				s = "Playing"
			case StatePaused:
				s = "Paused"
			}
		}
		return VStr(s)
	case "Rate":
		if p.info != nil && p.info.Rate > 0 {
			return VDouble(p.info.Rate)
		}
		return VDouble(1)
	case "Volume":
		return VDouble(1)
	case "MinimumRate":
		return VDouble(0.25)
	case "MaximumRate":
		return VDouble(4)
	case "Position":
		return VI64(micros(p.position()))
	case "Metadata":
		return p.metadata()
	case "CanGoNext":
		return VBool(p.has(CommandNext))
	case "CanGoPrevious":
		return VBool(p.has(CommandPrevious))
	case "CanPlay":
		return VBool(p.has(CommandPlay))
	case "CanPause":
		return VBool(p.has(CommandPause))
	case "CanSeek":
		return VBool(p.has(CommandSeek) || p.has(CommandSkipForward) || p.has(CommandSkipBackward))
	case "CanControl":
		return VBool(true)
	}
	return 0
}

func (p *mpris) metadata() Ptr {
	m := map[string]Ptr{"mpris:trackid": VPath(p.trackID())}
	if i := p.info; i != nil {
		if i.Title != "" {
			m["xesam:title"] = VStr(i.Title)
		}
		if i.Artist != "" {
			m["xesam:artist"] = VStrings([]string{i.Artist})
		}
		if i.Album != "" {
			m["xesam:album"] = VStr(i.Album)
		}
		if i.Duration > 0 {
			m["mpris:length"] = VI64(micros(i.Duration))
		}
		if p.artURL != "" {
			m["mpris:artUrl"] = VStr(p.artURL)
		}
	}
	return VDict(m)
}

// changed emits PropertiesChanged with the properties' values.
func (p *mpris) changed(iface string, props ...string) {
	m := make(map[string]Ptr, len(props))
	for _, name := range props {
		m[name] = p.property(name)
	}
	DBusEmit(p.conn, mprisPath, "org.freedesktop.DBus.Properties", "PropertiesChanged",
		VTuple(VStr(iface), VDict(m), VStrings(nil)))
}

// method answers a client's call: the player's commands go to the
// watchers.
func (p *mpris) method(c DBusInvocation) {
	switch c.Method {
	case "Raise":
		c.Return(0)
		native.Raise()
		return
	case "Quit":
		c.Return(0)
		return
	case "OpenUri":
		c.Error("org.freedesktop.DBus.Error.NotSupported", "the player opens no URIs")
		return
	}
	args, _ := VariantGo(c.Params).([]any)
	c.Return(0)
	switch c.Method {
	case "Play":
		send(p.info, CommandPlay, 0)
	case "Pause":
		send(p.info, CommandPause, 0)
	case "PlayPause":
		send(p.info, CommandTogglePlayPause, 0)
	case "Stop":
		send(p.info, CommandStop, 0)
	case "Next":
		send(p.info, CommandNext, 0)
	case "Previous":
		send(p.info, CommandPrevious, 0)
	case "Seek":
		// By an offset, in µs: a skip where the app skips, else a seek.
		if len(args) != 1 {
			return
		}
		off, _ := args[0].(int64)
		s := float64(off) / 1e6
		switch {
		case s > 0 && p.has(CommandSkipForward):
			send(p.info, CommandSkipForward, s)
		case s < 0 && p.has(CommandSkipBackward):
			send(p.info, CommandSkipBackward, -s)
		case s != 0 && p.has(CommandSeek):
			to := max(p.position()+s, 0)
			if p.info.Duration > 0 && to > p.info.Duration {
				send(p.info, CommandNext, 0) // past the end: the next track, as the specification says
				return
			}
			send(p.info, CommandSeek, to)
		}
	case "SetPosition":
		// Ignored for another track, and out of the track.
		if len(args) != 2 {
			return
		}
		id, _ := args[0].(string)
		us, _ := args[1].(int64)
		s := float64(us) / 1e6
		if p.info != nil && id == p.trackID() && s >= 0 && (p.info.Duration == 0 || s <= p.info.Duration) {
			send(p.info, CommandSeek, s)
		}
	}
}
