package appbuild

import (
	"os"
	"path/filepath"
	"testing"
)

// usesTestApp writes an app that calls asuka/location's current() and
// asuka/icloud's remove(), with a local function named delete, and a
// dependency (@test/sync) that calls location.requestAlways and
// icloud.readFile; it returns the app's directory.
func usesTestApp(t *testing.T) string {
	t.Helper()
	root := t.TempDir()
	app, sync := filepath.Join(root, "app"), filepath.Join(root, "sync")
	os.MkdirAll(app, 0o755)
	os.MkdirAll(sync, 0o755)
	write := func(path, s string) {
		if err := os.WriteFile(path, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write(filepath.Join(app, "app.asuka"), `import asuka/icloud
import asuka/location
import asuka/notify
import @test/sync

async fn clear() {
  let _ = try? await icloud.remove("k")
  let _ = try? await location.current()
  let n = notify.Notification(body: "hi").from(notify.Person(name: "Ann"), notify.Conversation())
  let m = notify.Notification(sender: notify.Person(name: "Bo"))
  try? await notify.show(m)
  try? await notify.show(n)
}

fn delete(x: string) -> string = x

test "a test's calls aren't the app's" {
  let _ = try? await icloud.documents()
}

app { Text(delete(sync.name())) }
`)
	write(filepath.Join(app, "asuka.toml"), "id = \"dev.asuka.journal\"\n\n[dependencies]\n\"@test/sync\" = \"file:../sync\"\n")
	write(filepath.Join(sync, "sync.asuka"), `import asuka/icloud
import asuka/location

pub fn name() -> string = "sync"

pub async fn pull() throws -> bytes {
  let _ = try await location.requestAlways()
  return try await icloud.readFile("notes.txt")
}
`)
	write(filepath.Join(sync, "asuka.toml"), "name = \"@test/sync\"\nversion = \"0.1.0\"\n")
	return app
}

// Uses is what the checker resolved, across the app and its
// dependencies: the members referred to from outside their packages, not
// every identifier (the app's own delete isn't asuka/icloud's), and not a
// test block's.
func TestUses(t *testing.T) {
	p, err := Load(usesTestApp(t))
	if err != nil {
		t.Fatal(err)
	}
	u := p.Uses()
	for _, imp := range []string{"asuka/icloud", "asuka/location", "@test/sync"} {
		if !u.Import(imp) {
			t.Errorf("no import %s: %v", imp, u.ImportList())
		}
	}
	for _, r := range [][2]string{{"asuka/icloud", "remove"}, {"asuka/location", "current"}, {"@test/sync", "name"},
		{"asuka/location", "requestAlways"}, {"asuka/icloud", "readFile"}, {"", "Text"},
		{"asuka/notify", "Notification.from"}, {"asuka/notify", "Person"}, {"asuka/notify", "Notification.sender"},
		{"asuka/notify", "Notification.body"}, {"asuka/notify", "Person.name"}} {
		if !u.Ref(r[0], r[1]) {
			t.Errorf("no %s.%s in %v", r[0], r[1], u.Refs)
		}
	}
	for _, r := range [][2]string{{"asuka/icloud", "delete"}, {"asuka/icloud", "documents"}, {"", "delete"}, {"", "clear"},
		{"asuka/notify", "Notification.title"}, {"asuka/notify", "Person.bot"}} {
		if u.Ref(r[0], r[1]) {
			t.Errorf("%s.%s in %v", r[0], r[1], u.Refs)
		}
	}
	// What std's packages call of each other counts as theirs: asuka/icloud
	// calls asuka/platform.
	if !u.Ref("asuka/platform", "call") {
		t.Errorf("no asuka/platform.call in %v", u.Refs)
	}
	if std := p.Std(); std["asuka/icloud"] == "" || std["asuka/platform"] == "" {
		t.Errorf("std %v", std)
	}
}
