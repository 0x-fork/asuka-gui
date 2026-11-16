//go:build windows || (linux && !android)

// asuka/bluetooth on Windows and Linux (macOS runs the package's Swift).
// The platform's work is an engine's (engine_windows.go: WinRT's
// Windows.Devices.Bluetooth; engine_linux.go: BlueZ over D-Bus), which
// speaks internal/bluetoothspec: methods that answer once, and events
// ("next") for what comes later. This is the package's Adapter on it:
// each call is the engine's method, run on the main thread; the events
// go to the calls waiting for them (a scan's advertisements, a
// connection's loss and its notifications, a server's requests). The
// engine names attributes by handles: a connection's are looked up by
// their service and characteristic, a server's are this file's.
package bluetooth

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
	"strings"

	spec "github.com/egoist/asuka/internal/bluetoothspec"
	"github.com/egoist/asuka/native"
)

type desktop struct{}

func newAdapter() (Adapter, error) { return desktop{}, nil }

// The bridge's state, on the main thread.
var br struct {
	pumping  bool
	state    State // the last known
	next     uint32
	states   map[uint32]func(State)
	scans    map[uint32]*scanSink
	conns    map[uint32]*connSink
	servers  map[uint32]*serverSink
	adverts  map[uint32]chan error
	requests map[int]pendingRequest
	nextReq  int
}

type scanSink struct {
	found   func(Advertisement)
	stopped chan error
}

type connSink struct {
	changed func(Link)
	gone    chan struct{} // closed once lost
	why     string
	handles map[string]uint32 // "service characteristic" → handle (none: not discovered)
	values  map[uint32]func([]byte)
}

type serverSink struct {
	request func(Request)
	stopped chan error
	attrs   map[uint32][2]string       // handle → service, characteristic (the package's UUIDs)
	subs    map[uint32]map[string]bool // handle → the centrals subscribed
}

type pendingRequest struct {
	server, request uint32
	offset          int
	read            bool
}

func id() uint32 {
	br.next++
	return br.next
}

// ---------------------------------------------------------------------
// The engine: calls and events

// call runs the engine's method with args (JSON) on the main thread, and
// waits for its answer.
func call(ctx context.Context, method string, args any) ([]byte, error) {
	var in []byte
	if args != nil {
		in, _ = json.Marshal(args)
	}
	type answer struct {
		b   []byte
		err error
	}
	ch := make(chan answer, 1)
	native.Post(func() {
		pump()
		engine()[method](in, func(b []byte, err error) {
			select {
			case ch <- answer{b, err}:
			default:
			}
		})
	})
	select {
	case a := <-ch:
		if a.err != nil {
			return nil, wireError(a.err.Error())
		}
		return a.b, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// send runs the engine's method without waiting; main thread or not.
func send(method string, args any) {
	in, _ := json.Marshal(args)
	native.Post(func() { engine()[method](in, func([]byte, error) {}) })
}

func callJSON(ctx context.Context, method string, args, out any) error {
	b, err := call(ctx, method, args)
	if err != nil {
		return err
	}
	return json.Unmarshal(b, out)
}

// pump follows the engine's events, from the first call on; main thread.
func pump() {
	if br.pumping {
		return
	}
	br.pumping = true
	var next func()
	next = func() {
		engine()["next"](nil, func(b []byte, err error) {
			if err != nil {
				br.pumping = false
				return
			}
			var es []spec.Event
			if json.Unmarshal(b, &es) == nil {
				for _, e := range es {
					dispatch(e)
				}
			}
			next()
		})
	}
	next()
}

// dispatch hands an event to whoever waits for it; main thread.
func dispatch(e spec.Event) {
	switch e.Type {
	case spec.EventState:
		s := wireState(e.State)
		br.state = s
		for _, f := range br.states {
			f(s)
		}
	case spec.EventAdvertisement:
		if s := br.scans[e.Scan]; s != nil && e.Advertisement != nil {
			s.found(advertisement(*e.Advertisement))
		}
	case spec.EventScanStopped:
		if s := br.scans[e.Scan]; s != nil {
			trySend(s.stopped, failure(e.Error))
		}
	case spec.EventDisconnected:
		if c := br.conns[e.Conn]; c != nil {
			lose(c, disconnection(e))
		}
	case spec.EventValue:
		if c := br.conns[e.Conn]; c != nil {
			if f := c.values[e.Handle]; f != nil {
				f(e.Value)
			}
		}
	case spec.EventServicesChanged:
		if c := br.conns[e.Conn]; c != nil {
			c.handles = nil // discovered again as they're next used
		}
	case spec.EventRead, spec.EventWrite:
		s := br.servers[e.Server]
		if s == nil {
			return
		}
		at, ok := s.attrs[e.Handle]
		if !ok {
			if e.Type == spec.EventRead || e.Response {
				send("respond", spec.Respond{Server: e.Server, Request: e.Request, Error: 0x0a})
			}
			return
		}
		br.nextReq++
		r := Request{Id: br.nextReq, Kind: RequestKindRead, Service: at[0], Characteristic: at[1], Value: e.Value, Offset: e.Offset}
		if e.Central != nil {
			r.Central = e.Central.ID
		}
		if e.Type == spec.EventWrite {
			r.Kind = RequestKindWrite
		}
		// A write without response is answered by no one.
		if e.Type == spec.EventRead || e.Response {
			br.requests[r.Id] = pendingRequest{server: e.Server, request: e.Request, offset: e.Offset, read: e.Type == spec.EventRead}
		}
		s.request(r)
	case spec.EventSubscription:
		s := br.servers[e.Server]
		if s == nil {
			return
		}
		at, ok := s.attrs[e.Handle]
		if !ok || e.Central == nil {
			return
		}
		if s.subs[e.Handle] == nil {
			s.subs[e.Handle] = map[string]bool{}
		}
		kind := RequestKindSubscribe
		if e.On {
			s.subs[e.Handle][e.Central.ID] = true
		} else {
			delete(s.subs[e.Handle], e.Central.ID)
			kind = RequestKindUnsubscribe
		}
		br.nextReq++
		s.request(Request{Id: br.nextReq, Kind: kind, Central: e.Central.ID, Service: at[0], Characteristic: at[1]})
	case spec.EventAdvertiseStopped:
		if ch := br.adverts[e.ID]; ch != nil {
			trySend(ch, failure(e.Error))
		}
	case spec.EventServerStopped:
		if s := br.servers[e.Server]; s != nil {
			trySend(s.stopped, failure(e.Error))
		}
	}
}

func trySend(ch chan error, err error) {
	select {
	case ch <- err:
	default:
	}
}

func lose(c *connSink, why string) {
	select {
	case <-c.gone:
	default:
		c.why = why
		close(c.gone)
	}
}

func disconnection(e spec.Event) string {
	if e.Reason == spec.ReasonOff {
		return "bluetooth: off"
	}
	if e.Error != "" {
		return "bluetooth: " + e.Error
	}
	return "bluetooth: disconnected (" + e.Reason + ")"
}

// ---------------------------------------------------------------------
// The wire's words and the package's

func wireState(s string) State {
	switch s {
	case spec.StateOn:
		return StateOn
	case spec.StateOff:
		return StateOff
	case spec.StateDenied:
		return StateUnauthorized
	case spec.StateUnsupported:
		return StateUnsupported
	}
	return StateUnknown // unknown, notAsked (request asks)
}

// wireError is an engine's error as the package's.
func wireError(s string) error {
	switch s {
	case spec.StateOff:
		return ErrOff
	case spec.StateDenied:
		return ErrDenied
	case spec.StateUnsupported:
		return ErrUnsupported
	case "canceled":
		return ErrCanceled
	case "disconnected":
		return ErrDisconnected
	case "not found":
		return ErrNotFound
	case "timeout":
		return ErrTimeout
	}
	if strings.HasPrefix(s, "not available") {
		return ErrNotAvailable
	}
	return errors.New("bluetooth: " + s)
}

func failure(s string) error {
	if s == "" {
		return nil
	}
	return wireError(s)
}

const sigBase = "-0000-1000-8000-00805f9b34fb"

// short is a wire UUID as the package writes it: a SIG one's four hex
// digits, any other's 128 bits.
func short(u string) string {
	u = strings.ToLower(u)
	if len(u) == 36 && strings.HasPrefix(u, "0000") && strings.HasSuffix(u, sigBase) {
		return u[4:8]
	}
	return u
}

// full is a package UUID as the wire has it.
func full(u string) string { return spec.Normalize(u) }

func shorts(us []string) []string {
	out := make([]string, len(us))
	for i, u := range us {
		out[i] = short(u)
	}
	return out
}

func fulls(us []string) []string {
	out := make([]string, len(us))
	for i, u := range us {
		out[i] = full(u)
	}
	return out
}

// The wire names companies in decimal, the package in four hex digits.
func companyHex(m map[string][]byte) map[string][]byte {
	out := map[string][]byte{}
	for k, v := range m {
		if n, err := strconv.Atoi(k); err == nil {
			k = fmt.Sprintf("%04x", n)
		}
		out[k] = v
	}
	return out
}

func companyDecimal(m map[string][]byte) map[string][]byte {
	out := map[string][]byte{}
	for k, v := range m {
		if n, err := strconv.ParseUint(k, 16, 16); err == nil {
			k = strconv.FormatUint(n, 10)
		}
		out[k] = v
	}
	return out
}

func serviceData(m map[string][]byte, f func(string) string) map[string][]byte {
	out := map[string][]byte{}
	for k, v := range m {
		out[f(k)] = v
	}
	return out
}

func advertisement(a spec.Advertisement) Advertisement {
	out := Advertisement{
		Device:           Device{Id: a.Device.ID, Name: a.Device.Name},
		Rssi:             a.RSSI,
		LocalName:        a.LocalName,
		Services:         shorts(a.Services),
		Solicited:        shorts(a.Solicited),
		ServiceData:      serviceData(a.ServiceData, short),
		ManufacturerData: companyHex(a.ManufacturerData),
		Connectable:      a.Connectable == nil || *a.Connectable,
		Time:             int(a.Time) * 1_000_000,
	}
	if a.TxPower != nil {
		out.TxPower, out.HasTxPower = *a.TxPower, true
	}
	return out
}

var propertyBits = []struct {
	p   Property
	bit int
}{
	{PropertyBroadcast, spec.PropBroadcast}, {PropertyRead, spec.PropRead},
	{PropertyWriteWithoutResponse, spec.PropWriteNoResponse}, {PropertyWrite, spec.PropWrite},
	{PropertyNotify, spec.PropNotify}, {PropertyIndicate, spec.PropIndicate},
	{PropertyAuthenticatedSignedWrites, spec.PropSignedWrite}, {PropertyExtendedProperties, spec.PropExtendedProperties},
}

func properties(bits int) []Property {
	var out []Property
	for _, x := range propertyBits {
		if bits&x.bit != 0 {
			out = append(out, x.p)
		}
	}
	return out
}

func propertyMask(ps []Property) int {
	m := 0
	for _, p := range ps {
		for _, x := range propertyBits {
			if x.p == p {
				m |= x.bit
			}
		}
	}
	return m
}

func permissionMask(ps []Permission) int {
	m := 0
	for _, p := range ps {
		switch p {
		case PermissionReadable:
			m |= spec.PermRead
		case PermissionWriteable:
			m |= spec.PermWrite
		case PermissionReadEncrypted:
			m |= spec.PermReadEncrypted
		case PermissionWriteEncrypted:
			m |= spec.PermWriteEncrypted
		}
	}
	return m
}

// ---------------------------------------------------------------------
// The adapter

// State is synchronous: the state last known, asked for again (the
// engine may answer at once; else watchState and the events follow it).
func (desktop) State() (State, error) {
	var s State
	native.OnMain(func() {
		pump()
		engine()["state"](nil, func(b []byte, err error) {
			var st spec.State
			if err == nil && json.Unmarshal(b, &st) == nil {
				br.state = wireState(st.State)
			}
		})
		s = br.state
	})
	return s, nil
}

func (d desktop) WatchState(ctx context.Context, changed func(State)) error {
	var key uint32
	native.OnMain(func() {
		if br.states == nil {
			br.states = map[uint32]func(State){}
		}
		key = id()
		br.states[key] = changed
	})
	defer native.Post(func() { delete(br.states, key) })
	var st spec.State
	if err := callJSON(ctx, "state", nil, &st); err == nil {
		native.OnMain(func() { changed(wireState(st.State)) })
	}
	<-ctx.Done()
	return ctx.Err()
}

func (desktop) Request(ctx context.Context) (State, error) {
	var st spec.State
	if err := callJSON(ctx, "request", nil, &st); err != nil {
		return StateUnsupported, err
	}
	return wireState(st.State), nil
}

func (desktop) Scan(ctx context.Context, o ScanOptions, found func(Advertisement)) error {
	var key uint32
	sink := &scanSink{found: found, stopped: make(chan error, 1)}
	native.OnMain(func() {
		if br.scans == nil {
			br.scans = map[uint32]*scanSink{}
		}
		key = id()
		br.scans[key] = sink
	})
	defer native.Post(func() { delete(br.scans, key) })
	if _, err := call(ctx, "scan", spec.ScanArgs{ID: key, Services: fulls(o.Services), Duplicates: o.Duplicates}); err != nil {
		send("stopScan", spec.ID{ID: key})
		return err
	}
	select {
	case err := <-sink.stopped:
		if err == nil {
			err = ErrOff
		}
		return err
	case <-ctx.Done():
		send("stopScan", spec.ID{ID: key})
		return ctx.Err()
	}
}

func (desktop) Choose(ctx context.Context, filters []Filter, optionalServices []string) (Device, error) {
	a := spec.ChooseArgs{OptionalServices: fulls(optionalServices)}
	for _, f := range filters {
		a.Filters = append(a.Filters, spec.Filter{Services: fulls(f.Services), Name: f.Name, NamePrefix: f.NamePrefix})
	}
	var d spec.Device
	if err := callJSON(ctx, "choose", a, &d); err != nil {
		return Device{}, err
	}
	return Device{Id: d.ID, Name: d.Name}, nil
}

func (desktop) Connect(ctx context.Context, device Device, o ConnectOptions, changed func(Link)) error {
	var key uint32
	c := &connSink{changed: changed, gone: make(chan struct{}), values: map[uint32]func([]byte){}}
	native.OnMain(func() {
		if br.conns == nil {
			br.conns = map[uint32]*connSink{}
		}
		key = id()
		br.conns[key] = c
		changed(Link{State: ConnectionStateConnecting})
	})
	defer native.Post(func() { delete(br.conns, key) })
	cctx := ctx
	if o.Timeout > 0 && !o.AutoConnect {
		var cancel context.CancelFunc
		cctx, cancel = context.WithTimeout(ctx, o.Timeout)
		defer cancel()
	}
	if _, err := call(cctx, "connect", spec.ConnectArgs{Conn: key, Device: device.Id, AutoConnect: o.AutoConnect}); err != nil {
		if cctx.Err() != nil {
			send("disconnect", spec.Conn{Conn: key}) // ends the pending connect
			if ctx.Err() == nil {
				return ErrTimeout
			}
			return ctx.Err()
		}
		return err
	}
	native.OnMain(func() { changed(Link{State: ConnectionStateConnected, Handle: int(key)}) })
	select {
	case <-c.gone:
		native.OnMain(func() { changed(Link{State: ConnectionStateDisconnected, Error: c.why}) })
		return nil
	case <-ctx.Done():
		send("disconnect", spec.Conn{Conn: key})
		return ctx.Err()
	}
}

// conn is a connection still up; main thread.
func conn(h int) (*connSink, error) {
	c := br.conns[uint32(h)]
	if c == nil {
		return nil, ErrDisconnected
	}
	select {
	case <-c.gone:
		return nil, ErrDisconnected
	default:
		return c, nil
	}
}

func (desktop) Services(ctx context.Context, h int) ([]FoundService, error) {
	var tree []spec.Service
	if err := callJSON(ctx, "services", spec.Conn{Conn: uint32(h)}, &tree); err != nil {
		return nil, err
	}
	var out []FoundService
	native.OnMain(func() {
		handles := map[string]uint32{}
		for _, s := range tree {
			fs := FoundService{Uuid: short(s.UUID)}
			for _, ch := range s.Characteristics {
				fc := FoundCharacteristic{Uuid: short(ch.UUID), Properties: properties(ch.Properties)}
				for _, d := range ch.Descriptors {
					fc.Descriptors = append(fc.Descriptors, short(d.UUID))
				}
				fs.Characteristics = append(fs.Characteristics, fc)
				if _, ok := handles[fs.Uuid+" "+fc.Uuid]; !ok {
					handles[fs.Uuid+" "+fc.Uuid] = ch.Handle
				}
			}
			out = append(out, fs)
		}
		if c, err := conn(h); err == nil {
			c.handles = handles
		}
	})
	return out, nil
}

// handle is a characteristic's on a connection, discovering the
// services first when they aren't.
func (d desktop) handle(ctx context.Context, h int, service, char string) (uint32, error) {
	key := short(service) + " " + short(char)
	look := func() (uint32, bool, error) {
		var at uint32
		var known bool
		var err error
		native.OnMain(func() {
			var c *connSink
			if c, err = conn(h); err == nil && c.handles != nil {
				known = true
				at = c.handles[key]
			}
		})
		return at, known, err
	}
	at, known, err := look()
	if err != nil {
		return 0, err
	}
	if !known {
		if _, err := d.Services(ctx, h); err != nil {
			return 0, err
		}
		if at, _, err = look(); err != nil {
			return 0, err
		}
	}
	if at == 0 {
		return 0, ErrNotFound
	}
	return at, nil
}

func (desktop) Mtu(ctx context.Context, h int) (int, error) {
	var m spec.MTU
	if err := callJSON(ctx, "mtu", spec.Conn{Conn: uint32(h)}, &m); err != nil {
		return 0, err
	}
	return m.MTU, nil
}

func (desktop) Rssi(ctx context.Context, h int) (int, error) {
	var r spec.RSSI
	if err := callJSON(ctx, "rssi", spec.Conn{Conn: uint32(h)}, &r); err != nil {
		return 0, err
	}
	return r.RSSI, nil
}

func (d desktop) Read(ctx context.Context, h int, service, char string) ([]byte, error) {
	at, err := d.handle(ctx, h, service, char)
	if err != nil {
		return nil, err
	}
	return call(ctx, "read", spec.Attr{Conn: uint32(h), Handle: at})
}

func (d desktop) Write(ctx context.Context, h int, service, char string, value []byte, response bool) error {
	at, err := d.handle(ctx, h, service, char)
	if err != nil {
		return err
	}
	_, err = call(ctx, "write", spec.Write{Conn: uint32(h), Handle: at, Value: value, Response: response})
	return err
}

func (d desktop) Subscribe(ctx context.Context, h int, service, char string, value func([]byte)) error {
	at, err := d.handle(ctx, h, service, char)
	if err != nil {
		return err
	}
	var c *connSink
	native.OnMain(func() {
		if c, err = conn(h); err == nil {
			c.values[at] = value
		}
	})
	if err != nil {
		return err
	}
	defer native.Post(func() { delete(c.values, at) })
	if _, err := call(ctx, "subscribe", spec.Subscribe{Conn: uint32(h), Handle: at}); err != nil {
		return err
	}
	select {
	case <-c.gone:
		return ErrDisconnected
	case <-ctx.Done():
		send("unsubscribe", spec.Attr{Conn: uint32(h), Handle: at})
		return ctx.Err()
	}
}

// ---------------------------------------------------------------------
// The peripheral

func (desktop) Serve(ctx context.Context, services []ServedService, request func(Request)) error {
	var key uint32
	s := &serverSink{request: request, stopped: make(chan error, 1), attrs: map[uint32][2]string{}, subs: map[uint32]map[string]bool{}}
	a := spec.ServeArgs{}
	var handle uint32
	for _, ss := range services {
		ls := spec.LocalService{UUID: full(ss.Uuid), Primary: ss.Primary}
		for _, c := range ss.Characteristics {
			handle++
			s.attrs[handle] = [2]string{short(ss.Uuid), short(c.Uuid)}
			lc := spec.LocalCharacteristic{Handle: handle, UUID: full(c.Uuid), Properties: propertyMask(c.Properties),
				Permissions: permissionMask(c.Permissions)}
			if !c.Dynamic && len(c.Value) > 0 {
				lc.Value = c.Value
			}
			ls.Characteristics = append(ls.Characteristics, lc)
		}
		a.Services = append(a.Services, ls)
	}
	native.OnMain(func() {
		if br.servers == nil {
			br.servers = map[uint32]*serverSink{}
			br.requests = map[int]pendingRequest{}
		}
		key = id()
		a.ID = key
		br.servers[key] = s
	})
	defer native.Post(func() {
		delete(br.servers, key)
		for id, r := range br.requests {
			if r.server == key {
				delete(br.requests, id)
			}
		}
	})
	if _, err := call(ctx, "serve", a); err != nil {
		send("unserve", spec.ID{ID: key})
		return err
	}
	select {
	case err := <-s.stopped:
		if err == nil {
			err = ErrOff
		}
		return err
	case <-ctx.Done():
		send("unserve", spec.ID{ID: key})
		return ctx.Err()
	}
}

func (desktop) Respond(request int, value []byte, code int) error {
	native.OnMain(func() {
		r, ok := br.requests[request]
		if !ok {
			return
		}
		delete(br.requests, request)
		// A read's answer is the value from its offset on.
		if r.read && code == 0 && r.offset > 0 {
			if r.offset > len(value) {
				code = 0x07 // invalid offset
				value = nil
			} else {
				value = value[r.offset:]
			}
		}
		in, _ := json.Marshal(spec.Respond{Server: r.server, Request: r.request, Value: value, Error: code})
		engine()["respond"](in, func([]byte, error) {})
	})
	return nil
}

func (desktop) Notify(service, char string, value []byte) (int, error) {
	n := 0
	native.OnMain(func() {
		want := [2]string{short(service), short(char)}
		for key, s := range br.servers {
			for h, at := range s.attrs {
				if at != want {
					continue
				}
				if len(s.subs[h]) == 0 {
					continue
				}
				n += len(s.subs[h])
				in, _ := json.Marshal(spec.Notify{Server: key, Handle: h, Value: value})
				engine()["notify"](in, func([]byte, error) {})
			}
		}
	})
	return n, nil
}

func (desktop) Advertise(ctx context.Context, o Advertising) error {
	var key uint32
	stopped := make(chan error, 1)
	native.OnMain(func() {
		if br.adverts == nil {
			br.adverts = map[uint32]chan error{}
		}
		key = id()
		br.adverts[key] = stopped
	})
	defer native.Post(func() { delete(br.adverts, key) })
	a := spec.AdvertiseArgs{ID: key, LocalName: o.LocalName, Services: fulls(o.Services),
		ServiceData: serviceData(o.ServiceData, full), ManufacturerData: companyDecimal(o.ManufacturerData),
		Connectable: o.Connectable, IncludeTxPower: o.IncludeTxPower}
	if _, err := call(ctx, "advertise", a); err != nil {
		send("stopAdvertise", spec.ID{ID: key})
		return err
	}
	select {
	case err := <-stopped:
		if err == nil {
			err = ErrOff
		}
		return err
	case <-ctx.Done():
		send("stopAdvertise", spec.ID{ID: key})
		return ctx.Err()
	}
}
