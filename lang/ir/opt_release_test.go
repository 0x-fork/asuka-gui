package ir

import (
	"testing"

	"github.com/egoist/asuka/lang/check"
)

// Values of data alone may be let go of after their last read: a
// native object's, a closure, a task, or what holds one, at the end of
// its block (opt_release.go).
func TestPlainData(t *testing.T) {
	str := &check.TypeName{Kind: check.StructType, Fields: []*check.Field{{Name: "a", Type: check.String}, {Name: "b", Type: &check.Array{Elem: check.Int}}}}
	withFn := &check.TypeName{Kind: check.StructType, Fields: []*check.Field{{Name: "f", Type: &check.FuncType{}}}}
	native := &check.TypeName{Kind: check.StructType, Native: true}
	list := &check.TypeName{Kind: check.EnumType}
	list.Cases = []*check.Case{{Name: "cons", Params: []check.Type{check.Int, &check.Named{Obj: list}}}, {Name: "empty"}}
	for _, c := range []struct {
		t    check.Type
		want bool
	}{
		{check.String, true},
		{check.Bytes, true},
		{&check.Array{Elem: check.String}, true},
		{&check.Map{Key: check.String, Elem: &check.Optional{Elem: check.Float}}, true},
		{&check.Set{Elem: check.Int}, true},
		{&check.Tuple{Elems: []check.Type{check.Int, check.String}}, true},
		{&check.Named{Obj: str}, true},
		{&check.Named{Obj: list}, true},
		{&check.FuncType{}, false},
		{&check.Task{Elem: check.Int}, false},
		{check.View, false},
		{&check.Array{Elem: &check.Named{Obj: withFn}}, false},
		{&check.Named{Obj: native}, false},
		{&check.Map{Key: check.String, Elem: &check.FuncType{}}, false},
	} {
		if got := plainData(c.t, nil); got != c.want {
			t.Errorf("plainData(%s) = %v, want %v", c.t, got, c.want)
		}
	}
}
