// Package core holds Asuka's C core. This test builds each
// core/test/*_test.c with the whole core and the fake host, under
// AddressSanitizer and UndefinedBehaviorSanitizer, and runs it:
//
//	go test ./core              every C test
//	go test ./core -run Layout  core/test/layout_test.c alone
package core

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"github.com/egoist/asuka/internal/cbuild"
)

func TestC(t *testing.T) {
	cc, err := cbuild.Find()
	if err != nil {
		t.Skip(err)
	}
	root, _ := filepath.Abs(".")
	srcs, err := cbuild.Sources(root, true)
	if err != nil {
		t.Fatal(err)
	}
	flags := append(cbuild.Includes(root), "-g", "-O1", "-fno-omit-frame-pointer", "-DAK_TEST=1")
	if runtime.GOOS != "windows" {
		flags = append(flags, "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined")
	}
	tmp := t.TempDir()
	objs, err := cc.Compile(srcs, filepath.Join(tmp, "core"), flags)
	if err != nil {
		t.Fatal(err)
	}
	tests, _ := filepath.Glob(filepath.Join(root, "test", "*_test.c"))
	for _, src := range tests {
		name := strings.TrimSuffix(filepath.Base(src), "_test.c")
		t.Run(strings.ToUpper(name[:1])+name[1:], func(t *testing.T) {
			obj, err := cc.Compile([]string{src}, filepath.Join(tmp, name), flags)
			if err != nil {
				t.Fatal(err)
			}
			exe := filepath.Join(tmp, name+"_test")
			link := []string{"-lm"}
			if runtime.GOOS != "windows" {
				link = append(link, "-fsanitize=address,undefined")
			}
			if err := cc.Link(append(obj, objs...), exe, link); err != nil {
				t.Fatal(err)
			}
			cmd := exec.Command(exe)
			cmd.Dir = filepath.Join(root, "test")
			cmd.Env = append(os.Environ(), "ASUKA_ROOT="+filepath.Dir(root))
			if testing.Verbose() {
				cmd.Env = append(cmd.Env, "T_VERBOSE=1")
			}
			out, err := cmd.CombinedOutput()
			if err != nil || testing.Verbose() {
				t.Logf("%s", out)
			}
			if err != nil {
				t.Fatal(err)
			}
		})
	}
}
