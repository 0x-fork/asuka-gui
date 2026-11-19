package ir

import (
	"testing"

	bc "github.com/egoist/asuka/lang/bytecode"
)

// Every opcode is described to the optimizer: one that isn't (added
// since) is a barrier it doesn't optimize across, which is safe but
// leaves its code as the lowering made it. Describe a new one in
// opInfos (opt.go).
func TestEveryOpcodeDescribed(t *testing.T) {
	for i := range bc.Ops {
		if opInfos[i].unknown {
			t.Errorf("%s isn't in opt.go's opInfos", bc.Ops[i].Name)
		}
	}
}
