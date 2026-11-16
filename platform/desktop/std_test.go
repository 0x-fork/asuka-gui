package desktop_test

import (
	"bufio"
	"fmt"
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
	// Half a second of silence, a WAV for the media packages.
	media := t.TempDir()
	wav := filepath.Join(media, "tone.wav")
	os.WriteFile(wav, silentWAV(22050, 22050/2), 0o644)
	// Images and a video for imageedit: a PNG half clear, a JPEG that
	// EXIF turns upright, a second of Motion JPEG.
	pngFile, jpegFile, video := filepath.Join(media, "half.png"), filepath.Join(media, "turned.jpg"), filepath.Join(media, "clip.avi")
	os.WriteFile(pngFile, testPNG(40, 20), 0o644)
	os.WriteFile(jpegFile, orientedJPEG(40, 20, 6), 0o644)
	os.WriteFile(video, mjpegAVI(32, 24, 10, 10), 0o644)
	// A tone for the microphone; a Speech Dispatcher of the test's.
	t.Setenv("ASUKA_RECORD_SOURCE", "audiotestsrc is-live=true")
	ssip := fakeSSIP(t)
	for _, c := range []struct {
		name, src string
		want      []string
		drive     func(t *testing.T, pid int, line string) // each line the host prints, as it does
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
`, []string{`host: read 3 Ayu "quoted" ü def`, "host: removed 0 in true", "host: done"}, nil},
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
`, []string{"host: got 200 hello GET probe yes true", "host: posted 200 ping", "host: missing 404", "host: done"}, nil},
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
`, []string{"host: network true", "host: power -1 false", "host: appearance false", "host: screens true true true", "host: done"}, nil},
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
`, []string{"host: info PC Linux 1.2.3 45 true", "host: sensors false false", "host: secure", "host: shell no file at /no/such/file", "host: done"}, nil},
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
`, []string{"host: watching true", "host: created a.txt", "host: renamed a.txt b.txt", "host: wrote b.txt", "host: gone", "host: done"}, nil},
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
`, []string{`host: js 3 {"x":[1,"two"]}`, "host: threw Error: nope", "host: late", "host: done"}, nil},
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
`, []string{"host: signed in myapp://done?code=42", "host: done"}, nil},
		{"audio", `import asuka/audio

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    let p = audio.Player("` + wav + `")
    p.setVolume(0.5)
    p.play()
    var s = await p.status()
    var turns = 0
    while !s.ended && turns < 400 {
      await sleep(10ms)
      s = await p.status()
      turns += 1
    }
    step = "played {s.ended} {s.duration > 0.4 && s.duration < 0.6}"
    await sleep(1ms)
    p.release()
    audio.play("` + wav + `")
    step = "done"
  }
}

app { Probe() }
`, []string{"host: played true true", "host: done"}, nil},
		{"record", `import asuka/record
import asuka/audio

async fn probe() throws -> string {
  await sleep(500ms)
  let s = record.audioStatus()
  var busy = "free"
  do {
    let _ = try await record.audio()
  } catch record.RecordError.busy {
    busy = "busy"
  }
  record.pauseAudio()
  let p = record.audioStatus()
  await sleep(100ms)
  record.resumeAudio()
  let r = record.audioStatus()
  return "{s.recording} {s.elapsed > 300ms} {s.level > 0} {busy} {p.recording} {p.level} {r.recording}"
}

async fn stopSoon() -> bool {
  await sleep(300ms)
  record.stopAudio()
  return true
}

async fn duration(path: string) -> float {
  let p = audio.Player(path)
  var s = await p.status()
  var turns = 0
  while s.duration == 0 && turns < 300 {
    await sleep(10ms)
    s = await p.status()
    turns += 1
  }
  p.release()
  return s.duration
}

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let (path, seen) = try await all(record.audio(maxDuration: 1500ms), probe())
      step = "status {seen}"
      await sleep(1ms)
      let d = await duration(path)
      step = "recorded {path.hasSuffix(".m4a")} {d > 1.2 && d < 1.8}"
      await sleep(1ms)
      let (short, _) = try await all(record.audio(quality: .music), stopSoon())
      let ds = await duration(short)
      step = "stopped {ds > 0.1 && ds < 0.6}"
      await sleep(1ms)
      do {
        try await withTimeout(300ms) {
          let _ = try await record.audio()
        }
      } catch e {
        await sleep(300ms)
        step = "cancelled {record.audioStatus().recording}"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: status true true true busy false 0 true", "host: recorded true true", "host: stopped true", "host: cancelled false", "host: done"}, nil},
		{"speech", `import asuka/speech

async fn speakLong() throws -> string {
  do {
    try await speech.speak("long")
    return "ended"
  } catch speech.SpeechError.stopped {
    return "stopped"
  }
}

async fn stopSoon() -> bool {
  await sleep(200ms)
  let was = speech.speaking()
  speech.stop()
  return was
}

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let vs = try await speech.voices()
      step = "voices {vs.count()} {vs[0].id} {vs[0].language} {vs[1].language}"
      await sleep(1ms)
      try await speech.speak("Hello\n.dot", options: speech.Options(language: "en_US", rate: 2, volume: 0.5))
      step = "spoke {speech.speaking()}"
      await sleep(1ms)
      let (a, b) = try await all(speakLong(), stopSoon())
      step = "{a} {b} {speech.speaking()}"
      await sleep(1ms)
      do {
        try await withTimeout(200ms) {
          try await speech.speak("long")
        }
      } catch e {
        step = "cancelled {speech.speaking()}"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: voices 2 kal en-US de", "host: spoke false", "host: stopped true false", "host: cancelled false", "host: done"}, func(t *testing.T, pid int, line string) {
			if line != "host: done" {
				return
			}
			got := ssip.log()
			for _, w := range []string{"SET self NOTIFICATION ALL on", "SET self RATE 100", "SET self VOLUME 0", "SET self PITCH 0", "SET self LANGUAGE en-us", "Hello", "..dot", "CANCEL self"} {
				if !strings.Contains(got, "\n"+w+"\n") {
					t.Errorf("Speech Dispatcher heard no %q:\n%s", w, got)
				}
			}
		}},
		{"nowplaying", `import asuka/nowplaying

component Probe {
  state step = "start"
  Text("#{step}")
  nowplaying.NowPlaying {
    title: "Song"
    artist: "Someone"
    album: "Record"
    duration: 120
    position: 3
    state: .playing
    on play { step = "play" }
    on pause { step = "done" }
    on next { step = "next" }
    on seek(p) { step = "seek {p}" }
  }
  task {
    await sleep(50ms)
    step = "showing"
  }
}

app { Probe() }
`, []string{"host: showing", "host: next", "host: seek 60", "host: done"}, func(t *testing.T, pid int, line string) {
			dest := fmt.Sprintf("org.mpris.MediaPlayer2.Test.instance%d", pid)
			send := func(args ...string) string {
				out, err := exec.Command("dbus-send", append([]string{"--session", "--print-reply", "--dest=" + dest, "/org/mpris/MediaPlayer2"}, args...)...).CombinedOutput()
				if err != nil {
					t.Errorf("dbus-send %v: %v\n%s", args, err, out)
				}
				return string(out)
			}
			switch line {
			case "host: showing":
				md := send("org.freedesktop.DBus.Properties.Get", "string:org.mpris.MediaPlayer2.Player", "string:Metadata")
				status := send("org.freedesktop.DBus.Properties.Get", "string:org.mpris.MediaPlayer2.Player", "string:PlaybackStatus")
				if !strings.Contains(md, `"Song"`) || !strings.Contains(md, `"Someone"`) || !strings.Contains(md, `"Record"`) || !strings.Contains(status, `"Playing"`) {
					t.Errorf("metadata:\n%s\n%s", md, status)
				}
				send("org.mpris.MediaPlayer2.Player.Next")
			case "host: next":
				// Seconds as microseconds; the track as Metadata's trackid names it.
				md := send("org.freedesktop.DBus.Properties.Get", "string:org.mpris.MediaPlayer2.Player", "string:Metadata")
				i := strings.Index(md, "object path \"")
				if i < 0 {
					t.Errorf("no trackid in %s", md)
					return
				}
				track := md[i+len("object path \""):]
				track = track[:strings.Index(track, "\"")]
				send("org.mpris.MediaPlayer2.Player.SetPosition", "objpath:"+track, "int64:60000000")
			case "host: seek 60":
				send("org.mpris.MediaPlayer2.Player.Pause")
			}
		}},
		{"imageedit", `import asuka/imageedit
import asuka/fs

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let s = try await imageedit.size("` + pngFile + `")
      let j = try await imageedit.size("` + jpegFile + `")
      step = "size {s.width}x{s.height} {j.width}x{j.height} {s.path.isEmpty()}"
      await sleep(1ms)
      let a = try await imageedit.edit("` + pngFile + `", actions: [.resize(width: 20, height: 0), .rotate(degrees: 90), .flipHorizontal, .flipVertical, .fill(x: 0, y: 0, width: 4, height: 4, red: 0, green: 255, blue: 0), .pixelate(x: 0, y: 0, width: 10, height: 10, block: 3)], format: .png)
      let back = try await imageedit.size(a.path)
      let ab = try fs.read(a.path)
      step = "png {a.width}x{a.height} {back.width}x{back.height} {a.path.hasSuffix(".png")} {ab.slice(1, 4).string()}"
      await sleep(1ms)
      let b = try await imageedit.edit("` + jpegFile + `", actions: [.crop(x: 2, y: 4, width: 10, height: 30), .rotate(degrees: 45)], format: .jpeg, quality: 0.5)
      let bb = try fs.read(b.path)
      step = "jpeg {b.width}x{b.height} {b.path.hasSuffix(".jpg")} {bb.get(0) ?? 0} {bb.get(1) ?? 0}"
      await sleep(1ms)
      let u = try await imageedit.edit("` + jpegFile + `")
      step = "upright {u.width}x{u.height}"
      await sleep(1ms)
      do {
        let _ = try await imageedit.edit("/no/such.png")
      } catch imageedit.EditError.noImage {
        step = "no image"
        await sleep(1ms)
      }
      do {
        let _ = try await imageedit.edit("` + pngFile + `", actions: [.crop(x: 100, y: 100, width: 5, height: 5)])
      } catch e {
        step = "crop {e.message}"
        await sleep(1ms)
      }
      let v = try await imageedit.videoFrame("` + video + `", at: 500ms, actions: [.resize(width: 16, height: 0)])
      step = "video {v.width}x{v.height} {v.path.hasSuffix(".jpg")}"
      await sleep(1ms)
      do {
        let _ = try await imageedit.videoFrame("` + pngFile + `.none")
      } catch imageedit.EditError.noImage {
        step = "no video"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: size 40x20 20x40 true", "host: png 10x20 10x20 true PNG", "host: jpeg 29x29 true 255 216", "host: upright 20x40",
			"host: no image", "host: crop the crop is outside the 40×20 image", "host: video 16x12 true", "host: no video", "host: done"}, nil},
		{"print", `import asuka/print
import asuka/fs

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    do {
      let p = try await print.pdf("<!doctype html><title>Report: Q1</title><h1>Hello</h1><p>A page.</p>", page: print.Page.a4)
      let data = try fs.read(p)
      step = "pdf {fs.base(p)} {data.slice(0, 5).string()} {data.count() > 500}"
      await sleep(1ms)
      let q = try await print.pdf("<p>untitled</p>", landscape: true, margins: print.Margins(top: 0, right: 0, bottom: 0, left: 0))
      step = "untitled {fs.base(q)} {fs.exists(q)}"
      await sleep(1ms)
      do {
        let _ = try await print.file("/no/such/file.pdf")
      } catch e {
        step = "file {e.message}"
        await sleep(1ms)
      }
      step = "done"
    } catch e {
      step = "fail {e.message}"
    }
  }
}

app { Probe() }
`, []string{"host: pdf Report- Q1.pdf %PDF- true", "host: untitled Document.pdf true", "host: file no file at /no/such/file.pdf", "host: done"}, nil},
		{"purchase", `import asuka/purchase

component Probe {
  state step = "start"
  Text("#{step}")
  task {
    step = "available {purchase.available()}"
    await sleep(1ms)
    do {
      let _ = try await purchase.products([purchase.Item(id: "coins", kind: .consumable)])
    } catch purchase.PurchaseError.notAvailable {
      step = "not available"
      await sleep(1ms)
    }
    step = "done"
  }
}

app { Probe() }
`, []string{"host: available false", "host: not available", "host: done"}, nil},
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
			var out strings.Builder
			if c.drive == nil {
				cmd.Stdout, cmd.Stderr = &out, &out
				err = cmd.Run()
			} else {
				pipe, _ := cmd.StdoutPipe()
				cmd.Stderr = &out
				if err = cmd.Start(); err == nil {
					lines := bufio.NewScanner(pipe)
					for lines.Scan() {
						out.WriteString(lines.Text() + "\n")
						c.drive(t, cmd.Process.Pid, lines.Text())
					}
					err = cmd.Wait()
				}
			}
			got := out.String()
			if err != nil {
				t.Fatalf("%v\n%s", err, got)
			}
			for _, w := range c.want {
				if !strings.Contains(got, w) {
					t.Errorf("no %q in:\n%s", w, got)
				}
			}
		})
	}
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
		// ASUKA_STD_ONLY=audio,http: only those packages' C (a case that
		// imports another fails), for working on some while others don't
		// build.
		only := map[string]bool{}
		for _, n := range strings.Split(os.Getenv("ASUKA_STD_ONLY"), ",") {
			if n != "" {
				only[n] = true
			}
		}
		d, err := std.Dir()
		if err != nil {
			stdErr = err
			return
		}
		dirs, _ := filepath.Glob(filepath.Join(d, "*", "desktop"))
		var pkgs []string
		for _, x := range dirs {
			if len(only) > 0 && !only[filepath.Base(filepath.Dir(x))] {
				continue
			}
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

// silentWAV is n samples of 16-bit mono silence at rate, as a WAV file.
func silentWAV(rate, n int) []byte {
	le := func(b []byte, v uint32, size int) []byte {
		for i := 0; i < size; i++ {
			b = append(b, byte(v>>(8*i)))
		}
		return b
	}
	data := n * 2
	b := []byte("RIFF")
	b = le(b, uint32(36+data), 4)
	b = append(b, "WAVEfmt "...)
	b = le(b, 16, 4)
	b = le(b, 1, 2) // PCM
	b = le(b, 1, 2) // mono
	b = le(b, uint32(rate), 4)
	b = le(b, uint32(rate*2), 4)
	b = le(b, 2, 2)
	b = le(b, 16, 2)
	b = append(b, "data"...)
	b = le(b, uint32(data), 4)
	return append(b, make([]byte, data)...)
}
