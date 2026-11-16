//go:build darwin && !ios

package host

import "github.com/egoist/asuka/host/darwin"

// The Mac's host is Go (the package has no Swift for macOS).
var callModule = darwin.CallModule
