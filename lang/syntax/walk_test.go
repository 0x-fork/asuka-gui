package syntax

import (
	"os"
	"reflect"
	"testing"
)

// TestWalkComplete checks, on every .asuka file in the repository, that
// Walk visits each node's children, in source order: every Node its
// fields hold (in a slice too), and nothing else.
func TestWalkComplete(t *testing.T) {
	for _, name := range repoFiles(t) {
		src, err := os.ReadFile(name)
		if err != nil {
			t.Fatal(err)
		}
		f, _ := ParseFile(NewFileSet(), name, src)
		var stack []Node
		visited := map[Node][]Node{}
		Inspect(f, func(n Node) bool {
			if n == nil {
				stack = stack[:len(stack)-1]
				return false
			}
			if len(stack) > 0 {
				parent := stack[len(stack)-1]
				visited[parent] = append(visited[parent], n)
			}
			stack = append(stack, n)
			return true
		})
		Inspect(f, func(n Node) bool {
			if n == nil {
				return false
			}
			at := f.File.Position(n.Range().Start)
			got := map[Node]bool{}
			for i, c := range visited[n] {
				got[c] = true
				if i > 0 && c.Range().Start < visited[n][i-1].Range().Start {
					t.Errorf("%s: %T at %s: Walk visits its %T before its %T", name, n, at, visited[n][i-1], c)
				}
			}
			want := children(n)
			for _, c := range want {
				if !got[c] {
					t.Errorf("%s: %T at %s: Walk doesn't visit its %T", name, n, at, c)
				}
			}
			if len(got) != len(want) {
				t.Errorf("%s: %T at %s: Walk visits %d children, its fields hold %d", name, n, at, len(got), len(want))
			}
			return true
		})
	}
}

var nodeIface = reflect.TypeFor[Node]()

// children are the nodes n's fields hold, in field order (Trivia and
// comments aside).
func children(n Node) []Node {
	var out []Node
	var add func(v reflect.Value)
	add = func(v reflect.Value) {
		switch v.Kind() {
		case reflect.Interface, reflect.Pointer:
			if v.IsNil() {
				return
			}
			if node, ok := v.Interface().(Node); ok {
				out = append(out, node)
			}
		case reflect.Slice:
			for i := range v.Len() {
				add(v.Index(i))
			}
		}
	}
	v := reflect.ValueOf(n).Elem()
	for i := range v.NumField() {
		f := v.Type().Field(i)
		if f.Anonymous || f.Type == fileType || f.Type == commentsType {
			continue
		}
		add(v.Field(i))
	}
	return out
}
