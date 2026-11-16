//go:build linux && !android

package linux

import (
	"errors"
	"fmt"
)

// CallModule runs a method of one of the host's own modules (what
// ui.Call reaches) for the standard library's packages (std/host): reply
// gets its answer once, on the main thread. Main thread.
func CallModule(module, method string, args []byte, reply func([]byte, error)) {
	h := theHost
	if h == nil {
		reply(nil, errors.New("asuka: the host hasn't started"))
		return
	}
	m := h.module(module)[method]
	if m == nil {
		reply(nil, fmt.Errorf("unsupported: %s.%s isn't on Linux", module, method))
		return
	}
	m(args, reply)
}
