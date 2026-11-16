package tracking

import "errors"

// Elsewhere there's no Go tracker: Linux has no such setting, and macOS
// runs the package's Swift.

func supported() error { return errors.ErrUnsupported }

func status() (Status, error) { return StatusAllowed, errors.ErrUnsupported }
