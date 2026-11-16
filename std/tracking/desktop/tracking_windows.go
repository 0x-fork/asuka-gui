//go:build windows

package tracking

import (
	"unsafe"

	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: Settings' "Let apps show me personalized ads by using my
// advertising ID": off, the ID is empty (denied). Asked on the main
// thread, whose apartment WinRT runs in.

var iidAdvertisingManagerStatics = NewGUID("ADD3468C-A273-48CB-B346-3544522D5581")

func supported() error { return nil }

func status() (s Status, err error) {
	native.OnMain(func() {
		s = StatusAllowed
		st, err := ActivationFactory("Windows.System.UserProfile.AdvertisingManager", iidAdvertisingManagerStatics)
		if err != nil {
			return // no setting: as Android
		}
		defer Release(st)
		var hs HString
		if Failed(ComCall(st, 6, uintptr(unsafe.Pointer(&hs)))) { // get_AdvertisingId
			return
		}
		id := hs.String()
		hs.Delete()
		if id == "" {
			s = StatusDenied
		}
	})
	return s, nil
}
