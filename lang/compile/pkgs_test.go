package compile

import (
	"strings"
	"testing"
)

func TestPackages(t *testing.T) {
	res, err := Dir("testdata/pkgs/app", nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	out, err := Run(testRunner(t), res.Module, true, "")
	want := "#1 a\nHI!\n1\nok packages"
	if strings.TrimSpace(out) != want || err != nil {
		t.Fatalf("got (%v):\n%s\nwant:\n%s", err, out, want)
	}
}

func TestPackageErrors(t *testing.T) {
	res := Files(map[string][]byte{"a.asuka": []byte(`
import ./missing
import asuka/nope
`)}, nil)
	got := res.Err()
	if got == nil || !strings.Contains(got.Error(), "can't read package ./missing") || !strings.Contains(got.Error(), "no package asuka/nope") {
		t.Fatalf("got %v", got)
	}
	res2, err := Dir("testdata/pkgs/app", nil)
	if err != nil {
		t.Fatal(err)
	}
	_ = res2
}
