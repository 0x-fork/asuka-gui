package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"html"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"sort"
	"strings"
	"time"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/bytecode"
	iosHost "github.com/egoist/asuka/platform/ios"
)

// iOS: the Swift host (platform/ios, compiled once and cached) linked
// with the C core (libasuka.a) into an .app. A release's core has the
// app compiled into it (asuka_run); asuka dev's is the player, the core
// without an app, which loads the bytecode asuka dev sends it and again
// on each save, keeping its state (asuka_dev_connect, ASUKA_DEV in its
// environment): it's built and installed once, and again only when the
// app's settings or native code change.

const iosMin = "16.0"

type iosToolchain struct {
	sdk, sdkName, clang, target, goarch string
	release                             bool // optimized, without the debug automation
	verbose                             bool
	// packages is what the app's Swift packages bring besides the host's
	// object, as the last buildHost built them (nil without any).
	packages *swiftPackageOutput
	// cLink is what the app's C ([c]'s libraries and frameworks) links,
	// as the last buildCore built it: the executable's link adds it.
	cLink []string
}

func newIOSToolchain(verbose bool) (*iosToolchain, error) {
	sdk, err := output("xcrun", "--sdk", "iphonesimulator", "--show-sdk-path")
	if err != nil {
		return nil, fmt.Errorf("no iOS simulator SDK (install Xcode): %v", err)
	}
	clang, err := output("xcrun", "--sdk", "iphonesimulator", "--find", "clang")
	if err != nil {
		return nil, err
	}
	arch, goarch := "arm64", "arm64"
	if runtime.GOARCH == "amd64" {
		arch, goarch = "x86_64", "amd64"
	}
	return &iosToolchain{sdk: sdk, sdkName: "iphonesimulator", clang: clang, target: arch + "-apple-ios" + iosMin + "-simulator", goarch: goarch, verbose: verbose}, nil
}

func output(name string, args ...string) (string, error) {
	out, err := exec.Command(name, args...).Output()
	if err != nil {
		if ee, ok := err.(*exec.ExitError); ok {
			return "", fmt.Errorf("%s: %v: %s", name, err, bytes.TrimSpace(ee.Stderr))
		}
		return "", err
	}
	return strings.TrimSpace(string(out)), nil
}

func (t *iosToolchain) run(dir string, env []string, name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Dir = dir
	cmd.Env = append(os.Environ(), env...)
	if t.verbose {
		fmt.Fprintln(os.Stderr, "+", strings.Join(append(env, append([]string{name}, args...)...), " "))
	}
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("%s failed:\n%s", name, filterLinker(out))
	}
	return nil
}

// filterLinker drops the linker's noise about SDK versions.
func filterLinker(out []byte) string {
	var keep []string
	for _, l := range strings.Split(string(out), "\n") {
		if strings.Contains(l, "Wincompatible-sysroot") || strings.HasPrefix(l, "ld: warning:") {
			continue
		}
		keep = append(keep, l)
	}
	return strings.TrimSpace(strings.Join(keep, "\n"))
}

// buildHost compiles the embedded Swift host, with the app's own Swift
// (its ios/*.swift: native views for ui.Native), into one object file,
// cached by the hash of its sources and the SDK.
func (t *iosToolchain) buildHost(work string, app *asukaApp, features []string) (string, error) {
	opt := []string{"-Onone", "-D", "DEBUG"}
	if t.release {
		// With debug information, for the app's dSYM (iosSymbols).
		opt = []string{"-O", "-g"}
	}
	h := sha256.New()
	fmt.Fprintln(h, t.sdk, t.target, t.release, features, opt)
	var files []string
	err := fs.WalkDir(iosHost.Files, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		if strings.HasPrefix(path, "Share/") || strings.HasPrefix(path, "Widget/") {
			return nil // the extensions, built on their own (buildShare, buildWidgets)
		}
		files = append(files, path)
		return nil
	})
	if err != nil {
		return "", err
	}
	sort.Strings(files)
	shared := filepath.Join(filepath.Dir(filepath.Dir(work)), "ios-host") // shared by all apps
	// Its sources are written again for each build: one at a time.
	unlock, err := lockShared(shared)
	if err != nil {
		return "", err
	}
	defer unlock()
	src := filepath.Join(shared, "src")
	os.RemoveAll(src)
	var swift []string
	for _, f := range files {
		data, _ := iosHost.Files.ReadFile(f)
		h.Write([]byte(f))
		h.Write(data)
		dst := filepath.Join(src, f)
		os.MkdirAll(filepath.Dir(dst), 0o755)
		if err := os.WriteFile(dst, data, 0o644); err != nil {
			return "", err
		}
		if strings.HasSuffix(f, ".swift") {
			swift = append(swift, dst)
		}
	}
	if app != nil {
		// The app's native packages' Swift: then the host's is a Swift
		// package, built with theirs; what they need goes in its Info.plist.
		natives, err := nativeFor(app, "ios")
		if err != nil {
			return "", err
		}
		if len(natives) > 0 {
			if err := app.cfg.nativeNeeds(natives, "ios"); err != nil {
				return "", err
			}
			return t.buildHostPackage(work, natives, features)
		}
	}
	t.packages = nil
	obj := filepath.Join(shared, hex.EncodeToString(h.Sum(nil))[:16]+".o")
	if _, err := os.Stat(obj); err == nil {
		return obj, nil
	}
	fmt.Println("asuka: compiling the iOS host (once)")
	args := append([]string{"swiftc", "-target", t.target, "-sdk", t.sdk, "-swift-version", "5",
		"-module-name", "AsukaApp", "-parse-as-library", "-wmo"}, opt...)
	for _, f := range features {
		args = append(args, "-D", f)
	}
	args = append(args, "-I", filepath.Join(src, "Sources", "CAsuka", "include"), "-c", "-o", obj)
	if err := t.run(shared, nil, "xcrun", append(args, swift...)...); err != nil {
		return "", err
	}
	return obj, nil
}

// buildHostPackage builds the host's Swift with the app's native
// packages' (native_swift.go) into one object, as buildHost's swiftc
// build is: the packages' objects linked together. What else they bring
// (static libraries, frameworks, resource bundles) is t.packages, which
// linkApp links and embedPlugins puts in the app.
func (t *iosToolchain) buildHostPackage(work string, natives []nativePackage, features []string) (string, error) {
	host, err := swiftSources(iosHost.Files, "Sources/AsukaHost", "Sources/AsukaOps", "App")
	if err != nil {
		return "", err
	}
	cAsuka, err := filesUnder(iosHost.Files, "Sources/CAsuka")
	if err != nil {
		return "", err
	}
	b := &swiftBuild{dir: filepath.Join(work, "swift-"+t.sdkName), platform: "ios", sdkName: t.sdkName,
		arch: macSwiftArch(t.goarch), release: t.release, defines: features, host: host, cAsuka: cAsuka, natives: natives, run: t.run}
	out, err := b.build()
	if err != nil {
		return "", err
	}
	obj := filepath.Join(b.dir, "AsukaApp-linked.o")
	args := append([]string{"--sdk", t.sdkName, "clang", "-target", t.target, "-r", "-o", obj}, out.objects...)
	if err := t.run(work, nil, "xcrun", args...); err != nil {
		return "", err
	}
	t.packages = out
	return obj, nil
}

// iosFeatures are the parts of the host that link a framework of their
// own (WebKit, AVKit...), and the function of the standard library (or
// the element) an app uses each through. Loading a framework costs at
// launch whether or not it's used (WebKit and AVKit's together, about a
// second on the simulator), so a release's host is built with only those
// its app uses: the packages it imports, whose functions its sources
// name (asukaApp.hasSymbol). asuka dev's player has them all. They're
// the core's elements and the host's own modules, which the packages over
// asuka/host call (notify, push, iCloud...); a package with Swift of its
// own (asuka/camera, asuka/location...) brings it, and its frameworks, to
// the apps that import it, and needs no flag.
var iosFeatures = []struct{ flag, symbol string }{
	{"ASUKA_WEBVIEW", "github.com/egoist/asuka/ui.WebView"},
	{"ASUKA_VIDEO", "github.com/egoist/asuka/ui.Video"},
	{"ASUKA_SHADER", "github.com/egoist/asuka/ui.Shader"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Request"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Schedule"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Show"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.SetCategories"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.OnAction"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Present"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Showing"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.SetBadge"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Remove"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.RemoveThread"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.RemoveAll"},
	{"ASUKA_NOTIFY", "github.com/egoist/asuka/sdk/notify.Cancel"},
	// Messages in conversations: Intents' INSendMessageIntent.
	{"ASUKA_CONVERSATIONS", "github.com/egoist/asuka/sdk/notify.Notification.From"},
	{"ASUKA_CONVERSATIONS", "github.com/egoist/asuka/sdk/push.(*Message).From"},
	// The Notification Service Extension's principal class.
	{"ASUKA_NOTIFICATION_SERVICE", rewriteSymbol},
	{"ASUKA_PHOTOLIBRARY", "github.com/egoist/asuka/sdk/photos.Current"},
	{"ASUKA_PHOTOLIBRARY", "github.com/egoist/asuka/sdk/photos.Request"},
	{"ASUKA_PHOTOLIBRARY", "github.com/egoist/asuka/sdk/photos.ManageLimited"},
	{"ASUKA_PHOTOLIBRARY", "github.com/egoist/asuka/sdk/photos.List"},
	{"ASUKA_PHOTOLIBRARY", "github.com/egoist/asuka/sdk/photos.Thumbnail"},
	{"ASUKA_PHOTOLIBRARY", "github.com/egoist/asuka/sdk/photos.Export"},
	{"ASUKA_LIBRARY", "github.com/egoist/asuka/sdk/library.Save"},
	{"ASUKA_PUSH", "github.com/egoist/asuka/sdk/push.Register"},
	{"ASUKA_PERIODIC", "github.com/egoist/asuka/sdk/periodic.Schedule"},
	{"ASUKA_WIDGET", "github.com/egoist/asuka/sdk/widget.Set"},
	// ActivityKit, and the widget extension with the Live Activities' template.
	{"ASUKA_LIVEACTIVITY", "github.com/egoist/asuka/sdk/liveactivity.Start"},
	{"ASUKA_LIVEACTIVITY", "github.com/egoist/asuka/sdk/liveactivity.Enabled"},
	{"ASUKA_LIVEACTIVITY", "github.com/egoist/asuka/sdk/liveactivity.Running"},
	{"ASUKA_LIVEACTIVITY", "github.com/egoist/asuka/sdk/liveactivity.Observe"},
	{"ASUKA_CONTACTS", "github.com/egoist/asuka/sdk/contacts.Request"},
	{"ASUKA_CONTACTS", "github.com/egoist/asuka/sdk/contacts.List"},
	{"ASUKA_CONTACTS", "github.com/egoist/asuka/sdk/contacts.Pick"},
	{"ASUKA_CONTACTS", "github.com/egoist/asuka/sdk/contacts.Add"},
	{"ASUKA_CALENDAR", "github.com/egoist/asuka/sdk/calendar.Request"},
	{"ASUKA_CALENDAR", "github.com/egoist/asuka/sdk/calendar.Calendars"},
	{"ASUKA_CALENDAR", "github.com/egoist/asuka/sdk/calendar.Events"},
	{"ASUKA_CALENDAR", "github.com/egoist/asuka/sdk/calendar.Add"},
	{"ASUKA_CRASH", "github.com/egoist/asuka/sdk/crash.Reports"},
	{"ASUKA_CRASH", "github.com/egoist/asuka/sdk/crash.OnReport"},
	{"ASUKA_COMPOSE", "github.com/egoist/asuka/sdk/compose.Mail"},
	{"ASUKA_COMPOSE", "github.com/egoist/asuka/sdk/compose.Message"},
	{"ASUKA_COMPOSE", "github.com/egoist/asuka/sdk/compose.Check"},
	// asuka/sdk/icloud: every call goes through one of these (its
	// entitlements are from what the app calls: useICloud).
	{"ASUKA_ICLOUD", "github.com/egoist/asuka/sdk/icloud.call"},
	{"ASUKA_ICLOUD", "github.com/egoist/asuka/sdk/icloud.post"},
}

// featureExtras are what a feature needs of the app beyond its framework:
// the entitlements (with a simulator build's values; a release's profile
// must have them) and Info.plist's arrays, as featurePermissions' are a
// package's.
var featureExtras = map[string]struct {
	entitlements map[string]any
	plistArrays  map[string][]string
	iosTrue      []string
	usage        map[string]string
}{
	// Communication notifications: the capability, and the intent the
	// app donates for each message.
	"ASUKA_CONVERSATIONS": {
		entitlements: map[string]any{"com.apple.developer.usernotifications.communication": true},
		plistArrays:  map[string][]string{"NSUserActivityTypes": {"INSendMessageIntent"}},
	},
	// Live Activities: the app says it has them (the widget extension
	// draws them: liveActivity).
	"ASUKA_LIVEACTIVITY": {
		iosTrue: []string{"NSSupportsLiveActivities"},
	},
}

// addFeatureExtras adds what the features flags need (featureExtras).
func (c *appConfig) addFeatureExtras(flags []string) {
	for _, f := range flags {
		if f == "ASUKA_LIVEACTIVITY" {
			c.liveActivity = true
		}
		x, ok := featureExtras[f]
		if !ok {
			continue
		}
		for _, k := range x.iosTrue {
			if !slices.Contains(c.iosTrue, k) {
				c.iosTrue = append(c.iosTrue, k)
			}
		}
		for k, v := range x.entitlements {
			if c.entitlements == nil {
				c.entitlements = map[string]any{}
			}
			c.entitlements[k] = v
		}
		for k, vs := range x.plistArrays {
			if c.plistArrays == nil {
				c.plistArrays = map[string][]string{}
			}
			for _, v := range vs {
				if !slices.Contains(c.plistArrays[k], v) {
					c.plistArrays[k] = append(c.plistArrays[k], v)
				}
			}
		}
		for k, v := range x.usage {
			if c.IOS.Usage == nil {
				c.IOS.Usage = map[string]string{}
			}
			if c.IOS.Usage[k] == "" {
				c.IOS.Usage[k] = v
			}
		}
	}
}

// allFeatures is every feature: the player runs any app.
func allFeatures() []string {
	var out []string
	for _, f := range iosFeatures {
		if !slices.Contains(out, f.flag) {
			out = append(out, f.flag)
		}
	}
	return out
}

// usedFeatures are the features the app uses, and notes in cfg whether it
// has a share sheet or a push rewrite, and what its features need
// (addFeatureExtras).
func usedFeatures(app *asukaApp, cfg *appConfig) []string {
	cfg.shareSheet = app.hasSymbol(sheetSymbol)
	cfg.pushRewrite = app.hasSymbol(rewriteSymbol)
	// asuka/location's temporary full accuracy: Info.plist has the
	// purposes (asuka.toml's locationPurposes) only for an app that asks.
	cfg.locationPrecise = app.hasSymbol(sdkPrefix + "location.RequestPrecise")
	var used []string
	for _, f := range iosFeatures {
		if app.hasSymbol(f.symbol) && !slices.Contains(used, f.flag) {
			used = append(used, f.flag)
		}
	}
	cfg.addFeatureExtras(used)
	return used
}

// playerFeatures are every feature, for asuka dev's player, which runs
// whatever the app's next save calls; what they need of the app
// (entitlements, background modes) is only what the app uses' (which a
// device's development profile must allow).
func playerFeatures(app *asukaApp, cfg *appConfig) []string {
	usedFeatures(app, cfg)
	return allFeatures()
}

// buildCore builds the C core for the SDK's target into work: with m
// compiled in (a release), or without an app (m nil: asuka dev's
// player), with the C the app's externs call (m's, or for a player c,
// the last compile's).
func (t *iosToolchain) buildCore(work string, m *bytecode.Module, c *bytecode.C) (string, error) {
	target := cbuild.Target{OS: "ios", Arch: "arm64"}
	if t.sdkName == "iphonesimulator" {
		target = cbuild.Target{OS: "ios-simulator", Arch: t.goarch}
	}
	if m != nil {
		c = m.C
	}
	// A static library links nothing: the executable links what its C
	// needs.
	f, err := appbuild.Foreign(c, target)
	if err != nil {
		return "", err
	}
	t.cLink = nil
	if f != nil {
		t.cLink = f.Link
	}
	lib := filepath.Join(work, "libasuka.a")
	if m != nil {
		return lib, appbuild.Lib(target, cbuild.Static, m, lib)
	}
	return lib, appbuild.PlayerLib(target, cbuild.Static, c, lib)
}

func (t *iosToolchain) link(app *asukaApp, work, hostObj, lib string) (string, error) {
	bundle := filepath.Join(work, app.name+".app")
	os.RemoveAll(bundle)
	if err := os.MkdirAll(bundle, 0o755); err != nil {
		return "", err
	}
	ents, err := simulatorEntitlements(work, app.cfg)
	if err != nil {
		return "", err
	}
	code, err := t.linkApp(work, bundle, app.cfg, hostObj, lib, ents)
	if err != nil {
		return "", err
	}
	if err := t.embedPlugins(bundle, app); err != nil {
		return "", err
	}
	if err := copyDir(filepath.Join(app.dir, "assets"), filepath.Join(bundle, "assets")); err != nil {
		return "", err
	}
	if err := app.cfg.copyStoreKit(bundle); err != nil {
		return "", err
	}
	urls := ""
	if t := app.cfg.urlTypes(true); t != nil {
		schemes := t[0].(map[string]any)["CFBundleURLSchemes"].([]string)
		for i := range schemes {
			schemes[i] = html.EscapeString(schemes[i])
		}
		urls = fmt.Sprintf("<key>CFBundleURLTypes</key><array><dict><key>CFBundleURLName</key><string>%s</string>"+
			"<key>CFBundleURLSchemes</key><array><string>%s</string></array></dict></array>\n",
			html.EscapeString(app.bundleID), strings.Join(schemes, "</string><string>"))
	}
	// The purpose strings: without one, iOS ends an app that asks for what it protects.
	keys := make([]string, 0, len(app.cfg.IOS.Usage))
	for k := range app.cfg.IOS.Usage {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		urls += fmt.Sprintf("<key>%s</key><string>%s</string>\n", html.EscapeString(k), html.EscapeString(app.cfg.IOS.Usage[k]))
	}
	for _, k := range app.cfg.iosTrue {
		urls += fmt.Sprintf("<key>%s</key><true/>\n", html.EscapeString(k))
	}
	urls += plistKeys(app.cfg.plistExtras())
	if g := app.cfg.appGroup(); g != "" {
		urls += fmt.Sprintf("<key>AsukaAppGroup</key><string>%s</string>\n", html.EscapeString(g))
	}
	if app.cfg.ShareTarget.on && app.cfg.shareSheet {
		urls += "<key>AsukaShareSheet</key><true/>\n"
	}
	if app.cfg.pushRewrite {
		urls += "<key>AsukaNotificationService</key><true/>\n"
	}

	devices := ""
	for _, d := range app.cfg.iosDevices() {
		devices += fmt.Sprintf("<integer>%d</integer>", d)
	}
	if o := app.cfg.ipadOrientations(); o != nil {
		urls += "<key>UISupportedInterfaceOrientations~ipad</key><array><string>" + strings.Join(o, "</string><string>") + "</string></array>\n"
	}
	urls += app.cfg.plistLocalizationKeys()
	plist := fmt.Sprintf(infoPlist, app.bundleID, app.displayName(), app.displayName(), iosMin, devices, urls)
	if err := os.WriteFile(filepath.Join(bundle, "Info.plist"), []byte(plist), 0o644); err != nil {
		return "", err
	}
	if err := app.cfg.writeLprojs(bundle); err != nil {
		return "", err
	}
	if app.cfg.splashOn() {
		if err := t.compileAssets(app.cfg, work, bundle, filepath.Join(bundle, "Info.plist"), false); err != nil {
			return "", err
		}
	}
	// Signed as Xcode signs for the simulator (simulatorIdentity).
	sign := simulatorIdentity(app.cfg.IOS.Team)
	if sign == "-" && len(app.cfg.Shortcuts) > 0 {
		fmt.Println("asuka: no Apple Development identity in the keychain: the app is signed ad hoc, and its shortcuts' App Intents won't run (Xcode › Settings › Accounts makes one)")
	}
	if app.cfg.ShareTarget.on {
		exe, err := t.buildShare(work, app.cfg, code)
		if err != nil {
			return "", err
		}
		appex, err := embedShare(bundle, exe, app.cfg.shareInfo(map[string]any{
			"CFBundleName": app.displayName(), "CFBundleDisplayName": app.displayName(), "CFBundleShortVersionString": "1.0", "CFBundleVersion": "1", "MinimumOSVersion": iosMin,
			"CFBundleSupportedPlatforms": []string{"iPhoneSimulator"}, "UIDeviceFamily": app.cfg.iosDevices(),
		}))
		if err != nil {
			return "", err
		}
		if err := t.run(work, nil, "codesign", "--force", "--sign", sign, appex); err != nil {
			return "", err
		}
	}
	if app.cfg.pushRewrite {
		exe, err := t.buildNotification(work, app.cfg, code)
		if err != nil {
			return "", err
		}
		appex, err := embedNotification(bundle, exe, app.cfg.notificationInfo(map[string]any{
			"CFBundleName": app.displayName(), "CFBundleDisplayName": app.displayName(), "CFBundleShortVersionString": "1.0", "CFBundleVersion": "1", "MinimumOSVersion": iosMin,
			"CFBundleSupportedPlatforms": []string{"iPhoneSimulator"}, "UIDeviceFamily": app.cfg.iosDevices(),
		}))
		if err != nil {
			return "", err
		}
		if err := t.run(work, nil, "codesign", "--force", "--sign", sign, appex); err != nil {
			return "", err
		}
	}
	if app.cfg.widgetExtension() {
		exe, err := t.buildWidgets(work, app.cfg)
		if err != nil {
			return "", err
		}
		appex, err := embedWidgets(bundle, exe, app.cfg.widgetInfo(map[string]any{
			"CFBundleName": app.displayName(), "CFBundleDisplayName": app.displayName(), "CFBundleShortVersionString": "1.0", "CFBundleVersion": "1", "MinimumOSVersion": iosMin,
			"CFBundleSupportedPlatforms": []string{"iPhoneSimulator"}, "UIDeviceFamily": app.cfg.iosDevices(),
		}))
		if err != nil {
			return "", err
		}
		if err := app.cfg.writeLprojs(appex); err != nil {
			return "", err
		}
		if err := t.run(work, nil, "codesign", "--force", "--sign", sign, appex); err != nil {
			return "", err
		}
	}
	if err := t.signFrameworks(work, bundle, sign); err != nil {
		return "", err
	}
	return bundle, t.run(work, nil, "codesign", "--force", "--sign", sign, bundle)
}

// simulatorEntitlements are the linker flags that give a simulator build
// the entitlements the keychain wants (an app identifier and its access
// group; without them SecItemAdd fails with errSecMissingEntitlement, and
// asuka/sdk/secure with it). As Xcode does: in the binary's __entitlements
// section, for an ad hoc signature can't carry them.
func simulatorEntitlements(work string, cfg *appConfig) ([]string, error) {
	id := "ASUKASIMULA." + cfg.ID // a team ID's shape: ten characters
	ent := filepath.Join(work, "simulator.entitlements")
	m := map[string]any{
		"application-identifier": id,
		"keychain-access-groups": []string{id},
	}
	if ds := cfg.associatedDomains(true); len(ds) > 0 {
		m["com.apple.developer.associated-domains"] = ds
	}
	if g := cfg.appGroup(); g != "" {
		m["com.apple.security.application-groups"] = []string{g}
	}
	for k, v := range cfg.entitlements {
		m[k] = iCloudTeam(v, firstOf(cfg.IOS.Team, "ASUKASIMULA"))
	}
	if cfg.IOS.StoreKit != "" {
		// storekitd takes a StoreKit configuration only for an app
		// installed for development, as Xcode's debug builds are.
		m["get-task-allow"] = true
	}
	text := plist(m)
	if err := os.WriteFile(ent, []byte(text), 0o644); err != nil {
		return nil, err
	}
	return []string{"-Xlinker", "-sectcreate", "-Xlinker", "__TEXT", "-Xlinker", "__entitlements", "-Xlinker", ent}, nil
}

const infoPlist = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>%s</string>
<key>CFBundleExecutable</key><string>AsukaApp</string>
<key>CFBundleName</key><string>%s</string>
<key>CFBundleDisplayName</key><string>%s</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>MinimumOSVersion</key><string>%s</string>
<key>CFBundleSupportedPlatforms</key><array><string>iPhoneSimulator</string></array>
<key>UIDeviceFamily</key><array>%s</array>
<key>CADisableMinimumFrameDurationOnPhone</key><true/>
<key>NSAppTransportSecurity</key><dict><key>NSAllowsLocalNetworking</key><true/></dict>
<key>UIApplicationSceneManifest</key><dict><key>UIApplicationSupportsMultipleScenes</key><false/></dict>
<key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationPortrait</string><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
%s</dict></plist>
`

type simDevice struct {
	Name        string `json:"name"`
	UDID        string `json:"udid"`
	State       string `json:"state"`
	IsAvailable bool   `json:"isAvailable"`
	runtime     string
}

// pickSimulator returns the device named want, or the booted iPhone, or
// the newest available iPhone, booting it if needed.
func pickSimulator(want string) (*simDevice, error) {
	out, err := exec.Command("xcrun", "simctl", "list", "devices", "available", "-j").Output()
	if err != nil {
		return nil, fmt.Errorf("simctl: %v (install Xcode and an iOS simulator runtime)", err)
	}
	var list struct {
		Devices map[string][]simDevice `json:"devices"`
	}
	if err := json.Unmarshal(out, &list); err != nil {
		return nil, err
	}
	var all []*simDevice
	runtimes := make([]string, 0, len(list.Devices))
	for rt := range list.Devices {
		if strings.Contains(rt, "iOS") {
			runtimes = append(runtimes, rt)
		}
	}
	sort.Sort(sort.Reverse(sort.StringSlice(runtimes)))
	for _, rt := range runtimes {
		for i := range list.Devices[rt] {
			d := list.Devices[rt][i]
			d.runtime = rt
			if d.IsAvailable {
				all = append(all, &d)
			}
		}
	}
	var pick *simDevice
	for _, d := range all {
		switch {
		case want != "" && (d.UDID == want || d.Name == want):
			pick = d
		case want == "" && d.State == "Booted" && strings.HasPrefix(d.Name, "iPhone") && pick == nil:
			pick = d
		}
	}
	if pick == nil && want == "" {
		for _, d := range all {
			if strings.HasPrefix(d.Name, "iPhone") && strings.Contains(d.Name, "Pro") && !strings.Contains(d.Name, "Max") {
				pick = d
				break
			}
		}
		if pick == nil {
			for _, d := range all {
				if strings.HasPrefix(d.Name, "iPhone") {
					pick = d
					break
				}
			}
		}
	}
	if pick == nil {
		if want != "" {
			return nil, fmt.Errorf("no simulator %q (see xcrun simctl list devices)", want)
		}
		return nil, errors.New("no iPhone simulator; add one in Xcode")
	}
	if pick.State != "Booted" {
		fmt.Printf("asuka: booting %s\n", pick.Name)
		if out, err := exec.Command("xcrun", "simctl", "boot", pick.UDID).CombinedOutput(); err != nil && !bytes.Contains(out, []byte("current state: Booted")) {
			return nil, fmt.Errorf("simctl boot: %s", bytes.TrimSpace(out))
		}
		exec.Command("xcrun", "simctl", "bootstatus", pick.UDID, "-b").Run()
		exec.Command("open", "-a", "Simulator").Run() // shows it, when installed
	}
	return pick, nil
}

func devIOS(dir string, o *devOptions, stop <-chan os.Signal) error {
	app, err := loadApp(dir)
	if err != nil {
		return err
	}
	// An iPhone or iPad --device names (ios_device.go).
	if d, err := findIOSDevice(o.device); err != nil {
		return err
	} else if d != nil {
		return devIOSDevice(app, o, d, stop)
	}
	tc, err := newIOSToolchain(o.verbose)
	if err != nil {
		return err
	}
	work, err := workDir(app, "ios")
	if err != nil {
		return err
	}
	sim, err := pickSimulator(o.device)
	if err != nil {
		return err
	}
	fmt.Printf("asuka: %s on %s (%s)\n", app.name, sim.Name, sim.UDID)
	// The simulator shares this Mac's network: the player reaches asuka
	// dev on its loopback.
	loop, err := newDevLoop(app, "127.0.0.1:0")
	if err != nil {
		return err
	}
	return loop.run(simulatorPlayer(app, tc, work, sim, loop), stop, o.once)
}

// simulatorPlayer is the player on the simulator sim: built, installed
// when it changed, launched connected to the loop.
func simulatorPlayer(app *asukaApp, tc *iosToolchain, work string, sim *simDevice, loop *devLoop) devPlayer {
	installed := ""
	launch := func() error {
		start := time.Now()
		bundle, err := tc.buildPlayer(app, work)
		if err != nil {
			return err
		}
		if sum := dirDigest(bundle); sum != installed {
			if err := tc.run(work, nil, "xcrun", "simctl", "install", sim.UDID, bundle); err != nil {
				return err
			}
			installed = sum
			fmt.Printf("asuka: the player built and installed in %.1fs\n", time.Since(start).Seconds())
		}
		// Its environment: SIMCTL_CHILD_ names are the app's.
		env := append(app.cfg.storeKitLaunchEnv(), fmt.Sprintf("SIMCTL_CHILD_ASUKA_DEV=127.0.0.1:%d", loop.port()))
		return tc.run(work, env, "xcrun", "simctl", "launch", "--terminate-running-process", sim.UDID, app.bundleID)
	}
	return devPlayer{
		launch: launch,
		stop:   func() { exec.Command("xcrun", "simctl", "terminate", sim.UDID, app.bundleID).Run() },
	}
}

// buildPlayer builds asuka dev's player for the simulator: the app's
// .app (its Info.plist, icon, entitlements, native packages' Swift), with
// every feature of the host and the core without an app.
func (t *iosToolchain) buildPlayer(app *asukaApp, work string) (string, error) {
	if err := generateNatives(app); err != nil {
		return "", err
	}
	lib, err := t.buildCore(work, nil, app.c)
	if err != nil {
		return "", err
	}
	hostObj, err := t.buildHost(work, app, playerFeatures(app, app.cfg))
	if err != nil {
		return "", err
	}
	return t.link(app, work, hostObj, lib)
}

// copyDir copies the files under src (if it exists) to dst.
func copyDir(src, dst string) error {
	if _, err := os.Stat(src); err != nil {
		return nil
	}
	return filepath.WalkDir(src, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(src, path)
		out := filepath.Join(dst, rel)
		if d.IsDir() {
			return os.MkdirAll(out, 0o755)
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		return os.WriteFile(out, data, 0o644)
	})
}
