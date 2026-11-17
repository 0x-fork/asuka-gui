package windows_test

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The crash record on Windows (testdata/crash.asuka): an error recorded
// is a report at once; an access violation nothing handles (the host's
// exception filter) is one at the next launch, fatal.
func TestCrashRecord(t *testing.T) {
	w := onWindows(t)
	local, guest := w.job(t)
	src, err := os.ReadFile(filepath.Join("testdata", "crash.asuka"))
	if err != nil {
		t.Fatal(err)
	}
	buildApp(t, w, string(src), local)
	run := func(name string, steps ...string) string {
		t.Helper()
		snap := guest + `\` + name + ".png"
		env := map[string]string{
			"ASUKA_DATA":     guest + `\data`,
			"ASUKA_DUMP":     "1",
			"ASUKA_SNAPSHOT": snap,
			"ASUKA_SCRIPT":   strings.Join(append(steps, "snap:"+snap), ";"),
		}
		out, _ := w.run(t, guest, guest+`\app.exe`, env)
		b, _ := os.ReadFile(filepath.Join(local, name+".png.txt"))
		return string(b) + "\n" + out
	}
	first := run("first", "wait:1s", "tap:Record", "wait:1s")
	shows(t, first, "the first run", []string{"Reports: 1", "nonfatal false: Oops.broke"})
	run("fault", "wait:1s", "fault", "wait:2s")
	second := run("second", "wait:2s")
	if !strings.Contains(second, "Reports: 2") || !strings.Contains(second, "exception true: EXCEPTION_ACCESS_VIOLATION") {
		t.Errorf("the access violation isn't a report at the next launch:\n%s", second)
	}
}
