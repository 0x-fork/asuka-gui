package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/boot"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/plugin"
)

// Desktop apps: the executable is the desktop host (cmd/asuka-host: Go,
// AppKit, Win32 or GTK, the same for every app), with the C core beside
// it (libasuka: libasuka.dylib in a Mac app's Frameworks, asuka.dll
// beside the .exe, libasuka.so in a Linux prefix's lib/<exe>) and the
// app's settings (internal/boot's ConfigFile). A release's core has the
// app compiled into it; asuka dev's player's has none, and loads the
// bytecode the CLI sends (ASUKA_DEV).

// coreLibName is the core library's file name on goos (host/bridge's
// LibName).
func coreLibName(goos string) string {
	switch goos {
	case "darwin":
		return "libasuka.dylib"
	case "windows":
		return "asuka.dll"
	}
	return "libasuka.so"
}

// writeHostConfig writes the app's settings for the desktop host at path:
// a release's for target, asuka dev's player's for "" (hostConfig).
func writeHostConfig(app *asukaApp, platform, target string, updates bool, path string) error {
	c := app.hostConfig(platform, target, updates)
	b, err := json.MarshalIndent(&c, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return os.WriteFile(path, append(b, '\n'), 0o644)
}

// desktopProcess is a dev player running on this computer (or, for
// Linux from another system, on ASUKA_LINUX_RUN's machine).
type desktopProcess struct {
	cmd   *exec.Cmd
	proc  *os.Process
	stdin io.WriteCloser // Windows: closed, the app quits (ASUKA_STOP=stdin)
	done  chan struct{}
}

func (p *desktopProcess) stop() {
	if p == nil || p.proc == nil {
		return
	}
	select {
	case <-p.done:
		return
	default:
	}
	if p.stdin != nil {
		p.stdin.Close()
	} else {
		p.proc.Signal(os.Interrupt)
	}
	select {
	case <-p.done:
	case <-time.After(3 * time.Second):
		p.proc.Kill()
		<-p.done
	}
}

// devDesktop runs the app on this computer (mac, windows, linux): the
// player (the host and the core without an app) in the app's work
// directory, connected to asuka dev, which sends it the app.
func devDesktop(dir string, o *devOptions, stop <-chan os.Signal, platform string) error {
	goos := map[string]string{platformMac: "darwin", platformWindows: "windows", platformLinux: "linux"}[platform]
	var wrap []string
	if goos != runtime.GOOS {
		if platform != platformLinux {
			return fmt.Errorf("asuka dev --platform %s runs the app on this computer: run it on %s (asuka build --platform %s builds from anywhere)", platform, map[string]string{platformMac: "a Mac", platformWindows: "Windows"}[platform], platform)
		}
		if wrap = strings.Fields(os.Getenv("ASUKA_LINUX_RUN")); len(wrap) == 0 {
			return errors.New("a Linux app runs only on Linux: run asuka dev there, or set ASUKA_LINUX_RUN to a command that runs a program on a Linux machine with a display that sees this computer's files at the same paths (orb -m ubuntu, limactl shell default). asuka build --platform linux builds a release from here")
		}
	}
	app, err := loadApp(dir)
	if err != nil {
		return err
	}
	work, err := workDir(app, platform)
	if err != nil {
		return err
	}
	t := cbuild.Target{OS: goos, Arch: runtime.GOARCH}
	where := "this computer"
	listen, devHost := "127.0.0.1:0", "127.0.0.1"
	if wrap != nil {
		out, err := exec.Command(wrap[0], append(wrap[1:len(wrap):len(wrap)], "uname", "-m")...).Output()
		if err != nil {
			return fmt.Errorf("ASUKA_LINUX_RUN=%q: uname -m: %v", os.Getenv("ASUKA_LINUX_RUN"), err)
		}
		switch m := strings.TrimSpace(string(out)); m {
		case "aarch64", "arm64":
			t.Arch = "arm64"
		case "x86_64", "amd64":
			t.Arch = "amd64"
		default:
			return fmt.Errorf("ASUKA_LINUX_RUN: a %s machine; Asuka runs on amd64 and arm64", m)
		}
		where = wrap[0]
		// The machine reaches this one by the network.
		listen = ":0"
		if devHost = os.Getenv("ASUKA_DEV_HOST"); devHost == "" {
			if hs := lanHosts(); len(hs) > 0 {
				devHost = hs[0]
			}
		}
	}
	if platform == platformLinux && !gAppIDValid(app.bundleID) {
		fmt.Printf("asuka: %q isn't a valid application ID on Linux (letters, digits, _ and -, in two parts or more, none starting with a digit): the app runs under another\n", app.bundleID)
	}
	fmt.Printf("asuka: %s on %s\n", app.name, where)
	loop, err := newDevLoop(app, listen)
	if err != nil {
		return err
	}
	data := filepath.Join(work, "data")
	os.MkdirAll(data, 0o755)
	var proc *desktopProcess
	launch := func() error {
		start := time.Now()
		exe, err := buildDesktopPlayer(app, platform, t, work, o.verbose)
		if err != nil {
			return err
		}
		proc.stop()
		env := append(os.Environ(),
			fmt.Sprintf("ASUKA_DEV=%s:%d", devHost, loop.port()),
			"ASUKA_DATA="+data,
			// Read where they are, so changing one needs no rebuild.
			"ASUKA_ASSETS="+filepath.Join(app.dir, "assets"),
		)
		if platform != platformMac {
			env = append(env, "ASUKA_APP_CONFIG="+filepath.Join(filepath.Dir(exe), boot.ConfigFile))
		}
		// The plugins' libraries, where the core finds them (a build puts
		// them beside the program).
		libEnv, err := pluginEnv(app, plugin.Target(runtime.GOOS, runtime.GOARCH))
		if err != nil {
			return err
		}
		env = append(env, libEnv...)
		if platform == platformWindows {
			env = append(env, "ASUKA_STOP=stdin")
		}
		p := &desktopProcess{done: make(chan struct{})}
		if platform == platformMac {
			// It answers for itself, as when it's opened from the Finder:
			// its privacy prompts (camera, location...) are its own, with
			// its purpose strings, not the terminal's.
			p.proc, err = spawnOwn(exe, app.dir, env)
		} else {
			name, args := exe, []string(nil)
			if wrap != nil {
				name, args = wrap[0], append(wrap[1:len(wrap):len(wrap)], "env", "ASUKA_DEV="+fmt.Sprintf("%s:%d", devHost, loop.port()),
					"ASUKA_DATA="+data, "ASUKA_ASSETS="+filepath.Join(app.dir, "assets"),
					"ASUKA_APP_CONFIG="+filepath.Join(filepath.Dir(exe), boot.ConfigFile))
				args = append(append(args, libEnv...), exe)
			}
			c := exec.Command(name, args...)
			c.Dir = app.dir
			c.Env = env
			c.Stdout, c.Stderr = os.Stdout, os.Stderr
			if platform == platformWindows {
				if p.stdin, err = c.StdinPipe(); err != nil {
					return err
				}
			}
			if err = c.Start(); err == nil {
				p.cmd, p.proc = c, c.Process
			}
		}
		if err != nil {
			return err
		}
		go func() {
			if p.cmd != nil {
				p.cmd.Wait()
			} else {
				p.proc.Wait()
			}
			close(p.done)
		}()
		proc = p
		fmt.Printf("asuka: the player built and started in %.1fs\n", time.Since(start).Seconds())
		return nil
	}
	return loop.run(devPlayer{
		launch: launch,
		stop:   func() { proc.stop() },
		done: func() <-chan struct{} {
			if proc == nil {
				return nil
			}
			return proc.done
		},
	}, stop, o.once)
}

// buildDesktopPlayer builds the dev player for platform on target t in
// work/player: the host, the core without an app, the app's settings
// (on macOS, an .app with the app's Info.plist, icon and Swift, signed
// ad hoc). It returns the executable.
func buildDesktopPlayer(app *asukaApp, platform string, t cbuild.Target, work string, verbose bool) (string, error) {
	dir := filepath.Join(work, "player")
	if platform == platformMac {
		run := macRunner(verbose)
		bundle := filepath.Join(dir, app.displayName()+".app")
		name, err := writeMacBundle(app, bundle, work, false, run)
		if err != nil {
			return "", err
		}
		exe := filepath.Join(bundle, "Contents", "MacOS", name)
		if err := appbuild.Host(t, exe, appbuild.HostOptions{Verbose: verbose, Natives: desktopNatives(app)}); err != nil {
			return "", err
		}
		if err := appbuild.Lib(t, cbuild.Shared, nil, filepath.Join(bundle, "Contents", "Frameworks", coreLibName("darwin"))); err != nil {
			return "", err
		}
		if err := writeHostConfig(app, platform, "", false, filepath.Join(bundle, "Contents", "Resources", boot.ConfigFile)); err != nil {
			return "", err
		}
		if err := macSwift(app, bundle, work, []string{t.Arch}, false, run); err != nil {
			return "", err
		}
		ents, err := writeMacEntitlements(app, work)
		if err != nil {
			return "", err
		}
		return exe, signMac(bundle, "-", ents, run)
	}
	if t.OS == "windows" {
		// One console program: the host and the core, no app.
		exe := filepath.Join(dir, "asuka-player.exe")
		natives, err := windowsNatives(app)
		if err != nil {
			return "", err
		}
		if err := appbuild.WindowsHost(t, appbuild.WindowsOptions{Natives: natives, Console: true}, exe); err != nil {
			return "", err
		}
		return exe, writeHostConfig(app, platform, "", false, filepath.Join(dir, boot.ConfigFile))
	}
	exe := filepath.Join(dir, "asuka-host")
	if err := appbuild.Host(t, exe, appbuild.HostOptions{Verbose: verbose, Natives: desktopNatives(app)}); err != nil {
		return "", err
	}
	if err := appbuild.Lib(t, cbuild.Shared, nil, filepath.Join(dir, coreLibName(t.OS))); err != nil {
		return "", err
	}
	return exe, writeHostConfig(app, platform, "", false, filepath.Join(dir, boot.ConfigFile))
}

// desktopRelease writes a release's core (the app compiled in) and host
// for t: the library at lib, the executable at exe.
func desktopRelease(app *asukaApp, t cbuild.Target, exe, lib string, o *buildOptions, ho appbuild.HostOptions) error {
	m, err := app.compile()
	if err != nil {
		return err
	}
	if err := appbuild.Lib(t, cbuild.Shared, m, lib); err != nil {
		return err
	}
	ho.Release, ho.Verbose = true, o.verbose
	ho.Natives = desktopNatives(app)
	return appbuild.Host(t, exe, ho)
}

// desktopNatives are the directories of the app's own native packages'
// Go for the desktop (a package's desktop/ with Go files), which its
// host is built with; the standard library's are in every host.
func desktopNatives(app *asukaApp) []string {
	ps, err := appNatives(app)
	if err != nil {
		return nil
	}
	var out []string
	for _, p := range ps {
		if strings.HasPrefix(p.path, "asuka/") {
			continue
		}
		d := filepath.Join(p.dir, "desktop")
		if files, _ := filepath.Glob(filepath.Join(d, "*.go")); len(files) > 0 {
			out = append(out, d)
		}
	}
	return out
}
