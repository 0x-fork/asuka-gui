//go:build linux && !android

package secure

import (
	"context"
	"strings"

	"github.com/egoist/asuka/internal/boot"
	. "github.com/egoist/asuka/internal/gtk"
	"github.com/egoist/asuka/native"
)

// Linux: the user's keyring through libsecret (the Secret Service: GNOME
// Keyring, KWallet, KeePassXC), each key an item of the app's own schema
// (its ID), labeled with the app's name and the key: the Linux host's
// secure store's items. Without libsecret, or a keyring, the calls fail
// saying so. libsecret answers on GLib's main loop: the calls start on
// the main thread, and the goroutine waits for their answer.

func supported() error { return nil }

// schema is the app's ID, as the Linux host names its application.
func schema() string {
	if boot.ID != "" && ApplicationIDIsValid(boot.ID) {
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
	id := "dev.asuka.app." + b.String()
	if b.Len() == 0 || !ApplicationIDIsValid(id) {
		return "dev.asuka.App"
	}
	return id
}

func attrs(key string) map[string]string { return map[string]string{"key": key} }

// wait runs start on the main thread and waits for its answer, or ctx.
func wait[T any](ctx context.Context, start func(done func(T))) (T, error) {
	ch := make(chan T, 1)
	native.Post(func() { start(func(v T) { ch <- v }) })
	select {
	case v := <-ch:
		return v, nil
	case <-ctx.Done():
		var zero T
		return zero, ctx.Err()
	}
}

type found struct {
	v   []byte
	ok  bool
	err error
}

func (keychain) Get(ctx context.Context, key string) (*string, error) {
	r, err := wait(ctx, func(done func(found)) {
		SecretLookup(schema(), attrs(key), func(v []byte, ok bool, err error) { done(found{v, ok, err}) })
	})
	if err != nil {
		return nil, err
	}
	if r.err != nil || !r.ok {
		return nil, r.err
	}
	s := string(r.v)
	return &s, nil
}

func (keychain) Set(ctx context.Context, key, value string) error {
	err, werr := wait(ctx, func(done func(error)) {
		SecretStore(schema(), attrs(key), appName()+": "+key, []byte(value), done)
	})
	if werr != nil {
		return werr
	}
	return err
}

func (keychain) Remove(ctx context.Context, key string) error {
	err, werr := wait(ctx, func(done func(error)) {
		SecretClear(schema(), attrs(key), done)
	})
	if werr != nil {
		return werr
	}
	return err
}
