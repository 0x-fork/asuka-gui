//go:build darwin

package mactest

import (
	"testing"
)

// A PageControl draws its dots; a click on one chooses it (its page), the
// app's page moves the current one; VoiceOver hears it as a slider, "2/5".
func TestPageControl(t *testing.T) {
	a := Run(t, `
		component P {
		  state page = 0
		  Column {
		    padding: 10
		    align: .start
		    Text("page {page}")
		    Button("Next") { on press { page += 1 } }
		    PageControl(5) { bind page: page }
		  }
		}
		app { P() }
	`)
	tr := a.UntilText("page 0")
	pc := tr.Kind("PageControl")[0]
	dots, _ := pc.Get("dots").(map[string]any)
	if dots["count"] != 5.0 || dots["page"] != 0.0 || dots["a11yValue"] != "1/5" || pc.Str("a11yRole") != "AXSlider" {
		t.Fatalf("the page control: %v %v", dots, pc.Str("a11yRole"))
	}
	if pc.Frame[2] != 5*14+14 || pc.Frame[3] != 24 {
		t.Fatalf("its frame %v", pc.Frame)
	}
	a.Do("dot 3")
	a.UntilText("page 3")
	a.Do("tap Next")
	a.UntilText("page 4")
	a.Until(func(tr *Tree) bool {
		pc := tr.Kind("PageControl")[0]
		d, _ := pc.Get("dots").(map[string]any)
		return d["page"] == 4.0 && d["a11yValue"] == "5/5"
	})
}

// A paging Scroll hides its scroll bars and, as a live scroll ends,
// settles on the nearest page and reports it.
func TestPagingScroll(t *testing.T) {
	a := Run(t, `
		component P {
		  state page = 0
		  Column {
		    align: .start
		    Text("page {page}")
		    Scroll {
		      horizontal: true
		      paging: true
		      bind page: page
		      width: 200
		      height: 100
		      for i in 0..<4 key i { View { width: 200; height: 100; Text("Page {i}") } }
		    }
		  }
		}
		app { P() }
	`)
	tr := a.UntilText("page 0")
	s := tr.Kind("Scroll")[0]
	if s.Get("paging") != true {
		t.Fatalf("not paging: %v", s.Props)
	}
	a.Do("scrollx 230", "endscroll")
	a.UntilText("page 1")
	a.Until(func(tr *Tree) bool { return tr.FindText("Page 1").InWindow[0] == tr.Kind("Scroll")[0].InWindow[0] })
	a.Do("scrollx 480", "endscroll")
	a.UntilText("page 2")
}

// Pulled past its top beyond 60 points and let go, a Scroll refreshes, as a
// List does: a spinner above the content until refreshing goes false.
func TestScrollPullToRefresh(t *testing.T) {
	a := Run(t, `
		component R {
		  state refreshing = false
		  state count = 0
		  Column {
		    grow: 1
		    Text("refreshed {count}")
		    Button("Done") { on press { refreshing = false } }
		    Scroll {
		      grow: 1
		      refreshing: refreshing
		      on refresh {
		        refreshing = true
		        count += 1
		      }
		      Text("content")
		    }
		  }
		}
		app { R() }
	`)
	a.UntilText("content")
	a.Do("pull 80")
	a.UntilText("refreshed 1")
	a.Until(func(tr *Tree) bool {
		r, _ := tr.Kind("Scroll")[0].Get("refresh").(map[string]any)
		return r["refreshing"] == true && r["insetTop"] == 40.0 && r["spinner"] == true
	})
	a.Do("tap Done")
	a.Until(func(tr *Tree) bool {
		r, _ := tr.Kind("Scroll")[0].Get("refresh").(map[string]any)
		return r["refreshing"] == false && r["insetTop"] == 0.0
	})
}
