package compile

import (
	"strings"
	"testing"

	"github.com/egoist/asuka/lang/target"
)

// A shape has one method table, by name: two interfaces whose provided
// methods of one name a type gets both of (it has none of its own) are
// an error as the program is compiled, and so is a provided one beside
// another interface's that the type's own answers with another
// signature.
func TestProvidedMethodConflicts(t *testing.T) {
	for _, tc := range []struct{ src, want string }{
		{`
interface A { fn hi() -> string = "a" }
interface B { fn hi() -> string = "b" }
struct S { x: int }
test "t" {
  let a: A = S(x: 1)
  let b: B = S(x: 2)
  print(a.hi() + b.hi())
}`, "main.asuka:7:14: S gets hi() from both A and B: declare S's own hi()"},
		{`
interface A { fn hi() -> string = "a" }
interface B { fn hi(loud: bool) -> string }
struct S { fn hi(loud: bool) -> string = "s" }
test "t" {
  let b: B = S()
  let a: A = S()
  print(a.hi() + b.hi(loud: true))
}`, "main.asuka:7:14: S's own hi isn't of A's hi()'s signature, and both are called through its interfaces: rename one"},
		{`
interface A { fn hi() -> string = "a" }
interface B { fn hi() -> string = "b" }
struct S { x: int; fn hi() -> string = "s" }
test "t" {
  let a: A = S(x: 1)
  let b: B = S(x: 2)
  print(a.hi() + b.hi())
}`, ""},
		// A type parameter's value converts to its interface, but not an
		// enum without payloads that doesn't declare it: it's an int.
		{`
interface Shape { fn area() -> float }
enum Light { red; fn area() -> float = 0.0 }
fn asShape<T: Shape>(x: T) -> Shape = x
test "t" { print(asShape(Light.red).area()) }`, "main.asuka:4:39: Light is stored as an int: declare the interface (enum Light: Shape) to use it as one"},
	} {
		got := strings.Join(diagsFor(map[string]string{"main.asuka": tc.src}, target.Linux), "\n")
		if got != tc.want {
			t.Errorf("got %q, want %q", got, tc.want)
		}
	}
}
