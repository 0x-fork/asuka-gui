//go:build windows

package power

import (
	"context"
	"time"

	win "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the battery and the power source (GetSystemPowerStatus; low
// power is battery saver, and Windows 11's energy saver, which it reports
// the same way; Windows has no thermal state for apps: nominal), read
// again as win.WM_POWERBROADCAST says they changed. Sleep and waking are
// win.PBT_APMSUSPEND and win.PBT_APMRESUMEAUTOMATIC (or win.PBT_APMRESUMESUSPEND
// alone), which RegisterSuspendResumeNotification sends on PCs with
// modern standby too, reported once each; locking is the session's
// win.WM_WTSSESSION_CHANGE. The idle time is GetLastInputInfo's. All of it
// comes to internal/windows' system window.

func supported() error { return nil }

func state() Status {
	p, ok := win.SystemPowerStatus()
	if !ok {
		return unknown
	}
	battery := p.BatteryFlag != 128 && p.BatteryFlag != 255
	s := Status{Battery: battery, OnBattery: battery && p.ACLine == 0, Level: -1, Charging: battery && p.BatteryFlag&8 != 0, LowPower: p.SystemStatus&1 != 0}
	if battery && p.BatteryPercent <= 100 {
		s.Level = float64(p.BatteryPercent) / 100
	}
	return s
}

// asleep: suspending was reported, waking not yet. Both the broadcast and
// RegisterSuspendResumeNotification's message can come.
var asleep bool

func follow(changed func(), happened func(Event)) (stop func()) {
	return win.OnSystemMessage(func(m uint32, wp, _ uintptr) {
		switch m {
		case win.WM_POWERBROADCAST:
			switch wp {
			case win.PBT_APMPOWERSTATUSCHANGE, win.PBT_POWERSETTINGCHANGE:
				if changed != nil {
					changed()
				}
			case win.PBT_APMSUSPEND:
				if !asleep {
					asleep = true
					if happened != nil {
						happened(EventSuspend)
					}
				}
			case win.PBT_APMRESUMEAUTOMATIC, win.PBT_APMRESUMESUSPEND:
				if asleep {
					asleep = false
					if happened != nil {
						happened(EventResume)
					}
				}
				if changed != nil {
					changed() // unplugged while asleep
				}
			}
		case win.WM_WTSSESSION_CHANGE:
			if happened == nil {
				return
			}
			switch wp {
			case win.WTS_SESSION_LOCK:
				happened(EventLock)
			case win.WTS_SESSION_UNLOCK:
				happened(EventUnlock)
			}
		}
	})
}

func (monitor) Idle(context.Context) (d time.Duration, err error) {
	var ms uint32
	var ok bool
	native.OnMain(func() { ms, ok = win.IdleMillis() })
	if !ok {
		return 0, ErrIdleUnknown
	}
	return time.Duration(ms) * time.Millisecond, nil
}
