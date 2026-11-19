// Command ryugen writes the tables the core prints floats with (core/
// src/rt/ryu_gen.c): Ryu's (Ulf Adams, "Ryū: fast float-to-string
// conversion", PLDI 2018) powers of 5 and their inverses, 125 bits each,
// in the small form its d2s_small_table.h has: every 26th, and 5^0 to
// 5^25, from which str.c's pow5 and inv_pow5 compute the rest with a
// 64×128-bit multiply, and the correction each of those needs to be
// the exact table's value. And the powers of ten the core reads floats
// with (Eisel and Lemire's algorithm, as Go's strconv and fast_float
// have it): 10^q's 128 top bits, rounded down, for q from -348 to 347,
// in the same small form (every 27th, the rest from it and 5^0 to 5^26
// with their corrections):
//
//	go generate ./internal/ryugen
package main

//go:generate go run .

import (
	"bytes"
	"fmt"
	"log"
	"math/big"
	"os"
	"path/filepath"
	"runtime"
)

const (
	bitcount  = 125 // DOUBLE_POW5_BITCOUNT, DOUBLE_POW5_INV_BITCOUNT
	nPow5     = 326 // 5^0 to 5^325: e2 < 0
	nInvPow5  = 292 // 5^-0 to 5^-291: e2 >= 0
	tableSize = 26  // 5^25 is the most a uint64 holds
)

// pow5bits is ⌈log2 5^e⌉ (1 for e = 0), as Ryu computes it.
func pow5bits(e int) int { return ((e * 1217359) >> 19) + 1 }

func pow5(i int) *big.Int { return new(big.Int).Exp(big.NewInt(5), big.NewInt(int64(i)), nil) }

// split: 5^i's top 125 bits.
func split(i int) *big.Int {
	p := pow5(i)
	shift := p.BitLen() - bitcount
	if shift >= 0 {
		return p.Rsh(p, uint(shift))
	}
	return p.Lsh(p, uint(-shift))
}

// invSplit: ⌊2^(pow5bits(i) - 1 + 125) / 5^i⌋ + 1.
func invSplit(i int) *big.Int {
	p := pow5(i)
	j := p.BitLen() - 1 + bitcount
	q := new(big.Int).Lsh(big.NewInt(1), uint(j))
	q.Quo(q, p)
	return q.Add(q, big.NewInt(1))
}

var mask128 = new(big.Int).Sub(new(big.Int).Lsh(big.NewInt(1), 128), big.NewInt(1))

// approx is what the C computes from a base entry and 5^off: the
// 192-bit product shifted right by delta, its low 128 bits.
func approx(base *big.Int, off, delta int) *big.Int {
	r := new(big.Int).Mul(base, pow5(off))
	r.Rsh(r, uint(delta))
	return r.And(r, mask128)
}

// The powers of ten Eisel-Lemire's algorithm reads with (str.c's
// pow10_128): 10^q's mantissa, 128 bits from its top bit, rounded down
// (5^q's: 10^q is 5^q × 2^q).
const (
	p10Min  = -348
	p10Max  = 347
	p10Step = 27   // 5^26 is the most a uint64 holds normalized: 5^0 to 5^26
	p10Base = -351 // p10Min rounded down to a step
)

// top128 is x's top 128 bits, rounded down.
func top128(x *big.Int) *big.Int {
	if s := x.BitLen() - 128; s > 0 {
		return new(big.Int).Rsh(x, uint(s))
	}
	return new(big.Int).Lsh(x, uint(128-x.BitLen()))
}

// pow10Mantissa is 10^q's top 128 bits, rounded down.
func pow10Mantissa(q int) *big.Int {
	if q >= 0 {
		return top128(pow5(q))
	}
	d := pow5(-q)
	n := new(big.Int).Lsh(big.NewInt(1), uint(256+d.BitLen()))
	return top128(n.Quo(n, d)) // ⌊2^k / 5^-q⌋, then its top bits: still rounded down
}

// norm64 is 5^i shifted up to its top bit's being 63.
func norm64(i int) uint64 {
	p := pow5(i)
	return p.Lsh(p, uint(64-p.BitLen())).Uint64()
}

// pow10Approx is what the C computes for 10^q: its step's mantissa times
// 5^off (normalized), the 192-bit product's top 128 bits.
func pow10Approx(base *big.Int, off int) *big.Int {
	p := new(big.Int).Mul(base, new(big.Int).SetUint64(norm64(off)))
	if p.BitLen() == 192 {
		return p.Rsh(p, 64)
	}
	return p.Rsh(p, 63)
}

func pow10Tables(b *bytes.Buffer) {
	b.WriteString("\n// Eisel-Lemire's powers of ten (str.c's pow10_128): every 27th's top\n")
	b.WriteString("// 128 bits (rounded down), from 10^-351; 5^0 to 5^26 normalized; what to\n")
	b.WriteString("// add to the low word computed from them for each power from 10^-348.\n\n")
	nbase := (p10Max-p10Base)/p10Step + 1
	fmt.Fprintf(b, "const uint64_t ak_pow10_base[%d][2] = {\n", nbase)
	for k := 0; k < nbase; k++ {
		lo, hi := words(pow10Mantissa(p10Base + k*p10Step))
		fmt.Fprintf(b, "\t{%#xu, %#xu},\n", lo, hi)
	}
	b.WriteString("};\n\n")
	fmt.Fprintf(b, "const uint64_t ak_pow5_norm[%d] = {", p10Step)
	for i := 0; i < p10Step; i++ {
		if i%4 == 0 {
			b.WriteString("\n\t")
		} else {
			b.WriteString(" ")
		}
		fmt.Fprintf(b, "%#xu,", norm64(i))
	}
	b.WriteString("\n};\n\n")
	fmt.Fprintf(b, "const uint8_t ak_pow10_offsets[%d] = {", p10Max-p10Min+1)
	for q := p10Min; q <= p10Max; q++ {
		bq := p10Base + (q-p10Base)/p10Step*p10Step
		exact := pow10Mantissa(q)
		got := pow10Approx(pow10Mantissa(bq), q-bq)
		diff := new(big.Int).Sub(exact, got)
		if diff.Sign() < 0 || diff.Cmp(big.NewInt(255)) > 0 {
			log.Fatalf("10^%d: off by %v", q, diff)
		}
		glo, _ := words(got)
		if glo+diff.Uint64() < glo {
			log.Fatalf("10^%d: the correction carries", q)
		}
		if (q-p10Min)%16 == 0 {
			b.WriteString("\n\t")
		} else {
			b.WriteString(" ")
		}
		fmt.Fprintf(b, "%d,", diff.Int64())
	}
	b.WriteString("\n};\n")
}

func words(x *big.Int) (lo, hi uint64) {
	m := new(big.Int).Lsh(big.NewInt(1), 64)
	l := new(big.Int).Mod(x, m)
	h := new(big.Int).Rsh(x, 64)
	if h.BitLen() > 64 {
		log.Fatalf("%v is more than 128 bits", x)
	}
	return l.Uint64(), h.Uint64()
}

func main() {
	var b bytes.Buffer
	b.WriteString("// Code generated by internal/ryugen. DO NOT EDIT.\n\n")
	b.WriteString("// Ryu's powers of 5 (str.c's fmt_float): every 26th's top 125 bits,\n")
	b.WriteString("// and their inverses', and what to add to the low word computed from\n")
	b.WriteString("// them for each power between.\n\n")
	b.WriteString("#include <stdint.h>\n\n")

	fmt.Fprintf(&b, "const uint64_t ak_ryu_pow5_table[%d] = {", tableSize)
	for i := 0; i < tableSize; i++ {
		if i%4 == 0 {
			b.WriteString("\n\t")
		} else {
			b.WriteString(" ")
		}
		fmt.Fprintf(&b, "%#xu,", pow5(i).Uint64())
	}
	b.WriteString("\n};\n\n")

	// 5^i from 5^(26·(i/26)): exact low word minus what's computed.
	nsplit := (nPow5 + tableSize - 1) / tableSize
	fmt.Fprintf(&b, "const uint64_t ak_ryu_pow5_split[%d][2] = {\n", nsplit)
	for k := 0; k < nsplit; k++ {
		lo, hi := words(split(k * tableSize))
		fmt.Fprintf(&b, "\t{%#xu, %#xu},\n", lo, hi)
	}
	b.WriteString("};\n\n")
	fmt.Fprintf(&b, "const uint8_t ak_ryu_pow5_offsets[%d] = {", nPow5)
	for i := 0; i < nPow5; i++ {
		base := i / tableSize
		off := i - base*tableSize
		d := 0
		if off != 0 {
			exact := split(i)
			got := approx(split(base*tableSize), off, pow5bits(i)-pow5bits(base*tableSize))
			diff := new(big.Int).Sub(exact, got)
			if diff.Sign() < 0 || diff.Cmp(big.NewInt(255)) > 0 {
				log.Fatalf("5^%d: off by %v", i, diff)
			}
			// The correction is added to the low word: it mustn't carry.
			glo, _ := words(got)
			if glo+diff.Uint64() < glo {
				log.Fatalf("5^%d: the correction carries", i)
			}
			d = int(diff.Int64())
		}
		if i%16 == 0 {
			b.WriteString("\n\t")
		} else {
			b.WriteString(" ")
		}
		fmt.Fprintf(&b, "%d,", d)
	}
	b.WriteString("\n};\n\n")

	// 5^-i from 5^-(26·⌈i/26⌉) times 5^(26·⌈i/26⌉ - i).
	ninv := (nInvPow5+tableSize-1)/tableSize + 1
	fmt.Fprintf(&b, "const uint64_t ak_ryu_inv_pow5_split[%d][2] = {\n", ninv)
	for k := 0; k < ninv; k++ {
		lo, hi := words(invSplit(k * tableSize))
		fmt.Fprintf(&b, "\t{%#xu, %#xu},\n", lo, hi)
	}
	b.WriteString("};\n\n")
	fmt.Fprintf(&b, "const uint8_t ak_ryu_inv_pow5_offsets[%d] = {", nInvPow5)
	for i := 0; i < nInvPow5; i++ {
		base := (i + tableSize - 1) / tableSize
		off := base*tableSize - i
		d := 0
		if off != 0 {
			exact := invSplit(i)
			// (inv - 1) × 5^off >> delta, then + 1.
			m := new(big.Int).Sub(invSplit(base*tableSize), big.NewInt(1))
			got := approx(m, off, pow5bits(base*tableSize)-pow5bits(i))
			got.Add(got, big.NewInt(1))
			diff := new(big.Int).Sub(exact, got)
			if diff.Sign() < 0 || diff.Cmp(big.NewInt(255)) > 0 {
				log.Fatalf("5^-%d: off by %v", i, diff)
			}
			glo, _ := words(got)
			if glo+diff.Uint64() < glo {
				log.Fatalf("5^-%d: the correction carries", i)
			}
			d = int(diff.Int64())
		}
		if i%16 == 0 {
			b.WriteString("\n\t")
		} else {
			b.WriteString(" ")
		}
		fmt.Fprintf(&b, "%d,", d)
	}
	b.WriteString("\n};\n")

	pow10Tables(&b)

	_, here, _, _ := runtime.Caller(0)
	out := filepath.Join(filepath.Dir(here), "..", "..", "core", "src", "rt", "ryu_gen.c")
	if err := os.WriteFile(out, b.Bytes(), 0o644); err != nil {
		log.Fatal(err)
	}
}
