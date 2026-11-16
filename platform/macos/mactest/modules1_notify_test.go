//go:build darwin

package mactest

import (
	"encoding/json"
	"os/exec"
	"strings"
	"testing"
)

// The notify and push modules (Notify.swift, Push.swift). The packages'
// tests (asuka/notify, asuka/push) run on the notify player
// (modules1_player_test.go), which links asuka/host's Swift; the module
// tests call each method by name through asuka/platform, each answer or
// error shown in a Text, for what the packages don't answer (their sends)
// and the errors they don't reach.
// What asks the user (notify's request, push's register) is called only
// where it fails before asking (no bundle): a test can't answer the
// prompt. The players are app bundles, which the notification center
// needs.

// m1CallSrc is a component that calls module.method with args once and
// shows "label: ok [answer]" or "label: err error".
const m1CallSrc = `
	import asuka/platform

	component Call(label: string, module: string, method: string, args: bytes) {
	  state s = "pending"
	  task {
	    do {
	      let b = try await platform.call(module, method, args)
	      s = "ok [{b.string()}]"
	    } catch e {
	      s = "err {e.message}"
	    }
	  }
	  Text("{label}: {s}")
	}
`

// m1Until waits for every text of want to show.
func m1Until(t *testing.T, a *App, want ...string) *Tree {
	t.Helper()
	return a.Until(func(tr *Tree) bool {
		for _, w := range want {
			if tr.FindText(w) == nil {
				return false
			}
		}
		return true
	})
}

// m1State is what the tree's JSON shows of the modules (modules1JSON).
type m1State struct {
	Notify struct {
		Bundled               bool    `json:"bundled"`
		Delegate              bool    `json:"delegate"`
		InstalledBeforeLaunch bool    `json:"installedBeforeLaunch"`
		Badge                 string  `json:"badge"`
		Categories            string  `json:"categories"`
		Presenting            bool    `json:"presenting"`
		PendingPresent        int     `json:"pendingPresent"`
		PendingActions        int     `json:"pendingActions"`
		Presents              m1Queue `json:"presents"`
		Actions               m1Queue `json:"actions"`
	} `json:"notify"`
	Push struct {
		Registering int     `json:"registering"`
		Received    m1Queue `json:"received"`
		Opened      m1Queue `json:"opened"`
	} `json:"push"`
}

type m1Queue struct {
	Kept    int `json:"kept"`
	Waiting int `json:"waiting"`
}

func m1Of(t *testing.T, tr *Tree) m1State {
	t.Helper()
	var d struct {
		M m1State `json:"modules1"`
	}
	if err := json.Unmarshal(tr.raw, &d); err != nil {
		t.Fatal(err)
	}
	return d.M
}

// m1UntilState waits for f to hold of the modules' state.
func m1UntilState(t *testing.T, a *App, f func(m m1State) bool) m1State {
	t.Helper()
	return m1Of(t, a.Until(func(tr *Tree) bool { return f(m1Of(t, tr)) }))
}

// The module, method by method: the answers, the errors, the categories
// kept for the next launch, the presenter, the waiting calls and stop; the
// center's delegate is the host's, installed before launch ended.
func TestNotifyModule(t *testing.T) {
	a := Run(t, m1CallSrc+`
		component Waits(module: string, method: string) {
		  state s = "waiting"
		  task {
		    do {
		      let b = try await platform.call(module, method, bytes())
		      s = "came {b.string()}"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("{method}: {s}")
		}

		component App {
		  state stopped = false
		  Column {
		    Call("channels", "notify", "channels", bytes())
		    Call("channelGroups", "notify", "channelGroups", "[]".bytes())
		    Call("badge", "notify", "badge", platform.u32(3))
		    Call("present on", "notify", "present", platform.u8(1))
		    Call("presented", "notify", "presented", platform.u8(2).concat("no-such-token".bytes()))
		    Call("actionDone", "notify", "actionDone", "no-such-token".bytes())
		    Call("categories", "notify", "categories", "[\{\"id\":\"chat\",\"actions\":[\{\"id\":\"reply\",\"title\":\"Reply\",\"open\":false,\"reply\":\{\"button\":\"Send\",\"placeholder\":\"Message\"}},\{\"id\":\"open\",\"title\":\"Open\",\"open\":true,\"icon\":\"star\"}]}]".bytes())
		    Call("cancel", "notify", "cancel", "mactest-none".bytes())
		    Call("remove", "notify", "remove", "[\"mactest-none\"]".bytes())
		    Call("removeThread", "notify", "removeThread", "mactest-thread".bytes())
		    Call("removeAll", "notify", "removeAll", bytes())
		    Call("showing", "notify", "showing", bytes())
		    Call("bad schedule", "notify", "schedule", "not json".bytes())
		    Call("nope", "notify", "nope", bytes())
		    Waits("notify", "nextAction")
		    Waits("notify", "nextPresent")
		    Button("Stop") {
		      on press {
		        platform.send("notify", "stop", "nextAction".bytes())
		        platform.send("notify", "stop", "nextPresent".bytes())
		        stopped = true
		      }
		    }
		    if stopped {
		      Call("present off", "notify", "present", platform.u8(0))
		      Call("badge off", "notify", "badge", platform.u32(0))
		    }
		  }
		}
		app { App() }
	`)
	tr := m1Until(t, a,
		"channels: ok []", "channelGroups: ok []", "badge: ok []", "present on: ok []", "presented: ok []", "actionDone: ok []",
		"categories: ok []", "cancel: ok []", "remove: ok []", "removeThread: ok []", "removeAll: ok []",
		// The player's bundle shows none of its own.
		"showing: ok [[]]",
		"bad schedule: err bad notification",
		"nope: err unsupported: notify.nope isn't on macOS",
		"nextAction: waiting", "nextPresent: waiting")
	m := m1Of(t, tr)
	if !m.Notify.Bundled || !m.Notify.Delegate || !m.Notify.InstalledBeforeLaunch {
		t.Errorf("the center's delegate: %+v", m.Notify)
	}
	// The Dock tile's badge.
	if m.Notify.Badge != "3" {
		t.Errorf("the badge: %q, want 3", m.Notify.Badge)
	}
	if !m.Notify.Presenting || !strings.Contains(m.Notify.Categories, `"id":"chat"`) {
		t.Errorf("presenting, categories: %+v", m.Notify)
	}
	m = m1UntilState(t, a, func(m m1State) bool { return m.Notify.Actions.Waiting == 1 && m.Notify.Presents.Waiting == 1 })
	// The categories are kept for the next launch.
	out, err := exec.Command("defaults", "read", "dev.asuka.mactest", "asuka.notify.categories").CombinedOutput()
	if err != nil || len(strings.TrimSpace(string(out))) == 0 {
		t.Errorf("the categories weren't kept: %v %s", err, out)
	}
	// "stop" fails the calls that wait with "stopped".
	a.Do("tap Stop")
	m1Until(t, a, "nextAction: err stopped", "nextPresent: err stopped", "present off: ok []", "badge off: ok []")
	m = m1UntilState(t, a, func(m m1State) bool { return m.Notify.Actions.Waiting == 0 && m.Notify.Presents.Waiting == 0 })
	if m.Notify.Presenting || m.Notify.Badge != "" {
		t.Errorf("after present off, badge 0: %+v", m.Notify)
	}
}

// asuka/notify itself, on the notify player: what it answers, and what its
// sends do (the badge, the categories, the presenter, the responses that
// wait). A notification scheduled is added (an hour from now, then
// cancelled), or the center says why not (notifications not allowed).
func TestNotifyPackage(t *testing.T) {
	a := RunWith(t, `
		import asuka/notify

		component Showing {
		  state s = "pending"
		  task {
		    do {
		      let l = try await notify.showing()
		      s = "ok {l.count()}"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("showing: {s}")
		}

		component Scheduled {
		  state s = "pending"
		  task {
		    do {
		      try await notify.schedule(notify.Notification(id: "mactest-later", title: "Tea", body: "Ready", after: 1h,
		        data: ["k": "v"], thread: "t", badge: 2, image: "https://127.0.0.1:1/none.png"))
		      s = "ok"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("schedule: {s}")
		}

		component Responses {
		  state n = 0
		  task {
		    for await r in notify.responses() {
		      n += 1
		    }
		  }
		  Text("responses: {n}")
		}

		component App {
		  state step = 0
		  Column {
		    Showing()
		    Scheduled()
		    if step == 0 { Responses() }
		    Text("step {step}")
		    Button("Set up") {
		      on press {
		        notify.setBadge(7)
		        notify.setCategories([
		          notify.Category(id: "message", actions: [
		            notify.Action(id: "reply", title: "Reply", icon: "arrowshape.turn.up.left", reply: notify.Reply(button: "Send", placeholder: "Message")),
		            notify.Action(id: "mute", title: "Mute", destructive: true),
		          ]),
		          notify.Category(id: "reminder", actions: [notify.Action(id: "open", title: "Open", open: true)]),
		        ])
		        notify.setChannelGroups([notify.ChannelGroup(id: "chats", name: "Chats")])
		        notify.setChannels([notify.Channel(id: "messages", name: "Messages", importance: .high, group: "chats")])
		        notify.present(n => if n.thread == "hidden" { .hide } else { .banner })
		        notify.remove(["mactest-none"])
		        notify.removeThread("mactest-thread")
		        notify.removeAll()
		        step = 1
		      }
		    }
		    Button("Tear down") {
		      on press {
		        notify.stopPresenting()
		        notify.setBadge(0)
		        notify.cancel("mactest-later")
		        step = 2
		      }
		    }
		  }
		}
		app { App() }
	`, Options{Player: hostPlayer(t)})
	m1Until(t, a, "showing: ok 0", "responses: 0")
	a.Until(func(*Tree) bool {
		for _, s := range a.Texts() {
			if s == "schedule: ok" || strings.HasPrefix(s, "schedule: err ") {
				return true
			}
		}
		return false
	})
	t.Logf("%s", a.Texts())
	// The responses wait for one.
	m := m1UntilState(t, a, func(m m1State) bool { return m.Notify.Actions.Waiting == 1 })
	if !m.Notify.Delegate || !m.Notify.InstalledBeforeLaunch {
		t.Errorf("the center's delegate: %+v", m.Notify)
	}
	a.Do("tap Set up")
	m = m1UntilState(t, a, func(m m1State) bool {
		return m.Notify.Badge == "7" && m.Notify.Presenting && m.Notify.Presents.Waiting == 1 && m.Notify.Categories != ""
	})
	var cs []struct {
		ID      string `json:"id"`
		Actions []struct {
			ID          string `json:"id"`
			Icon        string `json:"icon"`
			Destructive bool   `json:"destructive"`
			Open        bool   `json:"open"`
			Reply       *struct{ Button, Placeholder string }
		} `json:"actions"`
	}
	if err := json.Unmarshal([]byte(m.Notify.Categories), &cs); err != nil || len(cs) != 2 || cs[0].ID != "message" || len(cs[0].Actions) != 2 ||
		cs[0].Actions[0].Reply == nil || cs[0].Actions[0].Reply.Button != "Send" || !cs[0].Actions[1].Destructive || !cs[1].Actions[0].Open {
		t.Errorf("the categories: %v %s", err, m.Notify.Categories)
	}
	out, err := exec.Command("defaults", "read", hostPlayerID, "asuka.notify.categories").CombinedOutput()
	if err != nil || len(strings.TrimSpace(string(out))) == 0 {
		t.Errorf("the categories weren't kept: %v %s", err, out)
	}
	// Leaving the responses stops their call.
	a.UntilText("step 1")
	m1UntilState(t, a, func(m m1State) bool { return m.Notify.Actions.Waiting == 0 })
	// Not presenting: the presenter's call stops too.
	a.Do("tap Tear down")
	a.UntilText("step 2")
	m1UntilState(t, a, func(m m1State) bool {
		return !m.Notify.Presenting && m.Notify.Presents.Waiting == 0 && m.Notify.Badge == ""
	})
}

// An executable run on its own (no bundle identifier) has no notification
// center: asuka/notify and asuka/push say so, and what needs no center (the
// Dock's badge, Android's channels) works.
func TestNotifyWithoutBundle(t *testing.T) {
	a := RunWith(t, `
		import asuka/notify
		import asuka/push

		component Request {
		  state s = "pending"
		  task {
		    do {
		      let ok = try await notify.request()
		      s = "ok {ok}"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("request: {s}")
		}

		component Showing {
		  state s = "pending"
		  task {
		    do {
		      let l = try await notify.showing()
		      s = "ok {l.count()}"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("showing: {s}")
		}

		component Show {
		  state s = "pending"
		  task {
		    do {
		      try await notify.show(notify.Notification(title: "x"))
		      s = "ok"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("show: {s}")
		}

		component Register {
		  state s = "pending"
		  task {
		    do {
		      let r = try await push.register()
		      s = "ok {r.token}"
		    } catch e {
		      s = "err {e.message}"
		    }
		  }
		  Text("register: {s}")
		}

		component App {
		  state badged = false
		  Column {
		    Request()
		    Showing()
		    Show()
		    Register()
		    Button("Badge") {
		      on press {
		        notify.setBadge(3)
		        notify.setChannels([notify.Channel(id: "c", name: "C")])
		        badged = true
		      }
		    }
		    if badged { Text("badged") }
		  }
		}
		app { App() }
	`, Options{Player: unbundled(t)})
	const noBundle = "asuka/notify: macOS's notifications need the app's bundle: build it with asuka build"
	tr := m1Until(t, a, "request: err "+noBundle, "showing: err "+noBundle, "show: err "+noBundle,
		"register: err asuka/push: APNs needs the app's bundle: build it with asuka build")
	if m := m1Of(t, tr); m.Notify.Bundled || m.Notify.Delegate || m.Notify.InstalledBeforeLaunch {
		t.Errorf("without a bundle: %+v", m.Notify)
	}
	a.Do("tap Badge")
	a.UntilText("badged")
	m1UntilState(t, a, func(m m1State) bool { return m.Notify.Badge == "3" })
}
