//go:build windows

package windows

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
	if m := h.asyncModule(module)[method]; m != nil {
		m(args, reply)
		return
	}
	f := h.module(module)
	if f == nil {
		reply(nil, fmt.Errorf("unsupported: %s isn't on Windows", module))
		return
	}
	reply(f(method, args))
}
