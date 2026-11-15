//go:build windows

package network

import (
	"github.com/egoist/asuka/internal/windows"
)

// Windows: the Network List Manager (connectivity, and the connection's
// cost: metered, roaming, over its data limit as constrained), made on
// the main thread, whose apartment its events come back to.

var list *windows.NetworkList

func supported() error { return nil }

func manager() (*windows.NetworkList, error) {
	if list == nil {
		l, err := windows.NewNetworkList(nil)
		if err != nil {
			return nil, err
		}
		list = l
	}
	return list, nil
}

func state() (Status, error) {
	l, err := manager()
	if err != nil {
		return Status{}, err
	}
	s := l.State()
	return Status{Connected: s.Connected, Type: ConnectionType(s.Type), Metered: s.Metered, Constrained: s.Constrained}, nil
}

func follow(changed func()) (stop func(), err error) {
	l, err := windows.NewNetworkList(changed)
	if err != nil {
		return nil, err
	}
	return l.Close, nil
}
