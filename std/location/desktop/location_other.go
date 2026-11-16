//go:build !(linux && !android) && !windows

package location

// Elsewhere (macOS runs the package's Swift) there's no Go locator.

func access() Authorization                           { return Authorization{Level: LevelDenied} }
func requestAccess(done func())                       { done() }
func fix(done func(Position, error))                  { done(Position{}, errUnsupported) }
func watchFixes(func(Position), func(error)) func()   { return func() {} }
func geocode(_, _ float64, done func(string, error))  { done("", errUnsupported) }
func openSettings() error                             { return errUnsupported }
func support() Support                                { return Support{} }
func addFence(Region) error                           { return ErrUnavailable }
func removeFence(string)                              {}
func fences() []Region                                { return nil }
func watchFences(func(string, Transition, *Position)) {}
