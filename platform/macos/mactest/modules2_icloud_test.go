//go:build darwin

package mactest

import (
	"encoding/json"
	"strings"
	"testing"
)

// icloud through the host's ICloud.swift (the iOS host's): the key-value
// store's values (a UserDefaults suite of the test's,
// ASUKA_ICLOUD_TEST_STORE), converted between JSON and property lists
// both ways; without iCloud's entitlements (the player has none), the
// account's and the documents' calls say why; "next" waits until "stop".
func TestModules2ICloud(t *testing.T) {
	set := `{"key":"asuka.test","value":{"n":1.5,"on":true,"s":"x","none":null,"list":[1,"two"],"in":{"deep":[]}}}`
	a := RunWith(t, m2Src+`
		app {
		  Column {
		    Call("kvSet", "icloud", "kvSet", `+m2Bytes(set)+`)
		    Call("kvGet", "icloud", "kvGet", "asuka.test".bytes()) { text: true }
		    Call("kvAll", "icloud", "kvAll", bytes()) { text: true }
		    Call("kvSet bad", "icloud", "kvSet", `+m2Bytes(`{"key":"asuka.bad","value":[null]}`)+`)
		    Call("kvDelete", "icloud", "kvDelete", "asuka.test".bytes())
		    Call("kvSync", "icloud", "kvSync", bytes())
		    Call("account", "icloud", "account", bytes())
		    Call("files", "icloud", "files", `+m2Bytes(`{"scope":"documents"}`)+`)
		    Call("read", "icloud", "read", "/tmp/x".bytes())
		    Call("next", "icloud", "next", bytes())
		    Call("stop", "icloud", "stop", bytes())
		  }
		}
	`, Options{Env: []string{"ASUKA_ICLOUD_TEST_STORE=1"}})
	m2Want(t, "kvSet", strings.TrimSpace(m2Go(t, a, "kvSet")), "ok")
	got := m2Go(t, a, "kvGet")
	var v map[string]any
	if err := json.Unmarshal([]byte(strings.TrimPrefix(got, "ok ")), &v); err != nil {
		t.Fatalf("kvGet: %s, %v", got, err)
	}
	if _, has := v["none"]; has || v["n"] != 1.5 || v["on"] != true || v["s"] != "x" || len(v["list"].([]any)) != 2 {
		t.Errorf("kvGet: %s", got)
	}
	if all := m2Go(t, a, "kvAll"); !strings.Contains(all, `"asuka.test"`) {
		t.Errorf("kvAll: %s", all)
	}
	if got := m2Go(t, a, "kvSet bad"); !strings.HasPrefix(got, "err ") {
		t.Errorf("a null in an array was kept: %s", got)
	}
	m2Want(t, "kvDelete", strings.TrimSpace(m2Go(t, a, "kvDelete")), "ok")
	m2Want(t, "deleted", strings.TrimSpace(m2Go(t, a, "kvGet")), "ok")
	m2Want(t, "kvSync", strings.TrimSpace(m2Go(t, a, "kvSync")), "ok")

	// Unsigned, there's no iCloud (whatever the Mac is signed in to), and
	// the calls say why.
	if got := m2Go(t, a, "account"); !strings.HasPrefix(got, "err not available: the app isn't signed with iCloud") {
		t.Errorf("account unsigned: %s", got)
	}
	if got := m2Go(t, a, "files"); !strings.HasPrefix(got, "err not available: the app isn't signed with iCloud's documents") {
		t.Errorf("files unsigned: %s", got)
	}
	if got := m2Go(t, a, "read"); !strings.HasPrefix(got, "err not available") {
		t.Errorf("read unsigned: %s", got)
	}

	// "next" waits, and "stop" ends it.
	a.Do("tap go next")
	if s := m2Text(a.Tree(), "next"); s == nil || *s != "pending" {
		t.Fatalf("next answered before stop: %v", s)
	}
	m2Want(t, "stop", strings.TrimSpace(m2Go(t, a, "stop")), "ok")
	m2Want(t, "next, stopped", m2Answer(t, a, "next"), "err stopped")
}
