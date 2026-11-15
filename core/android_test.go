package core_test

import (
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/androidsdk"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/cgen"
)

// The whole stack on Android: the Counter compiled to C, built with the
// core by the NDK into libasuka.so, packaged in the Android host's shell
// app (platform/android), installed on the running emulator, tapped
// through its accessibility tree (uiautomator), read back.
func TestAndroidCounter(t *testing.T) {
	if testing.Short() {
		t.Skip("an emulator (not -short)")
	}
	sys := androidsdk.Local()
	sdk := sys.SDK()
	adb := sys.ADB(sdk)
	out, err := exec.Command(adb, "devices").Output()
	if err != nil || !strings.Contains(string(out), "\tdevice") {
		t.Skip("no Android device or emulator running")
	}
	serial := regexp.MustCompile(`(?m)^(\S+)\tdevice$`).FindStringSubmatch(string(out))[1]
	abi, _ := exec.Command(adb, "-s", serial, "shell", "getprop", "ro.product.cpu.abi").Output()
	arch, abiDir := "arm64", "arm64-v8a"
	if strings.TrimSpace(string(abi)) == "x86_64" {
		arch, abiDir = "amd64", "x86_64"
	}
	tc, err := cbuild.ForTarget(cbuild.Target{OS: "android", Arch: arch})
	if err != nil {
		t.Skip(err)
	}
	m, body := counterModule(t)
	m.App = int32(body)
	src, err := cgen.Generate(m)
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	app := filepath.Join(dir, "app.c")
	os.WriteFile(app, src, 0o644)
	root, _ := filepath.Abs(".")
	jni := filepath.Join(dir, "jniLibs")
	if err := tc.Lib(root, cbuild.Shared, []string{app}, false, filepath.Join(jni, abiDir, "libasuka.so")); err != nil {
		t.Fatal(err)
	}
	android, _ := filepath.Abs(filepath.Join("..", "platform", "android"))
	gradle := exec.Command("./gradlew", "-q", ":app:assembleDebug", "-PasukaJniLibs="+jni, "-PasukaApplicationId=dev.asuka.coretest", "-PasukaAppName=Counter")
	gradle.Dir = android
	gradle.Env = append(os.Environ(), "ANDROID_HOME="+sdk)
	if b, err := gradle.CombinedOutput(); err != nil {
		t.Fatalf("gradle: %v\n%s", err, b)
	}
	apk := filepath.Join(android, "app", "build", "outputs", "apk", "debug", "app-debug.apk")
	run := func(args ...string) string {
		b, err := exec.Command(adb, append([]string{"-s", serial}, args...)...).CombinedOutput()
		if err != nil {
			t.Fatalf("adb %v: %v\n%s", args, err, b)
		}
		return string(b)
	}
	run("install", "-r", "-t", apk)
	run("shell", "am", "force-stop", "dev.asuka.coretest")
	run("shell", "am", "start", "-W", "-n", "dev.asuka.coretest/dev.asuka.AsukaActivity")
	// The screen's texts and where they are, from the accessibility tree.
	screen := func() string {
		run("shell", "uiautomator", "dump", "/sdcard/asuka.xml")
		return run("shell", "cat", "/sdcard/asuka.xml")
	}
	wait := func(text string) string {
		var s string
		for range 60 {
			if s = screen(); strings.Contains(s, text) {
				return s
			}
			time.Sleep(500 * time.Millisecond)
		}
		t.Fatalf("the screen never showed %q:\n%s", text, s)
		return ""
	}
	s := wait("Count: 0")
	m2 := regexp.MustCompile(`text="Add"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"`).FindStringSubmatch(s)
	if m2 == nil {
		t.Fatalf("no Add button:\n%s", s)
	}
	x := (atoi(m2[1]) + atoi(m2[3])) / 2
	y := (atoi(m2[2]) + atoi(m2[4])) / 2
	run("shell", "input", "tap", itoa(x), itoa(y))
	wait("Count: 1")
}

func atoi(s string) int {
	n := 0
	for _, c := range s {
		n = n*10 + int(c-'0')
	}
	return n
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	s := ""
	for ; n > 0; n /= 10 {
		s = string(rune('0'+n%10)) + s
	}
	return s
}
