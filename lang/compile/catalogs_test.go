package compile

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
)

var testCatalogs = fstest.MapFS{
	"en.json": {Data: []byte(`{
  "@@locale": "en",
  "inbox": "Inbox",
  "greeting": "Hello, {name}!",
  "unread": "{count, plural, one {# unread} other {# unread}}",
  "@unread": {"placeholders": {"count": {"type": "int"}}},
  "storage": "{used, number, percent} used",
  "@storage": {"placeholders": {"used": {"type": "double"}}},
  "seen": "Seen {when, date, short}",
  "guests": "{host} and {n, plural, one {# other} other {# others}}"
}`)},
	"de.json": {Data: []byte(`{
  "@@locale": "de",
  "inbox": "Posteingang",
  "greeting": "Hallo, {name}!"
}`)},
}

// compileWith compiles src, an app, with catalogs.
func compileWith(t *testing.T, cats fstest.MapFS, src string) *Result {
	t.Helper()
	return Files(map[string][]byte{"app.asuka": []byte(src)}, &Options{
		Catalogs: &Catalogs{FS: cats, Dir: "locales", Source: "en"},
	})
}

func TestTrChecks(t *testing.T) {
	ok := compileWith(t, testCatalogs, `
component Inbox {
  state n = 2
  Column {
    Text(tr.inbox())
    Text(tr.greeting(name: "Ada"))
    Text(tr.unread(count: n))
    Text(tr.storage(used: 0.5))
    Text(tr.storage(used: n))
    Text(tr.seen(when: 0))
    Text(tr.guests(host: "Ada", n: n))
    Text(tr.greeting("Grace"))
  }
}
app { Inbox() }
`)
	if err := ok.Err(); err != nil {
		t.Fatalf("a correct app doesn't compile:\n%v", err)
	}
	// The catalog's warnings: German lacks messages.
	if len(ok.Warnings) == 0 || !strings.Contains(ok.Warnings[0].String(), "locales/de.json") || !strings.Contains(ok.Warnings[0].Msg, "missing") {
		t.Errorf("warnings: %v", ok.Warnings)
	}
	for _, c := range []struct{ src, want string }{
		{`Text(tr.inbx())`, "app.asuka:3:13: locales/en.json has no message inbx (did you mean tr.inbox?)"},
		{`Text(tr.greeting())`, "tr.greeting needs its argument name (a string)"},
		{`Text(tr.greeting(nam: "x"))`, "tr.greeting has no argument nam: it takes tr.greeting(name:)"},
		{`Text(tr.unread(count: "3"))`, "tr.unread's count is a string, not a int"},
		{`Text(tr.unread(count: 1.5))`, "tr.unread's count"},
		{`Text(tr.storage(used: "x"))`, "tr.storage's used is a number (double), not a string"},
		{`Text(tr.seen(when: "today"))`, "tr.seen's when is a date"},
		{`Text(tr.inbox(x: 1))`, "tr.inbox takes no arguments"},
		{`Text(tr.guests("Ada", 2))`, "tr.guests's arguments are named after the message's: tr.guests(host: n:)"},
		{`Text(tr.greeting(name: "a", name: "b"))`, "tr.greeting's name is given twice"},
		{`Text(tr)`, "tr says the app's messages: call one, tr.key(…)"},
	} {
		res := compileWith(t, testCatalogs, "component A {\n  Column {\n    "+c.src+"\n  }\n}\napp { A() }\n")
		err := res.Err()
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: got %v, want %q", c.src, err, c.want)
		}
	}
}

// A message read where it's said once doesn't follow, and spawn can't say
// one: the language is the main thread's.
func TestTrIsMainThreads(t *testing.T) {
	res := compileWith(t, testCatalogs, `
fn label() -> string = tr.inbox()
async fn f() -> string {
  return await spawn { label() }
}
app { Text("x") }
`)
	if err := res.Err(); err == nil || !strings.Contains(err.Error(), "spawn calls label, which reads state") {
		t.Errorf("got %v", err)
	}
}

// What's wrong in a catalog is an error at its file and line.
func TestCatalogDiagnostics(t *testing.T) {
	for _, c := range []struct {
		files fstest.MapFS
		want  string
	}{
		{fstest.MapFS{"en.json": {Data: []byte("{\n  \"a\": \"{n, plural, one {x}}\"\n}")}}, `locales/en.json:2:1: "a" does not parse`},
		{fstest.MapFS{"en.json": {Data: []byte(`{"a": "A"}`)}, "de.json": {Data: []byte("{\n  \"a\": \"A\",\n  \"b\": \"B\"\n}")}}, `locales/de.json:3:1: "b" is not a key of locales/en.json`},
		{fstest.MapFS{"en.json": {Data: []byte(`{"a": "{x}"}`)}, "de.json": {Data: []byte(`{"a": "{y}"}`)}}, `locales/de.json:1:1: "a" uses the argument y, but the source message has the arguments x`},
		{fstest.MapFS{"en.json": {Data: []byte(`{"a": "{n, plural, other {#}}", "@a": {"placeholders": {"n": {"type": "String"}}}}`)}}, "uses n as a plural argument, which its type String cannot be"},
		{fstest.MapFS{"de.json": {Data: []byte(`{"a": "A"}`)}}, "locales/en.json:1:1: missing"},
		{fstest.MapFS{"en.json": {Data: []byte(`{"a": "A"}`)}, "EN-us.json": {Data: []byte(`{}`)}}, "rename it en-US.json"},
		{fstest.MapFS{"en.json": {Data: []byte(`{"asuka.nope": "x"}`)}}, "asuka has no message nope"},
	} {
		res := compileWith(t, c.files, "app { Text(\"x\") }\n")
		if err := res.Err(); err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("got %v, want %q", err, c.want)
		}
	}
	// A broken catalog's tr calls aren't errors of their own.
	res := compileWith(t, fstest.MapFS{"en.json": {Data: []byte(`{"a": "{"}`)}}, "app { Text(tr.a()) }\n")
	if err := res.Err(); err == nil || strings.Count(err.Error(), "\n") != 0 {
		t.Errorf("got %v, want the catalog's one error", err)
	}
}

// Dir finds the catalogs: asuka.toml's [i18n], else locales/.
func TestCatalogsOf(t *testing.T) {
	dir := t.TempDir()
	write := func(name, s string) {
		p := filepath.Join(dir, filepath.FromSlash(name))
		os.MkdirAll(filepath.Dir(p), 0o755)
		if err := os.WriteFile(p, []byte(s), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("app.asuka", "app { Text(tr.hi()) }\n")
	if res, err := Dir(dir, nil); err != nil || res.Err() == nil || !strings.Contains(res.Err().Error(), "unknown name tr") {
		t.Fatalf("no catalogs: %v %v", err, res.Err())
	}
	write("locales/en.json", `{"hi": "Hi"}`)
	if res, err := Dir(dir, nil); err != nil || res.Err() != nil {
		t.Fatalf("locales/: %v %v", err, res.Err())
	}
	write("asuka.toml", "[i18n]\ndir = \"strings\"\nsource = \"ja\"\n")
	write("strings/ja.json", `{"hi": "こんにちは"}`)
	res, err := Dir(dir, nil)
	if err != nil || res.Err() != nil {
		t.Fatalf("asuka.toml: %v %v", err, res.Err())
	}
	if res.Program.Catalog == nil || res.Program.Catalog.Source != "ja" {
		t.Errorf("catalog: %+v", res.Program.Catalog)
	}
	write("app.asuka", "app { Text(tr.bye()) }\n")
	res, _ = Dir(dir, nil)
	if err := res.Err(); err == nil || !strings.Contains(err.Error(), "strings/ja.json has no message bye") {
		t.Errorf("got %v", err)
	}
}

// An app without catalogs that formats numbers and dates gets English's
// formats, and no tr.
func TestImplicitCatalog(t *testing.T) {
	res := Files(map[string][]byte{"app.asuka": []byte("import std/i18n\napp { Text(i18n.formatNumber(1.5)) }\n")}, nil)
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	if c := res.Program.Catalog; c == nil || !c.Implicit {
		t.Fatalf("catalog: %+v", c)
	}
	res = Files(map[string][]byte{"app.asuka": []byte("import std/i18n\napp { Text(tr.x()) }\n")}, nil)
	if err := res.Err(); err == nil || !strings.Contains(err.Error(), "unknown name tr") {
		t.Errorf("got %v", err)
	}
}

// asuka dev's --lang and --dir are compiled into the module: whatever the
// user reads, the run shows them, and setLocale can't change it.
func TestDevLocale(t *testing.T) {
	res := Files(map[string][]byte{"app.asuka": []byte(`import std/i18n
component A {
  Column {
    Text(tr.inbox())
    Text("{i18n.locale()} rtl={i18n.isRTL()}")
  }
}
app { A() }
test "the run's language" {
  let app = mount(A())
  app.expect("Posteingang")
  app.expect("de rtl=true")
  app.setLanguages("en")
  app.expect("de rtl=true")
}
`)}, &Options{Catalogs: &Catalogs{FS: testCatalogs, Dir: "locales", Source: "en"}, Lang: "de", Direction: "rtl"})
	if err := res.Err(); err != nil {
		t.Fatal(err)
	}
	out, err := runBoth(t, res.Module)
	if err != nil || strings.Contains(out, "FAIL") {
		t.Fatalf("%v\n%s", err, out)
	}
}
