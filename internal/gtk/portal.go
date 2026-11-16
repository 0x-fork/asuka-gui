//go:build linux && !android

package gtk

import (
	"strconv"
	"strings"
)

// The desktop portals (xdg-desktop-portal: the print, settings and
// open-URI portals...), for native packages' Go as for the Linux host.

const (
	PortalDest = "org.freedesktop.portal.Desktop" // the portals' bus name and object
	PortalPath = "/org/freedesktop/portal/desktop"
)

var portalTokens int

// portalToken is a new handle_token.
func portalToken() string {
	portalTokens++
	return "asuka" + strconv.Itoa(portalTokens)
}

// ErrNoPortal reports whether err is a portal call's error when the portal (or the
// interface) isn't there.
func ErrNoPortal(err error) bool {
	if err == nil {
		return false
	}
	s := err.Error()
	for _, e := range []string{"ServiceUnknown", "UnknownMethod", "UnknownInterface", "UnknownObject", "No such interface", "NameHasNoOwner"} {
		if strings.Contains(s, e) {
			return true
		}
	}
	return false
}

// PortalRequest calls a portal's method that answers through a Request
// (its Response signal): params gets the handle token to put in its
// options. done gets the response (0 done, 1 cancelled by the user, 2
// another end) and its results, on the main thread.
func PortalRequest(iface, method string, params func(token string) Ptr, fds []int32, done func(response uint32, results map[string]any, err error)) {
	conn, err := Bus(BusSession)
	if err != nil {
		done(2, nil, err)
		return
	}
	token := portalToken()
	sender := strings.ReplaceAll(strings.TrimPrefix(UniqueName(conn), ":"), ".", "_")
	path := "/org/freedesktop/portal/desktop/request/" + sender + "/" + token
	answered := false
	var sub uint32
	listen := func(path string) {
		sub = DBusSubscribe(conn, PortalDest, "org.freedesktop.portal.Request", "Response", path, func(s DBusSignal) {
			if answered {
				return
			}
			answered = true
			DBusUnsubscribe(conn, sub)
			args, _ := VariantGo(s.Params).([]any)
			var code uint32 = 2
			var results map[string]any
			if len(args) >= 2 {
				code, _ = args[0].(uint32)
				results, _ = args[1].(map[string]any)
			}
			done(code, results, nil)
		})
	}
	listen(path)
	reply := func(r Ptr, err error) {
		if answered {
			return
		}
		if err != nil {
			answered = true
			DBusUnsubscribe(conn, sub)
			done(2, nil, err)
			return
		}
		// An older portal's request path isn't the token's.
		if args, _ := VariantGo(r).([]any); len(args) > 0 {
			if p, _ := args[0].(string); p != "" && p != path {
				DBusUnsubscribe(conn, sub)
				listen(p)
			}
		}
	}
	if len(fds) > 0 {
		DBusCallFDs(conn, PortalDest, PortalPath, iface, method, params(token), fds, -1, reply)
		return
	}
	DBusCall(conn, PortalDest, PortalPath, iface, method, params(token), -1, reply)
}
