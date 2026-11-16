//go:build windows

package preview

import (
	"errors"

	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: the file's default app (ShellExecute's open, which asks which
// app for a type that has none), owned by the window the user works in.

func supported() error { return nil }

func open(path string, done func(error)) {
	err := ShellExecute(native.Window(), "", path)
	if errors.Is(err, ErrNoAssociation) {
		err = ErrNoViewer
	}
	done(err)
}
