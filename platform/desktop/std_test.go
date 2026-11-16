package desktop_test

import (
	"fmt"
	"image"
	"image/png"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"

	"github.com/egoist/asuka/internal/appbuild"
	"github.com/egoist/asuka/internal/cbuild"
	"github.com/egoist/asuka/lang/compile"
	"github.com/egoist/asuka/std"
)

// The standard library's C on this computer's desktop, run with no Go:
// each case is an Asuka program that uses a package through its API on
// the test host (test/host.c), every std package's C linked in as in a
// dev player, and what it shows is checked. Linux only (the C is the
// Linux code; run it in the asuka-linux-test image, under xvfb-run and
// dbus-run-session for the cases that need a display or a bus).
func TestStdLinux(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("the standard library's Linux C (run in the asuka-linux-test image)")
	}
	exe := stdHost(t)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/hello":
			w.Header().Set("X-Asuka", "yes")
			fmt.Fprintf(w, "hello %s %s", r.Method, r.Header.Get("X-Who"))
		case "/moved":
			http.Redirect(w, r, "/hello", http.StatusFound)
		case "/signin":
			fmt.Fprint(w, `<!doctype html><title>Sign in</title><script>setTimeout(() => location.href = "myapp://done?code=42", 50)</script>`)
		case "/echo":
			b := make([]byte, r.ContentLength)
			r.Body.Read(b)
			w.Write(b)
		default:
			http.NotFound(w, r)
		}
	}))
	defer srv.Close()
	images := pngFixtures(t)
	for _, c := range []struct {
		name, src string
		want      []string
	}{
		{"store", `import asuka/store

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    store.write("count", 3)
    store.write("name", "Ayu \"quoted\" ü")
    store.flush()
    step = "read {store.read("count", 0)} {store.read("name", "")} {store.read("none", "def")}"
    await sleep(1ms)
    store.remove("count")
    store.flush()
    step = "removed {store.read("count", 0)} in {store.dir().count() > 0}"
    await sleep(1ms)
    step = "done"
  }
}

app { Probe() }
`, []string{`host: read 3 Ayu "quoted" ü def`, "host: removed 0 in true", "host: done"}},
		{"http", `import asuka/http

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let r = try await http.get("` + srv.URL + `/moved", headers: ["X-Who": "probe"])
      step = "got {r.status} {r.text()} {r.headers["x-asuka"] ?? "-"} {r.url.hasSuffix("/hello")}"
      await sleep(1ms)
      let p = try await http.post("` + srv.URL + `/echo", body: "ping".bytes())
      step = "posted {p.status} {p.text()}"
      await sleep(1ms)
      let n = try await http.get("` + srv.URL + `/none")
      step = "missing {n.status}"
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: got 200 hello GET probe yes true", "host: posted 200 ping", "host: missing 404", "host: done"}},
		{"monitors", `import asuka/network
import asuka/power
import asuka/appearance
import asuka/screen

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    for await s in network.changes() {
      step = "network {s.type != .none || !s.connected}"
      break
    }
    await sleep(1ms)
    for await s in power.changes() {
      step = "power {s.level} {s.battery}"
      break
    }
    await sleep(1ms)
    for await a in appearance.changes() {
      step = "appearance {a.increaseContrast}"
      break
    }
    await sleep(1ms)
    for await ds in screen.changes() {
      step = "screens {ds.count() > 0} {ds.count() == 0 || ds[0].primary} {ds.count() == 0 || ds[0].bounds.width > 0}"
      break
    }
    await sleep(1ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: network true", "host: power -1 false", "host: appearance false", "host: screens true true true", "host: done"}},
		{"device", `import asuka/device
import asuka/sensors
import asuka/secure
import asuka/shell

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let i = try await device.info()
      step = "info {i.model} {i.system} {i.appVersion} {i.build} {i.systemVersion.count() > 0}"
      await sleep(1ms)
      step = "sensors {sensors.available(.accelerometer)} {sensors.available(.gyroscope)}"
      await sleep(1ms)
      do {
        try await secure.write("token", "s3cret")
        let back = try await secure.read("token")
        try await secure.remove("token")
        let gone = try await secure.read("token")
        step = "secure {back ?? "-"} {gone ?? "none"}"
      } catch e {
        step = "secure failed: {e.message.contains("keyring") || e.message.contains("libsecret")}"
      }
      await sleep(1ms)
      shell.beep()
      do {
        try await shell.open("/no/such/file")
      } catch e {
        step = "shell {e.message}"
      }
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: info PC Linux 1.2.3 45 true", "host: sensors false false", "host: secure", "host: shell no file at /no/such/file", "host: done"}},
		{"watch", `import asuka/watch
import asuka/fs

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let dir = fs.join([fs.appDir(), "watched"])
      try fs.mkdirAll(dir)
      let w = try await watch.path(dir, ignore: [".*"])
      step = "watching {w.root.hasSuffix("/watched")}"
      await sleep(1ms)
      try fs.writeText(fs.join([dir, ".hidden"]), "h")
      try fs.writeText(fs.join([dir, "a.txt"]), "a")
      for await events in w.events {
        for e in events {
          if e.op == .create && e.path == "a.txt" {
            step = "created {e.path}"
            await sleep(1ms)
            try fs.rename(fs.join([dir, "a.txt"]), fs.join([dir, "b.txt"]))
          } else if e.op == .rename {
            step = "renamed {e.oldPath} {e.path}"
            await sleep(1ms)
            try fs.writeText(fs.join([dir, "b.txt"]), "more")
          } else if e.op == .write {
            step = "wrote {e.path}"
            await sleep(1ms)
            try fs.removeAll(dir)
          } else if e.op == .remove && e.path == "." {
            step = "gone"
            await sleep(1ms)
          } else if e.op == .create {
            step = "fail: created {e.path}"
          }
        }
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: watching true", "host: created a.txt", "host: renamed a.txt b.txt", "host: wrote b.txt", "host: gone", "host: done"}},
		{"js", `import asuka/js

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let a = try await js.eval("1 + 2")
      let b = try await js.eval(r"new Promise(res => setTimeout(() => res({x: [1, 'two']}), 10))")
      step = "js {a} {b}"
      await sleep(1ms)
      do {
        let _ = try await js.eval("throw new Error('nope')")
      } catch e {
        step = "threw {e.message}"
        await sleep(1ms)
      }
      do {
        let _ = try await js.eval(r"new Promise(() => {})", timeout: 300ms)
      } catch e {
        step = "late {e.message}"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{`host: js 3 {"x":[1,"two"]}`, "host: threw Error: nope", "host: late", "host: done"}},
		{"webauth", `import asuka/webauth

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let back = try await webauth.open("` + srv.URL + `/signin", callbackScheme: "myapp", ephemeral: true)
      step = "signed in {back}"
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: signed in myapp://done?code=42", "host: done"}},
		{"markdown", `import asuka/markdown

component Probe {
  state step = "start"
  Column {
    Text("#{step}")
    markdown.View("# Plan\n\nThree **steps**, *in* ` + "`order`" + `:\n\n1. Read [docs](https://asuka.dev)\n2. Write\n\n| a | b |\n|---|---|\n| 1 | 2 |\n") {
      width: 240
    }
  }
  task {
    await sleep(20ms)
    step = "healed {markdown.heal("**bold and ` + "`code" + `")}"
    await sleep(1ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: native view asuka/markdown#Document", "host: measured asuka/markdown#Document 240x", "host: healed **bold and `code`**", "host: done"}},
		{"svg", `import asuka/svg

let tick = svg.Icon(r"""<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 12l5 5L20 6"/><rect x="2" y="2" width="20" height="20" rx="4"/></svg>""")

component Probe {
  state step = "start"
  state color: Color = .accent
  Column {
    Text("#{step}")
    svg.View(tick) { color: color }
    svg.Shape([svg.Layer(path: svg.Path().rect(svg.Point(), svg.Point(fx: 1, fy: 1), radius: 6), fill: .red, fade: .blue)]) {
      width: 100
      height: 40
    }
  }
  task {
    await sleep(20ms)
    color = .label
    await sleep(20ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: native view asuka/svg#View", "host: native view asuka/svg#Shape", "host: measured asuka/svg#View 22x22", "host: done"}},
		{"shader", `import asuka/shader

let glow = shader.Program(r"""
package main

var Time float
var Tint vec4

func Fragment(dst vec4, pos vec2) vec4 {
	d := distance(pos/rectSize(), vec2(0.5))
	return vec4(Tint.rgb*smoothstep(0.5, 0, d)*(0.8+0.2*sin(Time*2)), 1) * Tint.a
}
""")

component Probe {
  state step = "start"
  state tint: [float] = [0.3, 0.6, 1, 1]
  Column {
    Text("#{step}")
    shader.Shader(glow) {
      uniforms: [shader.Uniform(name: "Tint", value: tint)]
      animated: true
      width: 160
      height: 80
    }
  }
  task {
    await sleep(100ms)
    tint = [1, 0, 0, 1]
    await sleep(100ms)
    step = "done"
  }
}

app { Probe() }
`, []string{"host: native view asuka/shader#Shader", "host: done"}},
		{"scan", `import asuka/scan

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let qr = try await scan.file("` + images + `/qr.png")
      step = "qr {qr.map(c => "{c.kind} {c.value}").joined(separator: ", ")}"
      await sleep(1ms)
      let both = try await scan.file("` + images + `/mixed.png")
      step = "mixed {both.count()}"
      await sleep(1ms)
      let ean = try await scan.file("` + images + `/mixed.png", kinds: ["ean13"])
      step = "asked {ean.map(c => "{c.kind} {c.value}").joined(separator: ", ")}"
      await sleep(1ms)
      let upc = try await scan.file("` + images + `/upca.png", kinds: ["ean13"])
      step = "upc as {upc.map(c => "{c.kind} {c.value}").joined(separator: ", ")}"
      await sleep(1ms)
      let empty = try await scan.file("` + images + `/none.png")
      step = "none {empty.count()}"
      await sleep(1ms)
      do {
        let _ = try await scan.file("` + images + `/missing.png")
      } catch e {
        step = "missing threw"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: qr qr https://asuka.dev/scan?q=1", "host: mixed 2", "host: asked ean13 5901234123457", "host: upc as ean13 0036000291452", "host: none 0", "host: missing threw", "host: done"}},
		{"vision", `import asuka/vision

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let r = try await vision.analyze("` + images + `/mixed.png")
      let q = r.codes.filter(c => c.kind == "qr")
      step = "seen {r.width}x{r.height} {r.codes.count()} codes {r.faces.count()} faces {q.count() == 1 && q[0].box.x > 0 && q[0].box.x < 0.2 && q[0].box.w > 0.2 && q[0].box.w < 0.4}"
      await sleep(1ms)
      let t = try await vision.analyze("` + images + `/qr.png", text: false, faces: false)
      step = "codes only {t.codes.count()} {t.lines.count()}"
      await sleep(1ms)
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: seen 426x168 2 codes 0 faces true", "host: codes only 1 0", "host: done"}},
	} {
		t.Run(c.name, func(t *testing.T) {
			dir := t.TempDir()
			os.WriteFile(filepath.Join(dir, "app.asuka"), []byte(c.src), 0o644)
			res, err := compile.Dir(dir, nil)
			if err != nil {
				t.Fatal(err)
			}
			if err := res.Err(); err != nil {
				t.Fatal(err)
			}
			mod := filepath.Join(dir, "app.akb")
			os.WriteFile(mod, res.Module.Encode(), 0o644)
			cmd := exec.Command(exe, mod)
			cmd.Env = append(os.Environ(), "ASUKA_DATA="+dir, noLeaks, "WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1") // WebKit's sandbox doesn't start in a container
			out, err := cmd.CombinedOutput()
			got := string(out)
			if err != nil {
				t.Fatalf("%v\n%s", err, got)
			}
			for _, w := range c.want {
				if !strings.Contains(got, w) {
					t.Errorf("no %q in:\n%s", w, got)
				}
			}
			if strings.Contains(got, "asuka: a shader doesn't run") {
				t.Errorf("a shader didn't run:\n%s", got)
			}
		})
	}
}

// pngFixtures writes testdata/barcode's images (gray, PGM) as PNGs in a
// directory of the test's: what the packages read as a photo.
func pngFixtures(t *testing.T) string {
	dir := t.TempDir()
	pgms, _ := filepath.Glob(filepath.Join("testdata", "barcode", "*.pgm"))
	for _, p := range pgms {
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		// "P5\n<w> <h>\n255\n", then the rows.
		head := strings.SplitN(string(b), "\n", 4)
		var w, h int
		if len(head) < 4 || head[0] != "P5" || head[2] != "255" {
			t.Fatalf("%s isn't a PGM", p)
		}
		fmt.Sscanf(head[1], "%d %d", &w, &h)
		img := image.NewGray(image.Rect(0, 0, w, h))
		copy(img.Pix, b[len(b)-w*h:])
		f, err := os.Create(filepath.Join(dir, strings.TrimSuffix(filepath.Base(p), ".pgm")+".png"))
		if err != nil {
			t.Fatal(err)
		}
		png.Encode(f, img)
		f.Close()
	}
	return dir
}

var (
	stdOnce sync.Once
	stdExe  string
	stdErr  error
)

// stdHost builds the test host with every standard package's C for this
// computer's desktop (ASan, UBSan), once.
func stdHost(t *testing.T) string {
	stdOnce.Do(func() {
		core, err := cbuild.CoreDir()
		if err != nil {
			stdErr = err
			return
		}
		d, err := std.Dir()
		if err != nil {
			stdErr = err
			return
		}
		dirs, _ := filepath.Glob(filepath.Join(d, "*", "desktop"))
		var pkgs []string
		for _, x := range dirs {
			pkgs = append(pkgs, filepath.Dir(x))
		}
		natives, err := appbuild.DesktopNatives(cbuild.Host(), pkgs)
		if err != nil {
			stdErr = err
			return
		}
		desk, _ := filepath.Abs(".")
		srcs := []string{filepath.Join(desk, "test", "host.c")}
		for _, s := range natives.Sources {
			if strings.HasSuffix(s, ".c") {
				srcs = append(srcs, s)
			}
		}
		work, err := os.MkdirTemp("", "asuka-std-host")
		if err != nil {
			stdErr = err
			return
		}
		flags := append(append([]string{}, cbuild.Flags...), natives.Flags...)
		stdExe, stdErr = cbuild.Exe(core, srcs, flags, natives.Libs, true, work, "asuka-std-host")
	})
	if stdErr != nil {
		t.Fatal(stdErr)
	}
	return stdExe
}
