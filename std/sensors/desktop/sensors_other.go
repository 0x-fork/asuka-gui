//go:build !(linux && !android)

package sensors

import "time"

// No sensors an app reads here.

func has(Sensor) bool { return false }

func prime() {}

func readProps(done func(map[string]any)) { done(nil) }

func follow(Sensor, map[string]any, func([]float64, time.Duration)) (func(), error) {
	return nil, ErrNotAvailable
}
