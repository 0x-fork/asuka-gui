//go:build !(linux && !android) && !windows && !(darwin && !ios)

package host

// Elsewhere there's no Go host.
var callModule func(module, method string, args []byte, reply func([]byte, error))
