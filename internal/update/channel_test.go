package update

import (
	"testing"
)

// Semantic versioning's own example of precedence (semver.org, 11.4).
func TestCompareSemverPrecedence(t *testing.T) {
	order := []string{"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0-beta.1", "1.1.0", "2.0.0"}
	for i := range order {
		for j := range order {
			want := 0
			switch {
			case i < j:
				want = -1
			case i > j:
				want = 1
			}
			if got := Compare(order[i], order[j]); got != want {
				t.Errorf("Compare(%q, %q) = %d, want %d", order[i], order[j], got, want)
			}
		}
	}
}

func TestChannel(t *testing.T) {
	for v, want := range map[string]string{
		"1.2.0": "", "v1.2.0": "", "1.2.0+42": "", "1.3.0-beta.2": "beta", "1.3.0-Beta2": "beta",
		"2.0.0-rc.1": "rc", "1.3.0-1": "pre", "1.3.0-nightly.20260101+abc": "nightly",
	} {
		if got := Channel(v); got != want {
			t.Errorf("Channel(%q) = %q, want %q", v, got, want)
		}
	}
	if got := ManifestName("darwin-arm64", ""); got != "update-darwin-arm64.json" {
		t.Error(got)
	}
	if got := ManifestName("windows-arm64", "beta"); got != "update-windows-arm64-beta.json" {
		t.Error(got)
	}
}

func TestValidVersion(t *testing.T) {
	for v, want := range map[string]bool{
		"1.0": true, "1.2.3": true, "v1.2.3": true, "1.3.0-beta.1": true, "1.3.0-beta.1+42": true, "1.0.0-x-y": true,
		"": false, "1..2": false, "1.2.x": false, "1.2.3-": false, "1.2.3-beta..1": false, "1.2.3+": false, "1.2.3-b@d": false, "latest": false,
	} {
		if got := ValidVersion(v); got != want {
			t.Errorf("ValidVersion(%q) = %t, want %t", v, got, want)
		}
	}
}

// An app on a channel takes releases and that channel's prereleases, and
// reads a prerelease's channel manifest.
