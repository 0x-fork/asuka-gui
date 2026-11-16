package main

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

// sheetApp is an app with a share target, whose program uses its own
// share sheet and a push's rewrite (with: the source refers to
// sharesheet.active and push.rewrite), or imports both packages without
// those members.
func sheetApp(t *testing.T, with bool) string {
	t.Helper()
	name := "Plain"
	src := `import asuka/sharesheet
import asuka/push

component Root {
  task { let _ = try? await push.register() }
  Button("Done") { on press { sharesheet.done() } }
}

app { Root() }
`
	if with {
		name = "Pins"
		src = `import asuka/sharesheet
import asuka/push

async fn decrypt(m: push.Message) throws -> push.Message? {
  var out = m
  out.title = "Ana"
  return out
}

component Save {
  state link = ""
  onShare(items => { link = items[0].text })
  Column {
    Text(link)
    Button("Save") { on press { sharesheet.done() } }
  }
}

component Root {
  task { push.rewrite(decrypt) }
  Text("Pins")
}

app {
  if sharesheet.active() { Save() } else { Root() }
}
`
	}
	dir := filepath.Join(t.TempDir(), strings.ToLower(name))
	os.MkdirAll(dir, 0o755)
	os.WriteFile(filepath.Join(dir, "asuka.toml"), []byte("name = \""+name+"\"\nid = \"dev.asuka."+strings.ToLower(name)+"\"\nshareTarget = true\n"), 0o644)
	os.WriteFile(filepath.Join(dir, "app.asuka"), []byte(src), 0o644)
	return dir
}

// What the app's program uses of asuka/sharesheet and asuka/push, as the
// checker resolved it, makes the CLI build their extensions: the share
// extension that runs the app as its sheet (Android: its Activity), and
// the Notification Service Extension (Android: the manifest's
// meta-data). Importing the packages alone makes neither.
func TestExtensionsFromUses(t *testing.T) {
	app := projectApp(t, sheetApp(t, true), nil)
	c := app.cfg
	if !c.shareSheet || !c.pushRewrite || !c.sharedLayout() {
		t.Fatalf("share sheet %v, rewrite %v, shared layout %v", c.shareSheet, c.pushRewrite, c.sharedLayout())
	}
	if p := c.shareInfo(nil)["NSExtension"].(map[string]any)["NSExtensionPrincipalClass"]; p != "AsukaShareSheetController" {
		t.Errorf("the share extension's principal %v", p)
	}
	man := c.androidManifest(false)
	if _, sheet, ok := strings.Cut(man, "dev.asuka.AsukaShareActivity"); !ok || !strings.Contains(sheet, "action.SEND") ||
		!strings.Contains(man, `<meta-data android:name="dev.asuka.push.rewrite" android:value="true" />`) {
		t.Errorf("manifest:\n%s", man)
	}

	plain := projectApp(t, sheetApp(t, false), nil).cfg
	if plain.shareSheet || plain.pushRewrite || plain.sharedLayout() {
		t.Fatalf("without the members: share sheet %v, rewrite %v", plain.shareSheet, plain.pushRewrite)
	}
	if p := plain.shareInfo(nil)["NSExtension"].(map[string]any)["NSExtensionPrincipalClass"]; p != "AsukaShareViewController" {
		t.Errorf("the share extension's principal %v", p)
	}
	man = plain.androidManifest(false)
	if strings.Contains(man, "AsukaShareActivity") || strings.Contains(man, "dev.asuka.push.rewrite") || !strings.Contains(man, "action.SEND") {
		t.Errorf("manifest without a sheet or a rewrite:\n%s", man)
	}
}

// A simulator build of an app with a share sheet and a rewrite has both
// extensions, linking the framework with the app's code and the host; one
// without has the share extension that opens the app, and no
// Notification Service Extension. Nothing is installed.
func TestBuildIOSExtensions(t *testing.T) {
	if runtime.GOOS != "darwin" || testing.Short() {
		t.Skip("Xcode, on a Mac (not -short)")
	}
	if _, err := cbuild.ForTarget(cbuild.Target{OS: "ios-simulator", Arch: runtime.GOARCH}); err != nil {
		t.Skip(err)
	}
	principal := func(appex string) string {
		out, err := exec.Command("plutil", "-extract", "NSExtension.NSExtensionPrincipalClass", "raw", "-o", "-", filepath.Join(appex, "Info.plist")).Output()
		if err != nil {
			t.Fatalf("%s: %v", appex, err)
		}
		return strings.TrimSpace(string(out))
	}
	for _, with := range []bool{true, false} {
		app := projectApp(t, sheetApp(t, with), nil)
		if work, err := workDir(app, "x"); err == nil {
			t.Cleanup(func() { os.RemoveAll(filepath.Dir(work)) })
		}
		out := t.TempDir()
		if err := buildIOS(app, &buildOptions{out: out, simulator: true}); err != nil {
			t.Fatal(err)
		}
		bundle := filepath.Join(out, safeName(app.cfg.Name)+".app")
		share := filepath.Join(bundle, "PlugIns", shareExe+".appex")
		notification := filepath.Join(bundle, "PlugIns", notificationExe+".appex")
		_, nerr := os.Stat(notification)
		_, ferr := os.Stat(filepath.Join(bundle, "Frameworks", fwName+".framework", fwName))
		if !with {
			if p := principal(share); p != "AsukaShareViewController" || nerr == nil || ferr == nil {
				t.Errorf("without: share principal %s, notification extension %v, framework %v", p, nerr, ferr)
			}
			continue
		}
		if p := principal(share); p != "AsukaShareSheetController" {
			t.Errorf("share principal %s", p)
		}
		if nerr != nil || ferr != nil {
			t.Fatalf("notification extension %v, framework %v", nerr, ferr)
		}
		if p := principal(notification); p != "AsukaNotificationService" {
			t.Errorf("notification principal %s", p)
		}
		for _, exe := range []string{filepath.Join(share, shareExe), filepath.Join(notification, notificationExe)} {
			libs, err := exec.Command("xcrun", "otool", "-L", exe).Output()
			if err != nil || !strings.Contains(string(libs), "@rpath/"+fwName+".framework/"+fwName) {
				t.Errorf("%s doesn't link the framework: %v\n%s", exe, err, libs)
			}
		}
		syms, err := exec.Command("xcrun", "nm", "-gU", filepath.Join(bundle, "Frameworks", fwName+".framework", fwName)).Output()
		if err != nil {
			t.Fatal(err)
		}
		for _, want := range []string{"_asuka_run", "AsukaShareSheetController", "AsukaNotificationService"} {
			if !strings.Contains(string(syms), want) {
				t.Errorf("the framework has no %s", want)
			}
		}
	}
}
