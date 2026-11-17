// Package androidsdk finds Android's tools on this computer, where each
// system puts them and by the names it gives them: the SDK, its newest
// NDK with a build for this computer and that NDK's clang, adb, the
// emulator, the scripts (gradlew.bat, sdkmanager.bat on Windows) and the
// Java that runs Gradle (one of the versions it runs on: JAVA_HOME's,
// else Android Studio's own, else another installed).
//
// Everything it looks at comes from a System, so tests give it another
// computer's: its GOOS, its environment and its home directory, over a
// tree of files made to look like one.
package androidsdk

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strconv"
	"strings"
)

// System is the computer the tools are looked for on.
type System struct {
	// GOOS is its system: darwin, linux or windows.
	GOOS string
	// Getenv reads its environment.
	Getenv func(string) string
	// Home is the user's home directory (%USERPROFILE% on Windows).
	Home string
	// Root is the directory system-wide places (/Applications, /opt) are
	// in: "" for this computer's own root; Windows' come from its
	// environment (%ProgramFiles%).
	Root string
}

// Local is this computer.
func Local() System {
	home, _ := os.UserHomeDir()
	return System{GOOS: runtime.GOOS, Getenv: os.Getenv, Home: home}
}

func (s System) env(k string) string {
	if s.Getenv == nil {
		return ""
	}
	return s.Getenv(k)
}

// Exe is the file name of the executable name: name.exe on Windows.
func (s System) Exe(name string) string {
	if s.GOOS == "windows" {
		return name + ".exe"
	}
	return name
}

// Script is the file name of the SDK's (or Gradle's) script name, which
// Windows has as a batch file: gradlew.bat, sdkmanager.bat, apksigner.bat.
func (s System) Script(name string) string {
	if s.GOOS == "windows" {
		return name + ".bat"
	}
	return name
}

// localAppData is Windows' %LOCALAPPDATA%.
func (s System) localAppData() string {
	if v := s.env("LOCALAPPDATA"); v != "" {
		return v
	}
	return filepath.Join(s.Home, "AppData", "Local")
}

// programFiles are Windows' %ProgramFiles% (the native one on Arm64
// Windows too, where Android Studio installs) and, after it, the x86's.
func (s System) programFiles() []string {
	var out []string
	for _, k := range []string{"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"} {
		if v := s.env(k); v != "" && !slices.Contains(out, v) {
			out = append(out, v)
		}
	}
	if len(out) == 0 {
		out = append(out, `C:\Program Files`)
	}
	return out
}

// SDK is the Android SDK's directory: ANDROID_HOME, ANDROID_SDK_ROOT,
// else where Android Studio installs it (~/Library/Android/sdk on a Mac,
// ~/Android/Sdk on Linux, %LOCALAPPDATA%\Android\Sdk on Windows); "" if
// there's none.
func (s System) SDK() string {
	for _, k := range []string{"ANDROID_HOME", "ANDROID_SDK_ROOT"} {
		if v := s.env(k); v != "" {
			return v
		}
	}
	var places []string
	switch s.GOOS {
	case "darwin":
		places = []string{filepath.Join(s.Home, "Library", "Android", "sdk")}
	case "windows":
		places = []string{filepath.Join(s.localAppData(), "Android", "Sdk")}
	default:
		places = []string{filepath.Join(s.Home, "Android", "Sdk")}
	}
	for _, p := range places {
		if isDir(p) {
			return p
		}
	}
	return ""
}

// ADB is the SDK's adb.
func (s System) ADB(sdk string) string {
	return filepath.Join(sdk, "platform-tools", s.Exe("adb"))
}

// Emulator is the SDK's emulator.
func (s System) Emulator(sdk string) string {
	return filepath.Join(sdk, "emulator", s.Exe("emulator"))
}

// HostTag is the name of the NDK's build for this computer, its
// toolchains/llvm/prebuilt/<tag>: x86_64 on every system (a Mac's runs
// on Apple silicon too, Windows on Arm runs Windows' emulated).
func (s System) HostTag() string {
	return s.GOOS + "-x86_64"
}

// NDK is the newest NDK in the SDK (its ndk/<version>) that has a build
// for this computer, and that build's bin directory, with clang.
func (s System) NDK(sdk string) (ndk, bin string, err error) {
	dirs := subdirs(filepath.Join(sdk, "ndk"), "")
	if len(dirs) == 0 {
		return "", "", errors.New("no Android NDK: install one with Android Studio's SDK Manager (SDK Tools, NDK)")
	}
	slices.SortFunc(dirs, func(a, b string) int { return compareVersions(filepath.Base(a), filepath.Base(b)) })
	for i := len(dirs) - 1; i >= 0; i-- {
		if b := s.NDKBin(dirs[i]); b != "" {
			return dirs[i], b, nil
		}
	}
	return "", "", fmt.Errorf("no Android NDK for %s in %s (%s): install one with Android Studio's SDK Manager", s.HostTag(), filepath.Join(sdk, "ndk"), strings.Join(names(dirs), ", "))
}

// NDKBin is the bin directory of ndk's build for this computer, with
// its clang; "" if the NDK has none. Its build for this system with
// another architecture (one made for Arm64 Linux) does too.
func (s System) NDKBin(ndk string) string {
	prebuilt := filepath.Join(ndk, "toolchains", "llvm", "prebuilt")
	bin := filepath.Join(prebuilt, s.HostTag(), "bin")
	if isFile(filepath.Join(bin, s.Exe("clang"))) {
		return bin
	}
	for _, o := range subdirs(prebuilt, s.GOOS+"-") {
		if b := filepath.Join(o, "bin"); isFile(filepath.Join(b, s.Exe("clang"))) {
			return b
		}
	}
	return ""
}

// Clang is the NDK's clang in its bin directory.
func (s System) Clang(bin string) string {
	return filepath.Join(bin, s.Exe("clang"))
}

// CC is the C compiler for an Android target (a triple and its API
// level, aarch64-linux-android33), as the CC of a cgo build: the NDK's
// clang, quoted when its path has spaces, with --target. It's what the
// NDK's wrappers (aarch64-linux-android33-clang, a shell script; .cmd on
// Windows, through cmd.exe and its quoting) run, called without them.
func CC(clang, target string) string {
	return quote(clang) + " --target=" + target
}

// quote quotes s as the go command splits CC (cmd/internal/quoted): in
// double quotes if it has spaces (a Windows path can't have one), else
// in single quotes.
func quote(s string) string {
	if !strings.ContainsAny(s, " \t\n\r'\"") {
		return s
	}
	if !strings.Contains(s, `"`) {
		return `"` + s + `"`
	}
	return "'" + s + "'"
}

// Gradle 8.14, the hosts' wrapper's, runs on Java 17 to 24: the Kotlin
// that compiles its build scripts can't read a later one's version.
const (
	JavaMin = 17
	JavaMax = 24
)

// Java is the JDK that runs Gradle, one of JavaMin to JavaMax: JAVA_HOME
// when it is, else Android Studio's own (its jbr, in each place it
// installs: its installer's, JetBrains Toolbox's, a preview's), else the
// newest other JDK installed. When none is, JAVA_HOME (JavaProblem says
// what's wrong with it), or "" (then Gradle's script looks for java on
// the PATH).
func (s System) Java() string {
	home := s.env("JAVA_HOME")
	if home != "" && javaFits(home) {
		return home
	}
	for _, h := range append(s.studioJavas(), s.installedJavas()...) {
		if isFile(filepath.Join(h, "bin", s.Exe("java"))) && javaFits(h) {
			return h
		}
	}
	return home
}

// JavaProblem is why the JDK at home can't run Gradle ("" when it can,
// or when its version can't be told).
func JavaProblem(home string) string {
	if home == "" || javaFits(home) {
		return ""
	}
	return fmt.Sprintf("Gradle runs on Java %d to %d, and %s is Java %d: set JAVA_HOME to one of those (Android Studio's own is one)", JavaMin, JavaMax, home, JavaVersion(home))
}

// javaFits reports whether the JDK at home runs Gradle: a version from
// JavaMin to JavaMax, or one that can't be told (it may).
func javaFits(home string) bool {
	v := JavaVersion(home)
	return v == 0 || v >= JavaMin && v <= JavaMax
}

// JavaVersion is the JDK at home's feature version (17 of "17.0.19", 8 of
// "1.8.0_402"), from its release file; 0 when it can't be told.
func JavaVersion(home string) int {
	b, err := os.ReadFile(filepath.Join(home, "release"))
	if err != nil {
		return 0
	}
	for _, line := range strings.Split(string(b), "\n") {
		v, ok := strings.CutPrefix(strings.TrimSpace(line), "JAVA_VERSION=")
		if !ok {
			continue
		}
		v = strings.TrimPrefix(strings.Trim(v, `"`), "1.") // 1.8.0: Java 8
		if i := strings.IndexFunc(v, func(r rune) bool { return r < '0' || r > '9' }); i >= 0 {
			v = v[:i]
		}
		n, _ := strconv.Atoi(v)
		return n
	}
	return 0
}

// installedJavas are the JDKs installed where each system's packages
// put them, newest first.
func (s System) installedJavas() []string {
	var out []string
	dirs := func(parent, sub string) {
		for _, d := range subdirs(parent, "") {
			out = append(out, filepath.Join(d, sub))
		}
	}
	switch s.GOOS {
	case "darwin":
		dirs(filepath.Join(s.Root, "/Library/Java/JavaVirtualMachines"), filepath.Join("Contents", "Home"))
		dirs(filepath.Join(s.Home, "Library", "Java", "JavaVirtualMachines"), filepath.Join("Contents", "Home"))
		for _, brew := range []string{filepath.Join(s.Root, "/opt/homebrew/opt"), filepath.Join(s.Root, "/usr/local/opt")} {
			for _, d := range subdirs(brew, "openjdk") {
				out = append(out, filepath.Join(d, "libexec", "openjdk.jdk", "Contents", "Home"))
			}
		}
	case "windows":
		for _, pf := range s.programFiles() {
			for _, vendor := range []string{"Java", "Eclipse Adoptium", "Microsoft", "Zulu", "BellSoft", "Amazon Corretto"} {
				dirs(filepath.Join(pf, vendor), "")
			}
		}
	default:
		dirs(filepath.Join(s.Root, "/usr/lib/jvm"), "")
		dirs(filepath.Join(s.Home, ".sdkman", "candidates", "java"), "")
	}
	dirs(filepath.Join(s.Home, ".jdks"), "") // IntelliJ's downloads
	slices.SortStableFunc(out, func(a, b string) int { return JavaVersion(b) - JavaVersion(a) })
	return out
}

// studioJavas are where Android Studio's JDK may be, most likely first.
func (s System) studioJavas() []string {
	var out []string
	// jbr since Android Studio Electric Eel (2022.1), jre before.
	jdks := func(dir string) {
		out = append(out, filepath.Join(dir, "jbr"), filepath.Join(dir, "jre"))
	}
	// JetBrains Toolbox 1.x: a directory per channel and build, newest first.
	toolbox := func(apps string) []string {
		var m []string
		for _, ch := range subdirs(apps, "ch-") {
			m = append(m, subdirs(ch, "")...)
		}
		slices.SortFunc(m, func(a, b string) int { return compareVersions(filepath.Base(b), filepath.Base(a)) })
		return m
	}
	switch s.GOOS {
	case "darwin":
		mac := func(app string) {
			out = append(out, filepath.Join(app, "Contents", "jbr", "Contents", "Home"), filepath.Join(app, "Contents", "jre", "Contents", "Home"))
		}
		for _, apps := range []string{filepath.Join(s.Root, "/Applications"), filepath.Join(s.Home, "Applications")} {
			mac(filepath.Join(apps, "Android Studio.app"))
			mac(filepath.Join(apps, "Android Studio Preview.app"))
		}
		for _, b := range toolbox(filepath.Join(s.Home, "Library", "Application Support", "JetBrains", "Toolbox", "apps", "AndroidStudio")) {
			mac(filepath.Join(b, "Android Studio.app"))
			mac(filepath.Join(b, "Android Studio Preview.app"))
		}
	case "windows":
		for _, pf := range s.programFiles() {
			jdks(filepath.Join(pf, "Android", "Android Studio"))
			jdks(filepath.Join(pf, "Android", "Android Studio Preview"))
		}
		// JetBrains Toolbox 2.x installs in Programs, 1.x a directory per build.
		local := s.localAppData()
		jdks(filepath.Join(local, "Programs", "Android Studio"))
		jdks(filepath.Join(local, "Programs", "Android Studio Preview"))
		for _, b := range toolbox(filepath.Join(local, "JetBrains", "Toolbox", "apps", "AndroidStudio")) {
			jdks(b)
		}
	default:
		for _, d := range []string{filepath.Join(s.Root, "/opt/android-studio"), filepath.Join(s.Root, "/usr/local/android-studio"), filepath.Join(s.Home, "android-studio"),
			filepath.Join(s.Root, "/snap/android-studio/current"), filepath.Join(s.Home, ".local", "share", "JetBrains", "Toolbox", "apps", "android-studio")} {
			jdks(d)
		}
		for _, b := range toolbox(filepath.Join(s.Home, ".local", "share", "JetBrains", "Toolbox", "apps", "AndroidStudio")) {
			jdks(b)
		}
	}
	return out
}

// compareVersions orders dotted versions by their numbers (27.0.12077973
// after 9.0.1); a name that isn't one comes first.
func compareVersions(a, b string) int {
	pa, oka := version(a)
	pb, okb := version(b)
	switch {
	case !oka && !okb:
		return strings.Compare(a, b)
	case !oka:
		return -1
	case !okb:
		return 1
	}
	return slices.Compare(pa, pb)
}

func version(s string) ([]int, bool) {
	var out []int
	for _, f := range strings.Split(s, ".") {
		n, err := strconv.Atoi(f)
		if err != nil {
			return nil, false
		}
		out = append(out, n)
	}
	return out, true
}

func names(paths []string) []string {
	out := make([]string, len(paths))
	for i, p := range paths {
		out[i] = filepath.Base(p)
	}
	return out
}

// subdirs are dir's directories whose names start with prefix, in
// order (read, not globbed: a path may have a glob's characters, which
// Windows can't escape).
func subdirs(dir, prefix string) []string {
	es, _ := os.ReadDir(dir)
	var out []string
	for _, e := range es {
		if strings.HasPrefix(e.Name(), prefix) && isDir(filepath.Join(dir, e.Name())) {
			out = append(out, filepath.Join(dir, e.Name()))
		}
	}
	return out
}

func isDir(p string) bool {
	st, err := os.Stat(p)
	return err == nil && st.IsDir()
}

func isFile(p string) bool {
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}
