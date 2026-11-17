// Command Asuka makes, runs and builds Asuka apps.
//
//	asuka new [--bin | --lib [--native]] <dir>  an app, a program (fn main), or a library (with a native module)
//	asuka dev --platform ios <dir>           build, run on the iOS simulator or an iPhone, rebuild on save
//	asuka dev --platform android <dir>       build, run on an Android emulator or device, rebuild on save
//	asuka dev --platform mac <dir>           build, run on this Mac, rebuild on save
//	asuka dev --platform windows <dir>       build, run on this PC, rebuild on save
//	asuka dev --platform linux <dir>         build, run on this Linux machine, rebuild on save
//	asuka dev --platform web <dir>           build, serve and open in the browser, rebuild on save
//	asuka dev <dir>                          on macOS, Windows or Linux: run on this computer
//	asuka test [dir]                         run an app's or a package's test blocks
//	asuka run [--bin name] [dir] [-- args]   run a program on the VM; a file's tests, or show its app
//	asuka fmt [-w] [-l] [paths]              format Asuka source (.asuka files)
//	asuka doc [package] [name]               a package's documentation, as Markdown
//	asuka check [--json] [dir]               report an Asuka package's problems, each with its code
//	asuka explain [code]                     a diagnostic code's page: why, how to fix, examples
//	asuka guide                              the language on one page, for coding agents
//	asuka lsp                                the language server, for editors
//	asuka cache stats|clean                  the build cache's size and hits; empty it
package main

import (
	"errors"
	"fmt"
	"os"
	"os/signal"
	"runtime"
	"slices"
	"syscall"

	"github.com/egoist/asuka/internal/ccache"
)

// version is the CLI's release (-ldflags -X main.version=…; scripts/npm
// sets it), "dev" for a build from source.
var version = "dev"

func usage() {
	fmt.Fprint(os.Stderr, `usage:
  asuka new [--bin | --lib [--native]] <dir>
                                            make an app, a program (a command-line tool), or a library
                                            (with --native, its platforms' code)
  asuka dev [-p platform] [flags] [dir]     run, rebuilding on save, on platform: ios (the
                                            simulator), android (an emulator or device), mac,
                                            windows, linux or web (the browser); default on
                                            macOS, Windows and Linux: this computer
  asuka build [-p platform] [flags] [dir]   a release build for platform, likewise; a program's
                                            executable (--bin name, --target os-arch)
  asuka test [-run pattern] [dir]           run the test blocks of an app or a package on the fake host
  asuka run [--bin name] [dir] [-- args]    run a program's main, with its arguments; a file without
                                            one: its test blocks, and what its app shows
  asuka publish -p platform [flags] [dir]
                                            a release build, uploaded to TestFlight or Google Play;
                                            mac, windows, linux: its update, to asuka.toml's updates
  asuka update keygen                       make the key pair that signs a desktop app's updates
  asuka native generate|check|new|add       native packages: Asuka declarations their Swift, Kotlin, JavaScript and C implement
  asuka generate [dir]                      write the functions of the app's catalogs
  asuka i18n init|add|check                 start, add languages to, and check the catalogs
  asuka fmt [-w] [-l] [paths]               format Asuka source (.asuka files) in its canonical form
  asuka doc [package] [name]                a package's documentation (std/time, platform/camera, ./util), as Markdown
  asuka add <package>[@range]               add dependencies: asuka.toml, asuka.lock, fetched
  asuka remove <package>                    remove dependencies
  asuka install [-update] [dir]             fetch what asuka.lock names (resolving again with -update)
  asuka pkg pack|publish [dir]              pack, or publish to the npm registry, a package
  asuka cache stats|clean                   the build cache's size and hits; empty it
  asuka version                             print the CLI's version
  asuka check [--json] [dir]                report an Asuka package's problems (file:line:col: error[code]: message)
  asuka explain [code]                      print a diagnostic code's page: why, how to fix, a wrong and a right program
  asuka guide                               print the language on one page, the workflow and every code, for coding agents
  asuka lsp                                 run the language server, for editors (LSP on stdio)

asuka <command> --help lists its flags.
`)
}

func main() {
	if runtime.GOOS == "darwin" && runtime.GOARCH == "amd64" {
		fmt.Fprintln(os.Stderr, "asuka: Asuka develops on Macs with Apple silicon, not Intel Macs (the apps it builds run on both: asuka build --arch amd64)")
		os.Exit(1)
	}
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
	case "native":
		err = cmdNative(os.Args[2:])
	case "test":
		err = cmdTest(os.Args[2:])
	case "run":
		err = cmdRun(os.Args[2:])
	case "fmt":
		err = cmdFmt(os.Args[2:])
	case "doc":
		err = cmdDoc(os.Args[2:])
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
	case "explain":
		err = cmdExplain(os.Args[2:])
	case "guide":
		err = cmdGuide(os.Args[2:])
	case "cache":
		err = cmdCache(os.Args[2:])
	case "prebuilt":
		// A release's: what it carries prebuilt (scripts/npm).
		err = cmdPrebuilt(os.Args[2:])
	case "lsp":
		err = cmdLSP(os.Args[2:])
	case "-h", "--help", "help":
		usage()
		return
	default:
		usage()
		os.Exit(2)
	}
	ccache.Flush() // the build cache's counts; it's trimmed to its cap
	if errors.Is(err, errHelp) {
		return
	}
	if errors.Is(err, errReported) {
		os.Exit(1)
	}
	var code exitCode
	if errors.As(err, &code) {
		os.Exit(int(code))
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
	bin       string // a program of asuka.toml's [bin.<name>]
}

func devFlags(o *devOptions) *flagSet {
	fs := newFlagSet("dev", "[dir] [-- args...]")
	fs.about = "Runs the app, and runs it again with each change you save. A program (fn main,\n" +
		"or --bin) runs on the VM with the arguments after --, again with each change."
	fs.String(&o.bin, "bin", 0, "", "run the `program` asuka.toml's [bin.<name>] names, instead of the app")
	platformFlag(fs, &o.platform, []string{platformIOS, platformAndroid, platformMac, platformWindows, platformLinux, platformWeb},
		"the `platform` to run on: ios (the simulator, or an iPhone with --device), android (a device or emulator), mac (this Mac), windows (this PC), linux (this Linux machine, or ASUKA_LINUX_RUN's), or web (the browser: the core as WebAssembly, the views the page's DOM); default on macOS, Windows and Linux: this computer")
	fs.Int(&o.port, "port", 0, 0, "web: the dev server's `port` (default 8790, or a free one)")
	fs.Bool(&o.lan, "lan", 0, "web: serve to this network too (a phone's browser), not only this machine")
	fs.Bool(&o.noOpen, "no-open", 0, "web: don't open the page in the browser")
	fs.String(&o.device, "device", 'd', "", "iOS: a simulator's or a connected iPhone's or iPad's name or UDID, or iphone for the one connected (default: the booted simulator, else the newest iPhone simulator); Android: adb serial or AVD name")
	fs.String(&o.team, "team", 0, "", "iOS, on a device: the Apple Developer `team` ID that signs the player and the app (default: ASUKA_TEAM_ID, asuka.toml's ios.team, else the keychain's one Apple Development team)")
	fs.Bool(&o.once, "once", 0, "build and launch the app, and return once it runs (it keeps running), without watching for changes")
	fs.String(&o.lang, "lang", 0, "", "show this `language` for this run, as de or zh-Hans, or a pseudo locale: en-XA (accented, a third longer) or ar-XB (right to left)")
	fs.String(&o.dir, "dir", 0, "", "lay the app out in this `direction` for this run, rtl or ltr, whatever its language")
	fs.Bool(&o.verbose, "verbose", 'v', "print the commands asuka runs")
	return fs
}

func cmdDev(args []string) error {
	var o devOptions
	var progArgs []string
	if i := slices.Index(args, "--"); i >= 0 {
		args, progArgs = args[:i], args[i+1:]
	}
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
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	if o.bin != "" || (o.platform == "" && isProgramDir(dir)) {
		return devProgram(dir, o.bin, progArgs, stop)
	}
	if o.platform == "" {
		if o.platform = thisComputer(); o.platform == "" {
			return errors.New("pick a platform: asuka dev --platform ios|android|web <dir>")
		}
	}
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
