//go:build windows

package shell

import (
	"context"
	"errors"

	. "github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

// Windows: Explorer (SHOpenFolderAndSelectItems), ShellExecute's open,
// the Recycle Bin (IFileOperation), MessageBeep. Reveal and Trash wait
// for the shell on their goroutine's own thread; ShellExecute, which may
// ask which app opens a type, runs on the main thread, owned by the
// window the user works in.

func supported() error { return nil }

func (desktop) Reveal(_ context.Context, path string) error {
	if err := there(path, true); err != nil {
		return err
	}
	return RevealInFolder(path)
}

func (desktop) Open(_ context.Context, path string) error {
	if err := there(path, false); err != nil {
		return err
	}
	var err error
	native.OnMain(func() { err = ShellExecute(native.Window(), "", path) })
	if errors.Is(err, ErrNoAssociation) {
		return ErrNoApp
	}
	return err
}

func (desktop) Trash(_ context.Context, path string) error { return Trash(path) }

func (desktop) Beep() error {
	AlertBeep()
	return nil
}
