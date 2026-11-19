package std

import (
	"os"
	"strings"
	"testing"
)

// std/http's TLS settings are std/websocket's too, and so is the code
// that applies them on Apple's systems and Android: the same Swift file
// in both packages, the same Kotlin file but for its package line.
func TestTLSCopiesAlike(t *testing.T) {
	for _, c := range []struct{ a, b, pkgA, pkgB string }{
		{"http/swift/TLS.swift", "websocket/swift/TLS.swift", "", ""},
		{"http/android/src/main/kotlin/std/http/TLS.kt", "websocket/android/src/main/kotlin/std/websocket/TLS.kt", "package std.http\n", "package std.websocket\n"},
	} {
		a, err := os.ReadFile(c.a)
		if err != nil {
			t.Fatal(err)
		}
		b, err := os.ReadFile(c.b)
		if err != nil {
			t.Fatal(err)
		}
		if strings.Replace(string(a), c.pkgA, c.pkgB, 1) != string(b) {
			t.Errorf("%s isn't %s (copy it over)", c.b, c.a)
		}
	}
}
