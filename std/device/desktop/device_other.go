//go:build !(linux && !android)

package device

import (
	"context"
	"errors"
)

// Elsewhere (macOS runs the package's Swift) there's no Go device.

func supported() error { return errors.ErrUnsupported }

func (platform) Info(context.Context) (Info, error)                { return Info{}, errors.ErrUnsupported }
func (platform) Browse(context.Context, string) error              { return errors.ErrUnsupported }
func (platform) PickPhotos(context.Context, int) ([]string, error) { return nil, errors.ErrUnsupported }
func (platform) PickFiles(context.Context, []string, bool) ([]File, error) {
	return nil, errors.ErrUnsupported
}
func (platform) ClipboardText(context.Context) (*string, error) { return nil, errors.ErrUnsupported }
func (platform) ClipboardHas(context.Context) (ClipboardKinds, error) {
	return ClipboardKinds{}, errors.ErrUnsupported
}
func (platform) ClipboardImage(context.Context) (*string, error) { return nil, errors.ErrUnsupported }
func (platform) CopyImage(context.Context, string) error         { return errors.ErrUnsupported }
func (platform) WatchClipboard(context.Context, func(ClipboardKinds)) error {
	return errors.ErrUnsupported
}
func (platform) KeepAwake(bool) error { return nil }
