package ir

import (
	"math"
	"math/bits"
)

// The sized integers' arithmetic as the VM does it (core/src/rt/rt.h's
// ak_int_* and vm/ops.h's ak_addn...), for the optimizer's constant
// folding: a type's code t (check.IntCode: log2 of its bytes, 4 more
// when unsigned), values canonical, a u64 its 64 bits.

const intU64 = 7

func intSigned(t int) bool { return t < 4 }

// intWrap is v's low bits at type t, sign- or zero-extended.
func intWrap(v int64, t int) int64 {
	switch t {
	case 0:
		return int64(int8(v))
	case 1:
		return int64(int16(v))
	case 2:
		return int64(int32(v))
	case 4:
		return int64(uint8(v))
	case 5:
		return int64(uint16(v))
	case 6:
		return int64(uint32(v))
	}
	return v
}

func intMin(t int) int64 {
	if !intSigned(t) {
		return 0
	}
	if t == 3 {
		return math.MinInt64
	}
	return -1 << ((8 << t) - 1)
}

func intMax(t int) int64 {
	switch {
	case t == intU64:
		return -1
	case t == 3:
		return math.MaxInt64
	case intSigned(t):
		return 1<<((8<<t)-1) - 1
	}
	return 1<<(8<<(t&3)) - 1
}

// intFits reports whether v, of type from, is a value of type to.
func intFits(v int64, from, to int) bool {
	if v < 0 && (from == intU64 || to == intU64) {
		return from == to
	}
	return intWrap(v, to) == v
}

// intMul is a * b at type t; false when it overflows.
func intMul(a, b int64, t int) (int64, bool) {
	if t == intU64 {
		hi, lo := bits.Mul64(uint64(a), uint64(b))
		return int64(lo), hi == 0
	}
	r := a * b
	if a != 0 && (r/a != b || a == -1 && b == math.MinInt64 || b == -1 && a == math.MinInt64) {
		return 0, false
	}
	return r, intWrap(r, t) == r
}

// sizedEval is what an instruction of the sized integers computes from
// x and y, false when it would throw (the VM throws as it runs).
func sizedEval(name string, x, y int64, t int) (int64, bool) {
	vt, ct := t&15, t>>4
	switch name {
	case "ADDN", "SUBN":
		if vt == intU64 {
			if name == "ADDN" {
				r, c := bits.Add64(uint64(x), uint64(y), 0)
				return int64(r), c == 0
			}
			r, b := bits.Sub64(uint64(x), uint64(y), 0)
			return int64(r), b == 0
		}
		var r int64
		var of bool
		if name == "ADDN" {
			r = x + y
			of = (x >= 0) == (y >= 0) && (r >= 0) != (x >= 0)
		} else {
			r = x - y
			of = (x >= 0) != (y >= 0) && (r >= 0) != (x >= 0)
		}
		return r, !of && intWrap(r, vt) == r
	case "MULN":
		return intMul(x, y, vt)
	case "DIVN", "MODN":
		if y == 0 {
			return 0, false
		}
		if !intSigned(vt) {
			if name == "DIVN" {
				return int64(uint64(x) / uint64(y)), true
			}
			return int64(uint64(x) % uint64(y)), true
		}
		if y == -1 {
			if name == "MODN" {
				return 0, true
			}
			return -x, x != intMin(vt)
		}
		if name == "DIVN" {
			return x / y, true
		}
		return x % y, true
	case "NEGN":
		return -x, x != intMin(vt)
	case "POWN", "POWI":
		if y < 0 && ct != intU64 {
			return 0, false
		}
		e := uint64(y)
		switch {
		case x == 0 || x == 1:
			if e == 0 {
				return 1, true
			}
			return x, true
		case x == -1 && vt != intU64:
			if e&1 != 0 {
				return -1, true
			}
			return 1, true
		}
		r, b := int64(1), x
		for {
			var ok bool
			if e&1 != 0 {
				if r, ok = intMul(r, b, vt); !ok {
					return 0, false
				}
			}
			e >>= 1
			if e == 0 {
				return r, true
			}
			if b, ok = intMul(b, b, vt); !ok {
				return 0, false
			}
		}
	case "SHLN", "SHRN":
		if y < 0 && ct != intU64 {
			return 0, false
		}
		n := uint64(y)
		if name == "SHLN" {
			if n >= 64 {
				return 0, true
			}
			return intWrap(int64(uint64(x)<<n), vt), true
		}
		if intSigned(vt) {
			if n >= 64 {
				if x < 0 {
					return -1, true
				}
				return 0, true
			}
			return x >> n, true
		}
		if n >= 64 {
			return 0, true
		}
		return int64(uint64(x) >> n), true
	case "LTU", "LEU":
		b := uint64(x) < uint64(y)
		if name == "LEU" {
			b = uint64(x) <= uint64(y)
		}
		if b {
			return 1, true
		}
		return 0, true
	case "WRAP":
		return intWrap(x, t), true
	case "CONVN":
		return x, intFits(x, ct, vt)
	case "CLAMPN":
		if intFits(x, ct, vt) {
			return x, true
		}
		if x < 0 && ct != intU64 {
			return intMin(vt), true
		}
		return intMax(vt), true
	}
	return 0, false
}
