//go:build e2e

package e2e

// SQLite in a release build: linked into the app (std/sqlite's C), with
// no library of its own beside it, and the libraries there aligned as the
// stores want them. The debug builds the other tests run (TestNotesIOS,
// TestNotesAndroid) link it the same way; a release has no automation to
// drive, so these check what's built, and on iOS that the app opens its
// database.

import (
	"archive/zip"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"
)

// TestSQLiteReleaseIOS builds examples/notes for the simulator as a
// release: SQLite in the app's executable, no framework of its own, and
// the app opens notes.db.
func TestSQLiteReleaseIOS(t *testing.T) {
	if _, err := exec.LookPath("xcrun"); err != nil {
		t.Skip("no Xcode")
	}
	out := t.TempDir()
	build(t, "--platform", "ios", "--simulator", "--out", out, "examples/notes")
	app := filepath.Join(out, "Notes.app")
	if fws, _ := filepath.Glob(filepath.Join(app, "Frameworks", "*sqlite*")); len(fws) > 0 {
		t.Errorf("SQLite beside the app: %v", fws)
	}
	if b, err := os.ReadFile(filepath.Join(app, "Notes")); err != nil || !strings.Contains(string(b), "SQLite format 3\x00") {
		t.Errorf("no SQLite in the app's executable (%v)", err)
	}
	run(t, "codesign", "--verify", "--deep", "--strict", app)

	udid := bootedIPhone(t)
	run(t, "xcrun", "simctl", "install", udid, app)
	launched := run(t, "xcrun", "simctl", "launch", "--terminate-running-process", udid, "dev.asuka.notes")
	m := regexp.MustCompile(`: (\d+)\s*$`).FindStringSubmatch(launched)
	if m == nil {
		t.Fatalf("no pid in %q", launched)
	}
	// The simulator's apps are this Mac's processes: what it has open
	// says it opened its database.
	deadline := time.Now().Add(within)
	var files string
	for time.Now().Before(deadline) {
		b, _ := exec.Command("lsof", "-p", m[1]).Output()
		files = string(b)
		if strings.Contains(files, "notes.db") {
			return
		}
		time.Sleep(200 * time.Millisecond)
	}
	t.Fatalf("the app hasn't opened notes.db; it has open:\n%s", files)
}

// TestSQLiteReleaseAndroid builds examples/notes's .aab: libasuka.so
// with SQLite in it for every ABI, no library of SQLite's beside it; the
// APK made from it (bundletool's, as Google Play makes a device's) has
// its libraries uncompressed, on 16 KB boundaries, so Android maps them
// from the APK.
func TestSQLiteReleaseAndroid(t *testing.T) {
	keytool := androidTool("keytool")
	if keytool == "" {
		t.Skip("no keytool")
	}
	out := t.TempDir()
	keystore := filepath.Join(out, "upload.jks")
	run(t, keytool, "-genkeypair", "-keystore", keystore, "-alias", "upload", "-keyalg", "RSA", "-keysize", "2048",
		"-validity", "1", "-storepass", "e2e-pass", "-keypass", "e2e-pass", "-dname", "CN=e2e")
	t.Setenv("ASUKA_KEYSTORE", keystore)
	t.Setenv("ASUKA_KEY_ALIAS", "upload")
	t.Setenv("ASUKA_KEYSTORE_PASSWORD", "e2e-pass")
	build(t, "--platform", "android", "--out", out, "examples/notes")
	aab := filepath.Join(out, "Notes.aab")
	abis := []string{"arm64-v8a", "armeabi-v7a", "x86_64"}
	names := zipNames(t, aab)
	for _, abi := range abis {
		if !names["base/lib/"+abi+"/libasuka.so"] {
			t.Errorf("the .aab has no base/lib/%s/libasuka.so", abi)
		}
		if names["base/lib/"+abi+"/libasqlite.so"] {
			t.Errorf("the .aab has a base/lib/%s/libasqlite.so: SQLite is in libasuka.so", abi)
		}
	}

	tool := bundletool()
	if tool == nil {
		t.Skip("no bundletool (ASUKA_BUNDLETOOL: its bundletool-all jar)")
	}
	apks := filepath.Join(out, "notes.apks")
	run(t, tool[0], append(tool[1:], "build-apks", "--mode=universal", "--bundle="+aab, "--output="+apks,
		"--ks="+keystore, "--ks-key-alias=upload", "--ks-pass=pass:e2e-pass")...)
	z, err := zip.OpenReader(apks)
	if err != nil {
		t.Fatal(err)
	}
	defer z.Close()
	apk := filepath.Join(out, "universal.apk")
	for _, f := range z.File {
		if f.Name == "universal.apk" {
			r, err := f.Open()
			if err != nil {
				t.Fatal(err)
			}
			data, err := io.ReadAll(r)
			r.Close()
			if err != nil {
				t.Fatal(err)
			}
			os.WriteFile(apk, data, 0o644)
		}
	}
	a, err := zip.OpenReader(apk)
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	found := 0
	for _, f := range a.File {
		if !strings.HasPrefix(f.Name, "lib/") || !strings.HasSuffix(f.Name, ".so") {
			continue
		}
		if strings.HasSuffix(f.Name, "/libasuka.so") {
			found++
			r, err := f.Open()
			if err != nil {
				t.Fatal(err)
			}
			b, _ := io.ReadAll(r)
			r.Close()
			if !strings.Contains(string(b), "SQLite format 3\x00") {
				t.Errorf("%s: no SQLite in it", f.Name)
			}
		}
		off, err := f.DataOffset()
		if err != nil {
			t.Fatal(err)
		}
		if f.Method != zip.Store || off%16384 != 0 {
			t.Errorf("%s: method %d, at %d: Android maps a library from the APK only stored, on a 16 KB boundary", f.Name, f.Method, off)
		}
	}
	if found != len(abis) {
		t.Errorf("the APK has %d libasuka.so, want %d", found, len(abis))
	}
}

// build runs asuka build with args.
func build(t *testing.T, args ...string) {
	t.Helper()
	c := exec.Command("go", append([]string{"run", "./cmd/asuka", "build"}, args...)...)
	c.Dir = root(t)
	if out, err := c.CombinedOutput(); err != nil {
		t.Fatalf("asuka build %s: %v\n%s", strings.Join(args, " "), err, out)
	}
}

// bootedIPhone is ASUKA_DEVICE, or a booted iPhone simulator's UDID.
func bootedIPhone(t *testing.T) string {
	if d := os.Getenv("ASUKA_DEVICE"); d != "" {
		return d
	}
	var list struct {
		Devices map[string][]struct{ Name, UDID string }
	}
	if err := json.Unmarshal([]byte(run(t, "xcrun", "simctl", "list", "devices", "booted", "-j")), &list); err != nil {
		t.Fatal(err)
	}
	for _, ds := range list.Devices {
		for _, d := range ds {
			if strings.HasPrefix(d.Name, "iPhone") {
				return d.UDID
			}
		}
	}
	t.Skip("no booted iPhone simulator")
	return ""
}

func zipNames(t *testing.T, path string) map[string]bool {
	t.Helper()
	z, err := zip.OpenReader(path)
	if err != nil {
		t.Fatal(err)
	}
	defer z.Close()
	names := map[string]bool{}
	for _, f := range z.File {
		names[f.Name] = true
	}
	return names
}

// bundletool is the command that runs bundletool: ASUKA_BUNDLETOOL (its
// bundletool-all jar), or bundletool on the PATH (Homebrew's); nil if
// neither.
func bundletool() []string {
	if jar := os.Getenv("ASUKA_BUNDLETOOL"); jar != "" {
		return []string{"java", "-jar", jar}
	}
	if p, err := exec.LookPath("bundletool"); err == nil {
		return []string{p}
	}
	return nil
}
