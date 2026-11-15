//go:build windows

package bridge

import "golang.org/x/sys/windows"

func openLib(path string) (uintptr, error) {
	h, err := windows.LoadLibrary(path)
	return uintptr(h), err
}
