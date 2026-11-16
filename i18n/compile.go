package i18n

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"slices"
	"strings"
)

// Compiled is a catalog compiled for the C core (core/src/ui/i18n.c),
// which an Asuka app's tr.* lookups say messages from: its messages
// parsed into small programs, with what the core needs of CLDR's data
// for the catalog's languages (their number symbols and patterns), so
// the core has no message parser and no number tables of its own.
type Compiled struct {
	// Blob is what the core loads (i18n.catalog), in the format below.
	Blob []byte
	// Keys are the messages' keys, sorted: a key's index is what tr.key
	// says it by (i18n.say).
	Keys []string
	// Args are each key's arguments, in the order the source language's
	// message has them: the tuple i18n.say takes, an int or a float for a
	// number, plural or selectordinal, a string otherwise.
	Args [][]Arg
}

// The blob, little-endian, strings a u32 length and UTF-8:
//
//	"AKC1", string source language
//	u32 key count, per key a string
//	u32 language count, per language:
//	  string tag, u8 flags (1: right to left)
//	  the number system: 10 strings (its digits), string decimal
//	  separator, string group separator, u8 minimum grouping digits;
//	  then the decimal pattern and the percent pattern, each string
//	  prefix, suffix, negative prefix, negative suffix, u8 primary
//	  group, u8 secondary group, u8 minimum integer digits, u8 maximum
//	  fraction digits
//	  u32 message count, per message u32 key index and its nodes
//
// Nodes are u32 count, then each u8 kind and:
//
//	0 text      string
//	1 argument  u8 index (in the key's Args): its value, a number
//	            written as a number is by default
//	2 pound     (in a plural's case: its number less the offset)
//	3 number    u8 index, u8 flags (1 percent, 2 integer, 4 no grouping,
//	            8 half even, 16 its fraction digits given), u8 minimum
//	            fraction digits, u8 maximum
//	4 date      u8 index (written as the value is: a string)
//	5 plural, 6 selectordinal
//	            u8 index, i64 offset, u32 case count, per case u8 type (0
//	            an exact value: then a string, its decimal; 1 a category:
//	            then u8 Plural) and its nodes
//	7 select    u8 index, u32 case count, per case a string and its nodes
//
// A message's argument the source's hasn't is written as its name in
// braces, as Format writes a missing argument.
const (
	cText = iota
	cArg
	cPound
	cNumber
	cDate
	cPlural
	cOrdinal
	cSelect
)

// Compile compiles the catalog's messages in each of its languages.
func (c *Catalog) Compile() (*Compiled, error) {
	langs := c.langs()
	if len(langs) == 0 {
		return nil, errors.New("i18n: Compile: a catalog of no language")
	}
	source := c.message
	keys := map[string]bool{}
	for _, lang := range langs {
		l := c.set.Load().data[lang]
		if l == nil {
			continue
		}
		l.once.Do(func() { l.msgs = l.load() })
		for k := range l.msgs {
			keys[k] = true
		}
	}
	out := &Compiled{}
	for k := range keys {
		out.Keys = append(out.Keys, k)
	}
	slices.Sort(out.Keys)
	// Each key's arguments: the source language's message's, then any
	// another language's has that it hasn't (written as {name}).
	for _, k := range out.Keys {
		var args []Arg
		if m := source(c.source, k); m != nil {
			args = m.Args()
		} else {
			for _, lang := range langs {
				if m := source(lang, k); m != nil {
					args = m.Args()
					break
				}
			}
		}
		out.Args = append(out.Args, args)
	}
	var w cWriter
	w.b = append(w.b, "AKC1"...)
	w.str(c.source)
	w.u32(len(out.Keys))
	for _, k := range out.Keys {
		w.str(k)
	}
	w.u32(len(langs))
	for _, lang := range langs {
		w.str(lang)
		flags := byte(0)
		if Direction(lang) == RightToLeft {
			flags |= 1
		}
		w.b = append(w.b, flags)
		k := keyOf(lang)
		sys := k.dat().numbers(k.latn)
		for _, d := range sys.digits {
			w.str(d)
		}
		w.str(sys.decimalSep)
		w.str(sys.group)
		w.b = append(w.b, byte(sys.minGrouping))
		for _, p := range []*numberPattern{&sys.decimal, &sys.percent} {
			w.str(p.prefix)
			w.str(p.suffix)
			w.str(p.negPrefix)
			w.str(p.negSuffix)
			w.b = append(w.b, byte(p.primary), byte(p.secondary), byte(p.minInt), byte(p.maxFrac))
		}
		var msgs []int
		for i, key := range out.Keys {
			if source(lang, key) != nil {
				msgs = append(msgs, i)
			}
		}
		w.u32(len(msgs))
		for _, i := range msgs {
			w.u32(i)
			if err := w.nodes(source(lang, out.Keys[i]).nodes, out.Args[i]); err != nil {
				return nil, fmt.Errorf("i18n: Compile: %s's %s: %w", lang, out.Keys[i], err)
			}
		}
	}
	out.Blob = w.b
	return out, nil
}

type cWriter struct{ b []byte }

func (w *cWriter) u32(n int) { w.b = binary.LittleEndian.AppendUint32(w.b, uint32(n)) }

func (w *cWriter) str(s string) {
	w.u32(len(s))
	w.b = append(w.b, s...)
}

func (w *cWriter) nodes(nodes []node, args []Arg) error {
	w.u32(len(nodes))
	for i := range nodes {
		if err := w.node(&nodes[i], args); err != nil {
			return err
		}
	}
	return nil
}

func (w *cWriter) node(n *node, args []Arg) error {
	if n.kind == nodeText {
		w.b = append(w.b, cText)
		w.str(n.text)
		return nil
	}
	if n.kind == nodePound {
		w.b = append(w.b, cPound)
		return nil
	}
	index := slices.IndexFunc(args, func(a Arg) bool { return a.Name == n.text })
	if index < 0 {
		// Not one of the source's arguments: written as Format writes a
		// missing one.
		w.b = append(w.b, cText)
		w.str("{" + n.text + "}")
		return nil
	}
	if index > math.MaxUint8 {
		return errors.New("more than 255 arguments")
	}
	switch n.kind {
	case nodeArg:
		w.b = append(w.b, cArg, byte(index))
	case nodeNumber:
		flags, minFrac, maxFrac := numberStyle(n.style)
		w.b = append(w.b, cNumber, byte(index), flags, minFrac, maxFrac)
	case nodeDate, nodeTime:
		w.b = append(w.b, cDate, byte(index))
	case nodePlural, nodeSelectOrdinal:
		kind := byte(cPlural)
		if n.kind == nodeSelectOrdinal {
			kind = cOrdinal
		}
		w.b = append(w.b, kind, byte(index))
		w.b = binary.LittleEndian.AppendUint64(w.b, uint64(n.offset))
		w.u32(len(n.cases))
		for i := range n.cases {
			c := &n.cases[i]
			if exact, ok := strings.CutPrefix(c.key, "="); ok {
				w.b = append(w.b, 0)
				w.str(exact)
			} else {
				p, ok := ParsePlural(c.key)
				if !ok {
					return fmt.Errorf("a case %q", c.key)
				}
				w.b = append(w.b, 1, byte(p))
			}
			if err := w.nodes(c.nodes, args); err != nil {
				return err
			}
		}
	case nodeSelect:
		w.b = append(w.b, cSelect, byte(index))
		w.u32(len(n.cases))
		for i := range n.cases {
			w.str(n.cases[i].key)
			if err := w.nodes(n.cases[i].nodes, args); err != nil {
				return err
			}
		}
	default:
		return fmt.Errorf("a node of kind %d", n.kind)
	}
	return nil
}

// numberStyle is formatNumber's reading of a number style, as the core's
// flags: 1 percent, 2 integer, 4 no grouping, 8 half even, 16 its
// fraction digits given (then min and max).
func numberStyle(style string) (flags, minFrac, maxFrac byte) {
	stems := []string{style}
	if rest, ok := strings.CutPrefix(style, "::"); ok {
		stems = strings.Fields(rest)
	}
	for _, stem := range stems {
		switch {
		case stem == "integer" || stem == "precision-integer":
			flags |= 2 | 8
		case stem == "percent":
			flags |= 1 | 8
		case stem == "group-off":
			flags |= 4
		case strings.HasPrefix(stem, ".") && strings.Trim(stem[1:], "0#") == "" && len(stem) > 1:
			flags |= 16
			minFrac = byte(strings.Count(stem, "0"))
			maxFrac = byte(len(stem) - 1)
		}
	}
	return flags, minFrac, maxFrac
}
