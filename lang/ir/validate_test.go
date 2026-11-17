package ir

import (
	"strings"
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// A read of a cell, a memo or an accessor names its result register:
// with none, a reference value would be dropped without its release.
// A call's result may be let go of.
func TestValidateDiscards(t *testing.T) {
	for _, name := range []string{"CGET", "CPEEK", "CTAKE", "MREAD", "AGET"} {
		f := &Func{Name: "f", Kinds: []bc.Kind{bc.KRef}, Code: []Instr{{Op: op(name), Args: [5]int32{bc.None, 0}}}}
		err := (&Program{Funcs: []*Func{f}}).Validate()
		if err == nil || !strings.Contains(err.Error(), "names no register") {
			t.Errorf("%s into none: %v, want an error", name, err)
		}
	}
	f := &Func{Name: "f", Kinds: []bc.Kind{bc.KRef}, Code: []Instr{{Op: op("CALLC"), Args: [5]int32{bc.None, 0, 0, 0}}}}
	if err := (&Program{Funcs: []*Func{f}}).Validate(); err != nil && strings.Contains(err.Error(), "names no register") {
		t.Errorf("CALLC into none: %v", err)
	}
}
