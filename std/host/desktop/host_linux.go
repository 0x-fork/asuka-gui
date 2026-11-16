//go:build linux && !android

package host

import "github.com/egoist/asuka/host/linux"

var callModule = linux.CallModule
