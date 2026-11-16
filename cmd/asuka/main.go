// Command Asuka makes, runs and builds Asuka apps.
//
//	asuka new <dir>                          a project from the template
//	asuka dev --platform ios <dir>           build, run on the iOS simulator or an iPhone, rebuild on save
//	asuka dev --platform android <dir>       build, run on an Android emulator or device, rebuild on save
//	asuka dev --platform mac <dir>           build, run on this Mac, rebuild on save
//	asuka dev --platform windows <dir>       build, run on this PC, rebuild on save
//	asuka dev --platform linux <dir>         build, run on this Linux machine, rebuild on save
//	asuka dev --platform web <dir>           build, serve and open in the browser, rebuild on save
//	asuka dev <dir>                          on macOS, Windows or Linux: run on this computer
//	asuka test [dir]                         run an app's or a package's test blocks
//	asuka run file.asuka                     run a file's tests, or show its app
//	asuka fmt [-w] [-l] [paths]              format Asuka source (.asuka files)
//	asuka check [dir]                        report an Asuka package's problems
//	asuka test [-run pattern] [dir]          run an Asuka package's tests on the VM
//	asuka run file.asuka                     run a file's fn main() (or its tests) on the VM
//	asuka lsp                                the language server, for editors
package main

import (
	"errors"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"github.com/egoist/asuka/internal/appbuild"
)

// version is the CLI's release (-ldflags -X main.version=…; scripts/npm
// sets it), "dev" for a build from source.
var version = "dev"

func usage() {
	fmt.Fprint(os.Stderr, `usage:
  asuka new <dir>                           make a project from the template
  asuka dev [-p platform] [flags] [dir]     run, rebuilding on save, on platform: ios (the
                                            simulator), android (an emulator or device), mac,
                                            windows, linux or web (the browser); default on
                                            macOS, Windows and Linux: this computer
  asuka build [-p platform] [flags] [dir]   a release build for platform, likewise
  asuka test [-run pattern] [dir]           run the test blocks of an app or a package on the fake host
  asuka run file.asuka                      run a file's test blocks, or show what its app shows
  asuka publish -p platform [flags] [dir]
                                            a release build, uploaded to TestFlight or Google Play;
                                            mac, windows, linux: its update, to asuka.toml's updates
  asuka update keygen                       make the key pair that signs a desktop app's updates
  asuka plugin publish [dir]                upload a plugin's built libraries, record them in its manifest
  asuka native generate|check|new|add       native packages: Go interfaces their Swift, Kotlin, JavaScript implement
  asuka generate [dir]                      write the functions of the app's catalogs
  asuka i18n init|add|check                 start, add languages to, and check the catalogs
  asuka fmt [-w] [-l] [paths]               format Asuka source (.asuka files) in its canonical form
  asuka add <package>[@range]               add dependencies: asuka.toml, asuka.lock, fetched
  asuka remove <package>                    remove dependencies
  asuka install [-update] [dir]             fetch what asuka.lock names (resolving again with -update)
  asuka pkg pack|publish [dir]              pack, or publish to the npm registry, a package
  asuka version                             print the CLI's version
  asuka check [dir]                         report an Asuka package's problems (file:line:col: message)
  asuka test [-run pattern] [-v] [dir]      run an Asuka package's test blocks on the VM
  asuka run file.asuka                      run a file's fn main() (or its tests) on the VM
  asuka lsp                                 run the language server, for editors (LSP on stdio)

asuka <command> --help lists its flags.
`)
}

func main() {
	appbuild.Version = version
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	var err error
	switch os.Args[1] {
	case "new":
		err = cmdNew(os.Args[2:])
	case "dev":
		err = cmdDev(os.Args[2:])
	case "build":
		err = cmdBuild(os.Args[2:])
	case "publish":
		err = cmdPublish(os.Args[2:])
	case "generate":
		err = cmdGenerate(os.Args[2:])
	case "i18n":
		err = cmdI18n(os.Args[2:])
	case "update":
		err = cmdUpdate(os.Args[2:])
	case "plugin":
		err = cmdPlugin(os.Args[2:])
	case "native":
		err = cmdNative(os.Args[2:])
	case "test":
		err = cmdTest(os.Args[2:])
	case "run":
		err = cmdRun(os.Args[2:])
	case "fmt":
		err = cmdFmt(os.Args[2:])
	case "add":
		err = cmdAdd(os.Args[2:])
	case "remove":
		err = cmdRemove(os.Args[2:])
	case "install":
		err = cmdInstall(os.Args[2:])
	case "pkg":
		err = cmdPkg(os.Args[2:])
	case "version", "--version":
		fmt.Println("asuka", version)
		return
	case "check":
		err = cmdCheck(os.Args[2:])
	case "lsp":
		err = cmdLSP(os.Args[2:])
	case "-h", "--help", "help":
		usage()
		return
	default:
		usage()
		os.Exit(2)
	}
	if errors.Is(err, errHelp) {
		return
	}
	if errors.Is(err, errReported) {
		os.Exit(1)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "asuka:", err)
		os.Exit(1)
	}
}

type devOptions struct {
	platform  string
	port      int  // web: the dev server's port
	lan       bool // web: served to the network too, for phones
	noOpen    bool // web: don't open the browser
	device    string
	team      string // iOS: the team that signs for a device
	once      bool
	verbose   bool
	lang, dir string
}

func devFlags(o *devOptions) *flagSet {
	fs := newFlagSet("dev", "[dir]")
	fs.about = "Runs the app, and runs it again with each change you save."
	platformFlag(fs, &o.platform, []string{platformIOS, platformAndroid, platformMac, platformWindows, platformLinux, platformWeb},
		"the `platform` to run on: ios (the simulator, or an iPhone with --device), android (a device or emulator), mac (this Mac), windows (this PC), linux (this Linux machine, or ASUKA_LINUX_RUN's), or web (the browser: the core as WebAssembly, the views the page's DOM); default on macOS, Windows and Linux: this computer")
	fs.Int(&o.port, "port", 0, 0, "web: the dev server's `port` (default 8790, or a free one)")
	fs.Bool(&o.lan, "lan", 0, "web: serve to this network too (a phone's browser), not only this machine")
	fs.Bool(&o.noOpen, "no-open", 0, "web: don't open the page in the browser")
	fs.String(&o.device, "device", 'd', "", "iOS: a simulator's or a connected iPhone's or iPad's name or UDID, or iphone for the one connected (default: the booted simulator, else the newest iPhone simulator); Android: adb serial or AVD name")
	fs.String(&o.team, "team", 0, "", "iOS, on a device: the Apple Developer `team` ID that signs the player and the app (default: ASUKA_TEAM_ID, asuka.toml's ios.team, else the keychain's one Apple Development team)")
	fs.Bool(&o.once, "once", 0, "launch the app once, without watching for changes")
	fs.String(&o.lang, "lang", 0, "", "show this `language` for this run, as de or zh-Hans, or a pseudo locale: en-XA (accented, a third longer) or ar-XB (right to left)")
	fs.String(&o.dir, "dir", 0, "", "lay the app out in this `direction` for this run, rtl or ltr, whatever its language")
	fs.Bool(&o.verbose, "verbose", 'v', "print the commands asuka runs")
	return fs
}

func cmdDev(args []string) error {
	var o devOptions
	fs := devFlags(&o)
	if err := fs.parse(args); err != nil {
		return err
	}
	if err := setDevLocale(o.lang, o.dir); err != nil {
		return err
	}
	dir := "."
	if fs.Arg(0) != "" {
		dir = fs.Arg(0)
	}
	if o.platform == "" {
		if o.platform = thisComputer(); o.platform == "" {
			return errors.New("pick a platform: asuka dev --platform ios|android|web <dir>")
		}
	}
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	switch o.platform {
	case platformWeb:
		return devWeb(dir, &o, stop)
	case platformMac, platformWindows, platformLinux:
		return devDesktop(dir, &o, stop, o.platform)
	case platformAndroid:
		return devAndroid(dir, &o, stop)
	}
	return devIOS(dir, &o, stop)
}
