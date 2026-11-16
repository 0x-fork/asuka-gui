//go:build windows

package biometric

import (
	"context"

	"github.com/egoist/asuka/internal/localfile"
	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: Windows Hello (UserConsentVerifier). The kind is the sensor
// Windows Hello has (the Windows Biometric Framework's); enrolled is a
// sensor and Windows Hello set up, the passcode Windows Hello set up (its
// PIN). Its prompt shows over the window the user works in, with the
// reason; it always offers the PIN, and names its own cancel button: with
// no passcode asked, it shows only where there's a sensor.

func supported() error { return nil }

func availability(ctx context.Context) (HelloAvailability, error) {
	ch := make(chan HelloAvailability, 1)
	HelloCheck(func(a HelloAvailability, _ error) { ch <- a })
	select {
	case a := <-ch:
		return a, nil
	case <-ctx.Done():
		return HelloDeviceNotPresent, ctx.Err()
	}
}

func (authenticator) Check(ctx context.Context) (Status, error) {
	kind := Kind(BiometricSensor())
	a, err := availability(ctx)
	if err != nil {
		return Status{}, err
	}
	set := a == HelloAvailable
	return Status{Kind: kind, Enrolled: set && kind != KindNone, Passcode: set}, nil
}

func (authenticator) Authenticate(ctx context.Context, reason, _ string, noPasscode bool) error {
	if reason == "" {
		reason = localfile.AppName()
	}
	if noPasscode && BiometricSensor() == 0 {
		return ErrNotAvailable
	}
	a, err := availability(ctx)
	if err != nil {
		return err
	}
	if a != HelloAvailable {
		return ErrNotAvailable
	}
	ch := make(chan HelloResult, 1)
	native.Post(func() { HelloVerify(native.Window(), reason, func(r HelloResult, _ error) { ch <- r }) })
	select {
	case r := <-ch:
		switch r {
		case HelloVerified:
			return nil
		case HelloCanceled:
			return ErrCanceled
		case HelloNoDevice, HelloNotConfiguredForUser, HelloDisabled, HelloRetriesExhausted, HelloBusy:
			return ErrNotAvailable
		}
		return ErrFailed
	case <-ctx.Done():
		return ctx.Err()
	}
}
