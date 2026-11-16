//go:build windows

package secure

import (
	"context"
	"errors"

	. "github.com/egoist/asuka/internal/windows"
)

// Windows: Credential Manager, a generic credential per key, named the
// app's name / the key (as the Windows host's secure store named them).

func supported() error { return nil }

func target(key string) string { return appName() + "/" + key }

func (keychain) Get(_ context.Context, key string) (*string, error) {
	v, err := ReadCredential(target(key))
	if errors.Is(err, ErrNoCredential) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	s := string(v)
	return &s, nil
}

func (keychain) Set(_ context.Context, key, value string) error {
	return WriteCredential(target(key), key, []byte(value))
}

func (keychain) Remove(_ context.Context, key string) error {
	err := DeleteCredential(target(key))
	if errors.Is(err, ErrNoCredential) {
		return nil
	}
	return err
}
