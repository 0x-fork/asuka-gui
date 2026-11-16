//go:build darwin && !ios

package host

import "github.com/egoist/asuka/host/darwin"

// The Go Mac host's modules (the Swift host's: swift/Host.swift).
var callModule = darwin.CallModule
