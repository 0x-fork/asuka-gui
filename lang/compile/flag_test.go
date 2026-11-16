package compile

import (
	"strings"
	"testing"
)

// flag.parse<T> takes what the compiler knows of T: what it can't make
// a command line of is a compile error, at the field.
func TestFlagSpecErrors(t *testing.T) {
	for _, c := range []struct{ decl, want string }{
		{"struct O { verbose: bool }", "O.verbose: a switch has a default (verbose = false)"},
		{"struct O {\n  /// -h Help me.\n  hide: bool = false\n}", "O.hide: -h is --help's"},
		{"struct O {\n  /// -a A.\n  a: bool = false\n  /// -a B.\n  b: bool = false\n}", "O.b: -a is a's already"},
		{"struct O { at: [string: int] = [:] }", "O.at: an option or an argument is a bool, an int, a float, a string or an enum without payloads"},
		{"fn four() -> int = 4\nstruct O { jobs: int = four() }", "O.jobs: its default isn't written out"},
		{"struct O {\n  files: [string]\n  out: string\n}", "O.out: an array of arguments takes the rest"},
		{"struct O {\n  /// -o Out.\n  out: string\n}", "O.out: an argument has no short name"},
		{"enum O { one(a: int) }", "O.one: a command carries its options' struct, not int"},
	} {
		src := "import asuka/flag\n\n" + c.decl + "\n\nfn main() {\n  let o = flag.parse<O>([])\n  print(\"{o}\")\n}\n"
		res := Files(map[string][]byte{"main.asuka": []byte(src)}, nil)
		if err := res.Err(); err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s:\n got %v\nwant %q", c.decl, err, c.want)
		}
	}
}
