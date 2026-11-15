//go:build linux && !android

package network

import (
	"errors"

	. "github.com/egoist/asuka/internal/gtk"
)

// Linux: GNetworkMonitor (NetworkManager's, the portal's in a sandbox)
// says whether the network is there and metered; NetworkManager, when
// it runs, says what carries it (its primary connection's type), asked
// as it changes. Linux has no low data mode.

var (
	mon  Ptr
	kind = ConnectionType(0) // NetworkManager's, 0: not known
)

func supported() error { return nil }

func monitorPtr() (Ptr, error) {
	if mon == 0 {
		mon = Sys.NetworkMonitorGetDefault()
		if mon == 0 {
			return 0, errors.New("unsupported: no network monitor")
		}
	}
	return mon, nil
}

func state() (Status, error) {
	m, err := monitorPtr()
	if err != nil {
		return Status{}, err
	}
	const full = 4 // G_NETWORK_CONNECTIVITY_FULL
	if !Sys.NetworkMonitorAvailable(m) || Sys.NetworkMonitorConnectivity(m) != full {
		return Status{}, nil
	}
	t := kind
	if t == 0 {
		t = ConnectionTypeOther
	}
	return Status{Connected: true, Type: t, Metered: Sys.NetworkMonitorMetered(m)}, nil
}

// follow calls changed as the network changes, until stop.
func follow(changed func()) (stop func(), err error) {
	m, err := monitorPtr()
	if err != nil {
		return nil, err
	}
	read := func() {
		conn, err := Bus(BusSystem)
		if err != nil {
			changed()
			return
		}
		DBusCall(conn, "org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties", "Get",
			VTuple(VStr("org.freedesktop.NetworkManager"), VStr("PrimaryConnectionType")), 2000, func(r Ptr, err error) {
				kind = 0
				if err == nil {
					if a, _ := VariantGo(r).([]any); len(a) > 0 {
						t, _ := a[0].(string)
						kind = nmType(t)
					}
				}
				changed()
			})
	}
	hs := []uint64{
		Connect(m, "network-changed", func(uintptr) { read() }),
		Connect(m, "notify::network-metered", func(uintptr) { read() }),
		Connect(m, "notify::connectivity", func(uintptr) { read() }),
	}
	var sub uint32
	conn, cerr := Bus(BusSystem)
	if cerr == nil {
		// The primary connection changing (Wi-Fi to a cable) isn't the
		// monitor's change.
		sub = DBusSubscribe(conn, "org.freedesktop.NetworkManager", "org.freedesktop.DBus.Properties", "PropertiesChanged", "/org/freedesktop/NetworkManager", func(DBusSignal) { read() })
	}
	read()
	return func() {
		for _, h := range hs {
			Disconnect(m, h)
		}
		if sub != 0 {
			DBusUnsubscribe(conn, sub)
		}
	}, nil
}

// nmType is a NetworkManager connection type.
func nmType(t string) ConnectionType {
	switch t {
	case "":
		return 0
	case "802-11-wireless", "wifi-p2p":
		return ConnectionTypeWifi
	case "gsm", "cdma", "wwan":
		return ConnectionTypeCellular
	case "802-3-ethernet", "pppoe", "infiniband", "veth":
		return ConnectionTypeEthernet
	}
	return ConnectionTypeOther
}
