package pkgs

import "testing"

func TestRanges(t *testing.T) {
	for _, c := range []struct {
		r, v string
		ok   bool
	}{
		{"^1.2.0", "1.2.0", true}, {"^1.2.0", "1.9.9", true}, {"^1.2.0", "2.0.0", false}, {"^1.2.0", "1.1.9", false},
		{"^0.2.3", "0.2.9", true}, {"^0.2.3", "0.3.0", false}, {"^0.0.3", "0.0.4", false},
		{"~1.2.0", "1.2.9", true}, {"~1.2.0", "1.3.0", false}, {"~1", "1.9.0", true},
		{"1.2.3", "1.2.3", true}, {"1.2.3", "1.2.4", false}, {"1.x", "1.5.0", true}, {"1.x", "2.0.0", false},
		{">=1.0.0 <2.0.0", "1.5.0", true}, {">=1.0.0 <2.0.0", "2.0.0", false},
		{"1.0.0 - 1.2", "1.2.9", true}, {"1.0.0 - 1.2", "1.3.0", false},
		{"^1.0.0 || ^2.0.0", "2.1.0", true}, {"*", "3.1.0", true},
		{"^1.2.0", "1.3.0-beta.1", false}, {"^1.3.0-beta.1", "1.3.0-beta.2", true}, {"^1.3.0-beta.1", "1.3.0", true},
		{">1.2", "1.3.0", true}, {">1.2", "1.2.9", false}, {"<=1.2", "1.2.9", true},
	} {
		r, err := ParseRange(c.r)
		if err != nil {
			t.Fatalf("%s: %v", c.r, err)
		}
		v, err := ParseVersion(c.v)
		if err != nil {
			t.Fatal(err)
		}
		if got := r.Match(v); got != c.ok {
			t.Errorf("%s matches %s: %v, want %v", c.r, c.v, got, c.ok)
		}
	}
}

func TestCompare(t *testing.T) {
	order := []string{"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"}
	for i := 1; i < len(order); i++ {
		a, _ := ParseVersion(order[i-1])
		b, _ := ParseVersion(order[i])
		if a.Compare(b) >= 0 {
			t.Errorf("%s < %s", a, b)
		}
	}
}
