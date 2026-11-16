// asuka/tracking on Windows (macOS runs the package's Swift; Linux has
// no such setting, which Asuka's side answers as allowed).
package tracking

import "context"

type tracker struct{}

func newTracker() (Tracker, error) {
	if err := supported(); err != nil {
		return nil, err
	}
	return tracker{}, nil
}

func (tracker) Current() (Status, error) { return status() }

func (tracker) Request(context.Context) (Status, error) { return status() }
