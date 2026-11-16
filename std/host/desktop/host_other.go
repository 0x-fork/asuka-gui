//go:build !windows && !(darwin && !ios)

package host

// Elsewhere there's no Go host: on Linux the host is C (platform/linux),
// whose modules asuka/platform reaches.
var callModule func(module, method string, args []byte, reply func([]byte, error))
