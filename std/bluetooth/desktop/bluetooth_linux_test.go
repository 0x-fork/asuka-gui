//go:build linux && !android

package bluetooth

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os"
	"reflect"
	"sort"
	"sync"
	"testing"
	"time"

	spec "github.com/egoist/asuka/internal/bluetoothspec"
	"github.com/egoist/asuka/internal/bluez"
	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// The package's Adapter on Linux, through the bridge and the BlueZ
// engine, against a BlueZ of the test's own: org.bluez on the session
// bus (a connection of its own), with an adapter, a heart rate monitor
// to scan, connect to and read, and the calls BlueZ makes of the app's
// advertisement and GATT application. GLib's main loop is the main
// thread (native.Post's); run under dbus-run-session.

var (
	mainMu   sync.Mutex
	mainQ    []func()
	wakeMain func()
)

func TestMain(m *testing.M) {
	if err := Load(); err != nil {
		fmt.Println("skipping asuka/bluetooth's Linux tests:", err)
		os.Exit(0)
	}
	if _, err := Bus(BusSession); err != nil {
		fmt.Println("skipping asuka/bluetooth's Linux tests: no session bus (run under dbus-run-session)")
		os.Exit(0)
	}
	wakeMain = Waker(runMainQ)
	native.SetPost(func(f func()) {
		mainMu.Lock()
		mainQ = append(mainQ, f)
		mainMu.Unlock()
		wakeMain()
	})
	done := make(chan int, 1)
	go func() {
		code := m.Run()
		done <- code
		wakeMain()
	}()
	for {
		select {
		case code := <-done:
			os.Exit(code)
		default:
		}
		MainContextIteration(0, true)
	}
}

func runMainQ() {
	mainMu.Lock()
	q := mainQ
	mainQ = nil
	mainMu.Unlock()
	for _, f := range q {
		f()
	}
}

// onMain runs f on the main thread. A failure in f (fatal) ends f, not
// the main thread's loop.
func onMain(f func()) {
	native.OnMain(func() {
		defer func() {
			if r := recover(); r != nil && r != errFatal {
				panic(r)
			}
		}()
		f()
	})
}

var errFatal = new(int)

func fatalf(t *testing.T, format string, args ...any) {
	t.Helper()
	t.Errorf(format, args...)
	panic(errFatal)
}

func waitFor(t *testing.T, what string, cond func() bool) {
	t.Helper()
	for i := 0; i < 2000; i++ {
		var ok bool
		onMain(func() { ok = cond() })
		if ok {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("%s: never", what)
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

const fakeBluezXML = `<node>
 <interface name="org.bluez.Adapter1">
  <method name="SetDiscoveryFilter"><arg name="filter" type="a{sv}" direction="in"/></method>
  <method name="StartDiscovery"/>
  <method name="StopDiscovery"/>
 </interface>
 <interface name="org.bluez.Device1">
  <method name="Connect"/>
  <method name="Disconnect"/>
 </interface>
 <interface name="org.bluez.LEAdvertisingManager1">
  <method name="RegisterAdvertisement"><arg name="advertisement" type="o" direction="in"/><arg name="options" type="a{sv}" direction="in"/></method>
  <method name="UnregisterAdvertisement"><arg name="advertisement" type="o" direction="in"/></method>
 </interface>
 <interface name="org.bluez.GattManager1">
  <method name="RegisterApplication"><arg name="application" type="o" direction="in"/><arg name="options" type="a{sv}" direction="in"/></method>
  <method name="UnregisterApplication"><arg name="application" type="o" direction="in"/></method>
 </interface>
</node>`

type objPath string

// gv is a Go value as a GVariant (floating).
func gv(v any) Ptr {
	switch x := v.(type) {
	case string:
		return VStr(x)
	case objPath:
		return VPath(string(x))
	case bool:
		return VBool(x)
	case int16:
		return Call(Sym("g_variant_new_int16"), uintptr(int64(x)))
	case uint16:
		return VU16(x)
	case []string:
		return VStrings(x)
	case []byte:
		return VByteArray(x)
	case map[string]any:
		m := map[string]Ptr{}
		for k, v := range x {
			m[k] = gv(v)
		}
		return VDict(m)
	}
	panic(fmt.Sprintf("gv: %T", v))
}

const (
	fakeAdapter = "/org/bluez/hci0"
	fakeDevice  = fakeAdapter + "/dev_AA_BB_CC_DD_EE_FF"
	fakeService = fakeDevice + "/service000a"
	fakeChar    = fakeService + "/char000b"
	fakeDesc    = fakeChar + "/desc000d"
	fakeAddr    = "AA:BB:CC:DD:EE:FF"
)

type fakeBluez struct {
	conn    Ptr
	objects map[string]map[string]map[string]any
	calls   []string
	filter  map[string]any
	written []byte
	options map[string]any
	// The app's, as BlueZ read them.
	advert  map[string]any
	app     map[string]any
	appRead []byte
	appErr  error
	sender  string
}

func (f *fakeBluez) emit(path, iface, name string, params Ptr) {
	if err := DBusEmit(f.conn, path, iface, name, params); err != nil {
		panic(err)
	}
}

// set changes properties, as BlueZ tells.
func (f *fakeBluez) set(path, iface string, props map[string]any) {
	for k, v := range props {
		f.objects[path][iface][k] = v
	}
	f.emit(path, ifProperties, "PropertiesChanged", VTuple(VStr(iface), gv(props), VArray("s")))
}

// add adds an object, as BlueZ tells.
func (f *fakeBluez) add(path string, ifs map[string]map[string]any) {
	f.objects[path] = ifs
	f.emit("/", ifObjectManager, "InterfacesAdded", VTuple(VPath(path), f.ifaces(ifs)))
}

func (f *fakeBluez) ifaces(ifs map[string]map[string]any) Ptr {
	var names []string
	for n := range ifs {
		names = append(names, n)
	}
	sort.Strings(names)
	var items []Ptr
	for _, n := range names {
		items = append(items, VEntry(VStr(n), gv(ifs[n])))
	}
	return VArray("{sa{sv}}", items...)
}

func (f *fakeBluez) managed() Ptr {
	var paths []string
	for p := range f.objects {
		paths = append(paths, p)
	}
	sort.Strings(paths)
	var items []Ptr
	for _, p := range paths {
		items = append(items, VEntry(VPath(p), f.ifaces(f.objects[p])))
	}
	return VTuple(VArray("{oa{sa{sv}}}", items...))
}

func (f *fakeBluez) method(c DBusInvocation) {
	f.calls = append(f.calls, c.Method)
	args, _ := VariantGoBytes(c.Params).([]any)
	switch c.Method {
	case "GetManagedObjects":
		c.Return(f.managed())
		return
	case "SetDiscoveryFilter":
		f.filter, _ = args[0].(map[string]any)
	case "StartDiscovery":
		f.set(fakeAdapter, ifAdapter, map[string]any{"Discovering": true})
	case "StopDiscovery":
		f.set(fakeAdapter, ifAdapter, map[string]any{"Discovering": false})
	case "Connect":
		f.set(fakeDevice, ifDevice, map[string]any{"Connected": true})
		f.add(fakeService, map[string]map[string]any{ifGattService: {"UUID": spec.Normalize("180d"), "Primary": true, "Device": objPath(fakeDevice)}})
		f.add(fakeChar, map[string]map[string]any{ifGattChar: {"UUID": spec.Normalize("2a37"), "Service": objPath(fakeService),
			"Flags": []string{"read", "write", "notify"}, "MTU": uint16(185)}})
		f.add(fakeDesc, map[string]map[string]any{ifGattDesc: {"UUID": spec.Normalize("2902"), "Characteristic": objPath(fakeChar)}})
		f.set(fakeDevice, ifDevice, map[string]any{"ServicesResolved": true})
	case "ReadValue":
		c.Return(VTuple(VByteArray([]byte{1, 2, 3})))
		return
	case "WriteValue":
		f.written = bluez.Bytes(args[0])
		f.options, _ = args[1].(map[string]any)
	case "RegisterAdvertisement":
		path, _ := args[0].(string)
		DBusCall(f.conn, c.Sender, path, ifProperties, "GetAll", VTuple(VStr(ifAdvert)), 5000, func(r Ptr, err error) {
			if err != nil {
				c.Error("org.bluez.Error.Failed", err.Error())
				return
			}
			a, _ := VariantGoBytes(r).([]any)
			f.advert, _ = a[0].(map[string]any)
			c.Return(0)
		})
		return
	case "RegisterApplication":
		root, _ := args[0].(string)
		f.sender = c.Sender
		DBusCall(f.conn, c.Sender, root, ifObjectManager, "GetManagedObjects", 0, 5000, func(r Ptr, err error) {
			if err != nil {
				c.Error("org.bluez.Error.Failed", err.Error())
				return
			}
			a, _ := VariantGoBytes(r).([]any)
			f.app, _ = a[0].(map[string]any)
			c.Return(0)
		})
		return
	}
	c.Return(0)
}

// startFakeBluez exports the fake on a connection of its own to the
// session bus, owning org.bluez, and points the module at that bus.
func startFakeBluez(t *testing.T) *fakeBluez {
	f := &fakeBluez{objects: map[string]map[string]map[string]any{
		fakeAdapter: {
			ifAdapter:     {"Address": "00:11:22:33:44:55", "Powered": true, "Discovering": false},
			ifAdvManager:  {},
			ifGattManager: {},
		},
		fakeDevice: {ifDevice: {"Address": fakeAddr, "Adapter": objPath(fakeAdapter), "Name": "Polar H10", "Alias": "Polar H10",
			"Connected": false, "ServicesResolved": false, "UUIDs": []string{spec.Normalize("180d")}}},
	}}
	var owned bool
	onMain(func() {
		conn, err := privateSessionBus()
		if err != nil {
			fatalf(t, "session bus: %v", err)
		}
		f.conn = conn
		reg := func(path, xml, iface string) {
			if _, err := DBusRegister(conn, path, xml, iface, DBusObject{Method: f.method}); err != nil {
				fatalf(t, "export %s: %v", iface, err)
			}
		}
		reg("/", btObjectsXML, ifObjectManager)
		reg(fakeAdapter, fakeBluezXML, ifAdapter)
		reg(fakeAdapter, fakeBluezXML, ifAdvManager)
		reg(fakeAdapter, fakeBluezXML, ifGattManager)
		reg(fakeDevice, fakeBluezXML, ifDevice)
		reg(fakeChar, btObjectsXML, ifGattChar)
		DBusCall(conn, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
			VTuple(VStr(bluezName), VU32(4)), 5000, func(_ Ptr, err error) {
				if err != nil {
					t.Errorf("RequestName: %v", err)
				}
				owned = true
			})
		btBus = func() (Ptr, error) { return Bus(BusSession) }
		b := &blue
		for _, id := range b.subs {
			DBusUnsubscribe(b.conn, id)
		}
		blue = bluetooth{}
		bt()
		theEngine = nil
		br.pumping = false
	})
	waitFor(t, "org.bluez owned", func() bool { return owned })
	t.Cleanup(func() {
		onMain(func() {
			for _, id := range blue.subs {
				DBusUnsubscribe(blue.conn, id)
			}
			blue = bluetooth{}
			theEngine = nil
			br.pumping = false
			btBus = func() (Ptr, error) { return Bus(BusSystem) }
			closeBus(f.conn)
		})
	})
	return f
}

func testContext(t *testing.T) context.Context {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	t.Cleanup(cancel)
	return ctx
}

// recv waits for what the bridge hands over (the timeout is a safety
// net, not what passes).
func recv[T any](t *testing.T, ch <-chan T, what string) T {
	t.Helper()
	select {
	case v := <-ch:
		return v
	case <-time.After(20 * time.Second):
		t.Fatalf("%s: never", what)
	}
	var zero T
	return zero
}

func called(f *fakeBluez, method string) func() bool {
	return func() bool {
		for _, c := range f.calls {
			if c == method {
				return true
			}
		}
		return false
	}
}

func TestCentral(t *testing.T) {
	f := startFakeBluez(t)
	var d desktop
	ctx := testContext(t)

	if s, err := d.Request(ctx); err != nil || s != StateOn {
		t.Fatalf("request: %v %v", s, err)
	}
	states := make(chan State, 16)
	wctx, stopWatching := context.WithCancel(ctx)
	defer stopWatching()
	go d.WatchState(wctx, func(s State) { states <- s })
	if s := recv(t, states, "the state"); s != StateOn {
		t.Errorf("watched state %v", s)
	}

	// A scan of a service: discovery filtered and started; the device
	// heard as its RSSI comes, in the package's words.
	found := make(chan Advertisement, 16)
	sctx, stopScan := context.WithCancel(ctx)
	scanned := make(chan error, 1)
	go func() {
		scanned <- d.Scan(sctx, ScanOptions{Services: []string{"180d"}}, func(a Advertisement) { found <- a })
	}()
	waitFor(t, "StartDiscovery", called(f, "StartDiscovery"))
	onMain(func() {
		if !reflect.DeepEqual(f.filter["UUIDs"], []any{spec.Normalize("180d")}) {
			t.Errorf("discovery filter %v", f.filter)
		}
		f.set(fakeDevice, ifDevice, map[string]any{"RSSI": int16(-60)})
	})
	a := recv(t, found, "an advertisement")
	if a.Device.Id != fakeAddr || a.Device.Name != "Polar H10" || a.Rssi != -60 || a.LocalName != "Polar H10" ||
		!reflect.DeepEqual(a.Services, []string{"180d"}) || !a.Connectable || a.Time == 0 {
		t.Errorf("advertisement %+v", a)
	}
	stopScan()
	if err := recv(t, scanned, "the scan's end"); !errors.Is(err, context.Canceled) {
		t.Errorf("scan ended with %v", err)
	}
	waitFor(t, "StopDiscovery", func() bool { return f.calls[len(f.calls)-1] == "StopDiscovery" })

	// A connection: connecting, connected with its handle.
	links := make(chan Link, 16)
	cctx, disconnect := context.WithCancel(ctx)
	defer disconnect()
	ended := make(chan error, 1)
	go func() {
		ended <- d.Connect(cctx, Device{Id: fakeAddr}, ConnectOptions{Timeout: 10 * time.Second}, func(l Link) { links <- l })
	}()
	if l := recv(t, links, "connecting"); l.State != ConnectionStateConnecting {
		t.Errorf("first link %+v", l)
	}
	l := recv(t, links, "connected")
	if l.State != ConnectionStateConnected || l.Handle == 0 {
		t.Fatalf("link %+v", l)
	}
	h := l.Handle

	// Its services, a read, a write, the MTU, a notification.
	svcs, err := d.Services(ctx, h)
	want := []FoundService{{Uuid: "180d", Characteristics: []FoundCharacteristic{{Uuid: "2a37",
		Properties: []Property{PropertyRead, PropertyWrite, PropertyNotify}, Descriptors: []string{"2902"}}}}}
	if err != nil || !reflect.DeepEqual(svcs, want) {
		t.Errorf("services %+v %v", svcs, err)
	}
	if v, err := d.Read(ctx, h, "180d", "2a37"); err != nil || !bytes.Equal(v, []byte{1, 2, 3}) {
		t.Errorf("read %v %v", v, err)
	}
	if _, err := d.Read(ctx, h, "180d", "2a99"); !errors.Is(err, ErrNotFound) {
		t.Errorf("read of no characteristic: %v", err)
	}
	if err := d.Write(ctx, h, "180d", "2a37", []byte{7, 0}, true); err != nil {
		t.Errorf("write: %v", err)
	}
	onMain(func() {
		if !bytes.Equal(f.written, []byte{7, 0}) || f.options["type"] != "request" {
			t.Errorf("written %v %v", f.written, f.options)
		}
	})
	if m, err := d.Mtu(ctx, h); err != nil || m != 185 {
		t.Errorf("mtu %d %v", m, err)
	}
	values := make(chan []byte, 16)
	subscribed := make(chan error, 1)
	go func() {
		subscribed <- d.Subscribe(ctx, h, "180d", "2a37", func(v []byte) { values <- v })
	}()
	waitFor(t, "StartNotify", called(f, "StartNotify"))
	onMain(func() { f.set(fakeChar, ifGattChar, map[string]any{"Value": []byte{9, 8}}) })
	if v := recv(t, values, "a notification"); !bytes.Equal(v, []byte{9, 8}) {
		t.Errorf("notified %v", v)
	}

	// The device goes: the link says why, the subscription and the calls
	// end disconnected.
	onMain(func() {
		f.emit(fakeDevice, ifDevice, "Disconnected", VTuple(VStr("org.bluez.Reason.Remote"), VStr("Connection terminated by remote user")))
		f.set(fakeDevice, ifDevice, map[string]any{"Connected": false, "ServicesResolved": false})
	})
	if l := recv(t, links, "disconnected"); l.State != ConnectionStateDisconnected || l.Error != "bluetooth: Connection terminated by remote user" {
		t.Errorf("lost link %+v", l)
	}
	if err := recv(t, ended, "connect's return"); err != nil {
		t.Errorf("connect returned %v", err)
	}
	if err := recv(t, subscribed, "subscribe's return"); !errors.Is(err, ErrDisconnected) {
		t.Errorf("subscribe returned %v", err)
	}
	if _, err := d.Read(ctx, h, "180d", "2a37"); !errors.Is(err, ErrDisconnected) {
		t.Errorf("read after: %v", err)
	}

	// The adapter off: followed, and scans fail with it.
	onMain(func() { f.set(fakeAdapter, ifAdapter, map[string]any{"Powered": false}) })
	for s := recv(t, states, "off"); s != StateOff; s = recv(t, states, "off") {
	}
	if s, _ := d.State(); s != StateOff {
		t.Errorf("state %v", s)
	}
	if err := d.Scan(ctx, ScanOptions{}, func(Advertisement) {}); !errors.Is(err, ErrOff) {
		t.Errorf("scan while off: %v", err)
	}
	if _, err := d.Choose(ctx, nil, nil); !errors.Is(err, ErrNotAvailable) {
		t.Errorf("choose: %v", err)
	}
}

func TestPeripheral(t *testing.T) {
	f := startFakeBluez(t)
	var d desktop
	ctx := testContext(t)

	// An advertisement: BlueZ reads its properties.
	actx, stopAdvertising := context.WithCancel(ctx)
	defer stopAdvertising()
	advertised := make(chan error, 1)
	go func() {
		advertised <- d.Advertise(actx, Advertising{LocalName: "asuka", Services: []string{"180d"},
			ManufacturerData: map[string][]byte{"004c": {1, 2}}, Connectable: true, IncludeTxPower: true})
	}()
	waitFor(t, "RegisterAdvertisement", func() bool { return f.advert != nil })
	onMain(func() {
		a := f.advert
		if a["LocalName"] != "asuka" || !reflect.DeepEqual(a["ServiceUUIDs"], []any{"180d"}) ||
			!reflect.DeepEqual(a["ManufacturerData"], map[string]any{"76": []byte{1, 2}}) || a["Discoverable"] != true {
			t.Errorf("advertisement as BlueZ reads it: %v", a)
		}
	})

	// A server: BlueZ reads its objects; a central's read, at an offset,
	// is the package's request, answered from the offset on.
	requests := make(chan Request, 16)
	sctx, stopServing := context.WithCancel(ctx)
	served := make(chan error, 1)
	go func() {
		served <- d.Serve(sctx, []ServedService{{Uuid: "180d", Primary: true, Characteristics: []ServedCharacteristic{
			{Uuid: "2a37", Properties: []Property{PropertyRead, PropertyNotify}, Permissions: []Permission{PermissionReadable}, Dynamic: true},
			{Uuid: "2a38", Properties: []Property{PropertyWrite}, Permissions: []Permission{PermissionWriteEncrypted}},
		}}}, func(r Request) { requests <- r })
	}()
	waitFor(t, "RegisterApplication", func() bool { return f.app != nil })
	var char7, char8 string
	onMain(func() {
		for path, v := range f.app {
			ifs, _ := v.(map[string]any)
			if c, ok := ifs[ifGattChar].(map[string]any); ok {
				switch c["UUID"] {
				case spec.Normalize("2a37"):
					char7 = path
				case spec.Normalize("2a38"):
					char8 = path
					if !reflect.DeepEqual(c["Flags"], []any{"encrypt-write"}) {
						t.Errorf("flags %v", c["Flags"])
					}
				}
			}
		}
	})
	if char7 == "" || char8 == "" {
		t.Fatalf("the application as BlueZ reads it: %v", f.app)
	}
	var answered bool
	onMain(func() {
		opts := VDict(map[string]Ptr{"device": VPath(fakeDevice), "offset": VU16(1), "mtu": VU16(185)})
		DBusCall(f.conn, f.sender, char7, ifGattChar, "ReadValue", VTuple(opts), 5000, func(r Ptr, err error) {
			answered = true
			if err != nil {
				f.appErr = err
				return
			}
			v := Sys.VariantGetChildValue(r, 0)
			f.appRead = VariantBytes(v)
			Sys.VariantUnref(v)
		})
	})
	r := recv(t, requests, "a read")
	if r.Kind != RequestKindRead || r.Central != fakeAddr || r.Service != "180d" || r.Characteristic != "2a37" || r.Offset != 1 {
		t.Errorf("read request %+v", r)
	}
	d.Respond(r.Id, []byte{4, 5, 6}, 0)
	waitFor(t, "ReadValue answered", func() bool { return answered })
	onMain(func() {
		if f.appErr != nil || !bytes.Equal(f.appRead, []byte{5, 6}) {
			t.Errorf("the central read %v, %v", f.appRead, f.appErr)
		}
	})

	// A write the app refuses: the ATT error BlueZ sends.
	answered = false
	onMain(func() {
		opts := VDict(map[string]Ptr{"device": VPath(fakeDevice)})
		DBusCall(f.conn, f.sender, char8, ifGattChar, "WriteValue", VTuple(VByteArray([]byte{1}), opts), 5000, func(_ Ptr, err error) {
			answered = true
			f.appErr = err
		})
	})
	r = recv(t, requests, "a write")
	if r.Kind != RequestKindWrite || r.Characteristic != "2a38" || !bytes.Equal(r.Value, []byte{1}) {
		t.Errorf("write request %+v", r)
	}
	d.Respond(r.Id, nil, 0x08)
	waitFor(t, "WriteValue answered", func() bool { return answered })
	onMain(func() {
		if name, _ := bluez.Error(f.appErr); name != "org.bluez.Error.NotAuthorized" {
			t.Errorf("refused write: %v", f.appErr)
		}
	})

	// A subscription, then a notification to the one subscribed.
	var notified []byte
	onMain(func() {
		DBusSubscribe(f.conn, f.sender, ifProperties, "PropertiesChanged", char7, func(s DBusSignal) {
			a, _ := VariantGoBytes(s.Params).([]any)
			m, _ := a[1].(map[string]any)
			notified = bluez.Bytes(m["Value"])
		})
		DBusCall(f.conn, f.sender, char7, ifGattChar, "StartNotify", 0, 5000, func(Ptr, error) {})
	})
	r = recv(t, requests, "a subscription")
	if r.Kind != RequestKindSubscribe || r.Characteristic != "2a37" || r.Central == "" {
		t.Errorf("subscription %+v", r)
	}
	if n, err := d.Notify("180d", "2a37", []byte{42}); err != nil || n != 1 {
		t.Errorf("notify: %d %v", n, err)
	}
	waitFor(t, "the notification", func() bool { return bytes.Equal(notified, []byte{42}) })

	stopServing()
	if err := recv(t, served, "serve's return"); !errors.Is(err, context.Canceled) {
		t.Errorf("serve returned %v", err)
	}
	waitFor(t, "UnregisterApplication", func() bool { return f.calls[len(f.calls)-1] == "UnregisterApplication" })

	// BlueZ releases the advertisement: advertise ends with it.
	onMain(func() {
		for _, ad := range blue.adverts {
			DBusCall(f.conn, f.sender, ad.path, ifAdvert, "Release", 0, 5000, func(Ptr, error) {})
		}
	})
	if err := recv(t, advertised, "advertise's return"); err == nil || errors.Is(err, context.Canceled) {
		t.Errorf("advertise returned %v", err)
	}
}

func TestNoBluez(t *testing.T) {
	onMain(func() {
		btBus = func() (Ptr, error) { return Bus(BusSession) } // where no org.bluez is
		blue = bluetooth{}
		theEngine = nil
		br.pumping = false
	})
	defer onMain(func() {
		for _, id := range blue.subs {
			DBusUnsubscribe(blue.conn, id)
		}
		blue = bluetooth{}
		theEngine = nil
		br.pumping = false
		btBus = func() (Ptr, error) { return Bus(BusSystem) }
	})
	var d desktop
	ctx := testContext(t)
	if s, err := d.Request(ctx); err != nil || s != StateUnsupported {
		t.Errorf("without BlueZ: %v %v", s, err)
	}
	if err := d.Scan(ctx, ScanOptions{}, func(Advertisement) {}); !errors.Is(err, ErrUnsupported) {
		t.Errorf("scan without BlueZ: %v", err)
	}
	if err := d.Advertise(ctx, Advertising{}); !errors.Is(err, ErrUnsupported) {
		t.Errorf("advertise without BlueZ: %v", err)
	}
}
