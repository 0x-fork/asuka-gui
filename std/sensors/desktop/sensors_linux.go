//go:build linux && !android

package sensors

import (
	"syscall"
	"time"
	"unsafe"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: iio-sensor-proxy (net.hadess.SensorProxy, on the system bus),
// which convertible laptops and tablets run. It gives the ambient light
// (in lux, when the sensor says so) and the accelerometer's orientation,
// not its readings: the accelerometer reads 1 g along the axis that's up
// (portrait upright: y; on its left side: -x; flat or unknown: z, face
// up), each time the orientation changes. Gyroscopes, magnetometers,
// barometers and pedometers have no service an app can read. Main
// thread.

const (
	proxyDest  = "net.hadess.SensorProxy"
	proxyPath  = "/net/hadess/SensorProxy"
	proxyIface = "net.hadess.SensorProxy"
)

// known are the proxy's properties as last read: asked for as the
// package's object is made (as the app starts), so that available, which
// answers at once, has them; and again as each stream starts.
var known map[string]any

// readProps asks the proxy for its properties: done gets them (nil: no
// proxy), on the main thread.
func readProps(done func(map[string]any)) {
	conn, err := Bus(BusSystem)
	if err != nil {
		done(nil)
		return
	}
	DBusCall(conn, proxyDest, proxyPath, "org.freedesktop.DBus.Properties", "GetAll", VTuple(VStr(proxyIface)), 3000, func(r Ptr, err error) {
		var p map[string]any
		if err == nil {
			if args, _ := VariantGo(r).([]any); len(args) > 0 {
				p, _ = args[0].(map[string]any)
			}
		}
		known = p
		done(p)
	})
}

func prime() { readProps(func(map[string]any) {}) }

func hasIn(p map[string]any, s Sensor) bool {
	switch s {
	case SensorAccelerometer:
		b, _ := p["HasAccelerometer"].(bool)
		return b
	case SensorLight:
		b, _ := p["HasAmbientLight"].(bool)
		unit, _ := p["LightLevelUnit"].(string)
		return b && unit == "lux"
	}
	return false
}

func has(s Sensor) bool {
	if s != SensorAccelerometer && s != SensorLight {
		return false
	}
	return hasIn(known, s)
}

// follow claims the sensor from the proxy, sends its reading now and
// each change, until stop.
func follow(s Sensor, p map[string]any, send func([]float64, time.Duration)) (func(), error) {
	if !hasIn(p, s) {
		return nil, ErrNotAvailable
	}
	conn, err := Bus(BusSystem)
	if err != nil {
		return nil, err
	}
	claim, release := "ClaimAccelerometer", "ReleaseAccelerometer"
	if s == SensorLight {
		claim, release = "ClaimLight", "ReleaseLight"
	}
	DBusCall(conn, proxyDest, proxyPath, proxyIface, claim, 0, 3000, func(Ptr, error) {})
	orientation, _ := p["AccelerometerOrientation"].(string)
	light, _ := p["LightLevel"].(float64)
	read := func() {
		if s == SensorAccelerometer {
			send(gravity(orientation), sinceBoot())
		} else {
			send([]float64{light}, sinceBoot())
		}
	}
	sub := DBusSubscribe(conn, proxyDest, "org.freedesktop.DBus.Properties", "PropertiesChanged", proxyPath, func(sig DBusSignal) {
		args, _ := VariantGo(sig.Params).([]any)
		if len(args) < 2 {
			return
		}
		changed, _ := args[1].(map[string]any)
		if o, ok := changed["AccelerometerOrientation"].(string); ok && s == SensorAccelerometer && o != orientation {
			orientation = o
			read()
		}
		if l, ok := changed["LightLevel"].(float64); ok && s == SensorLight && l != light {
			light = l
			read()
		}
	})
	// The reading now: it changes only as the device turns.
	read()
	return func() {
		DBusUnsubscribe(conn, sub)
		DBusCall(conn, proxyDest, proxyPath, proxyIface, release, 0, 3000, func(Ptr, error) {})
	}, nil
}

// gravity is what an accelerometer reads held still in the orientation
// iio-sensor-proxy names, in g, the device's axes (x right, y up the
// screen, z out of it).
func gravity(o string) []float64 {
	switch o {
	case "normal":
		return []float64{0, 1, 0}
	case "bottom-up":
		return []float64{0, -1, 0}
	case "left-up":
		return []float64{-1, 0, 0}
	case "right-up":
		return []float64{1, 0, 0}
	}
	return []float64{0, 0, 1} // flat (or not known): face up
}

// sinceBoot is the time since the system booted (CLOCK_BOOTTIME, as
// Android's sensor timestamps; suspended time counts).
func sinceBoot() time.Duration {
	var ts syscall.Timespec
	const clockBoottime = 7
	syscall.Syscall(syscall.SYS_CLOCK_GETTIME, clockBoottime, uintptr(unsafe.Pointer(&ts)), 0)
	return time.Duration(ts.Sec)*time.Second + time.Duration(ts.Nsec)
}
