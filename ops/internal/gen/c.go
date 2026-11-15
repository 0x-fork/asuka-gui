package main

import (
	"bytes"
	"fmt"
	"log"
	"strings"

	"github.com/egoist/asuka/ops"
)

// The C core's side of the schema (core/src/ops/ops_gen.h and
// ops_gen.c). Names are upper snake case (upperSnake, as Kotlin's):
//
//	AK_KIND_<KIND>              a Kind, from 1
//	AK_PROP_<PROP>              a common prop, 1..63
//	AK_PROP_<KIND>_<PROP>       a kind's own prop, from AK_FIRST_KIND_PROP (64)
//	AK_EVENT_<EVENT>            an EventKind, from 1
//	AK_CMD_<COMMAND>            a CommandID, from 1
//	AK_<ENUM>_<VALUE>           an enum's value, from 0
//	AK_T_<TYPE>, AK_P_<PAYLOAD> a prop's wire type, an event's payload
//
// with a count for each (AK_KIND_COUNT is one more than the last kind,
// and so on), and the tables the fake host's dumps name things with.
// The generator fails if two names come out the same.

var cTypeNames = map[ops.Type]string{
	ops.TBool: "AK_T_BOOL", ops.TInt: "AK_T_INT", ops.TFloat: "AK_T_FLOAT", ops.TString: "AK_T_STRING",
	ops.TColor: "AK_T_COLOR", ops.TEdges: "AK_T_EDGES", ops.TEnum: "AK_T_ENUM", ops.TBytes: "AK_T_BYTES",
}

var cPayloadNames = map[ops.Payload]string{
	ops.PNone: "AK_P_NONE", ops.PBool: "AK_P_BOOL", ops.PInt: "AK_P_INT", ops.PFloat: "AK_P_FLOAT",
	ops.PString: "AK_P_STRING", ops.PPoint: "AK_P_POINT", ops.PEnvironment: "AK_P_ENVIRONMENT",
	ops.PGesture: "AK_P_GESTURE", ops.PBytes: "AK_P_BYTES",
}

// cNames checks that every C name is used once.
type cNames map[string]string

func (n cNames) add(name, what string) string {
	if prev, ok := n[name]; ok {
		log.Fatalf("gen: C name %s is both %s and %s", name, prev, what)
	}
	n[name] = what
	return name
}

func cComment(p func(string, ...any), indent, doc string) {
	if doc == "" {
		return
	}
	for _, line := range strings.Split(doc, "\n") {
		p("%s// %s\n", indent, strings.TrimRight(line, " "))
	}
}

func enumIndex(s ops.SchemaDef, name string) int {
	for i, e := range s.Enums {
		if e.Name == name {
			return i
		}
	}
	return -1
}

func cHeader(s ops.SchemaDef) []byte {
	var b bytes.Buffer
	p := func(format string, args ...any) { fmt.Fprintf(&b, format, args...) }
	names := cNames{}
	p("// %s\n//\n", header)
	p("// The schema's numbers for the C core (core/src/ops/ops.h includes it).\n")
	p("// Names are upper snake case: AK_KIND_<KIND> (from 1), AK_PROP_<PROP>\n")
	p("// (common props, 1..63), AK_PROP_<KIND>_<PROP> (a kind's own, from\n")
	p("// AK_FIRST_KIND_PROP), AK_EVENT_<EVENT> and AK_CMD_<COMMAND> (from 1),\n")
	p("// AK_<ENUM>_<VALUE> (from 0), AK_T_<TYPE> (a prop's wire type) and\n")
	p("// AK_P_<PAYLOAD> (an event's); each has a count, one past its last.\n")
	p("#ifndef AK_OPS_GEN_H\n#define AK_OPS_GEN_H\n\n#include <stdbool.h>\n#include <stdint.h>\n\n")

	p("typedef uint16_t ak_kind;\ntypedef uint16_t ak_prop;\ntypedef uint16_t ak_event_kind;\ntypedef uint16_t ak_cmd;\n\n")

	p("// Wire types of prop values.\nenum {\n")
	for t := ops.TBool; t <= ops.TBytes; t++ {
		p("\t%s = %d,\n", names.add(cTypeNames[t], "a type"), t)
	}
	p("\tAK_T_COUNT = %d,\n};\n\n", ops.TBytes+1)

	p("// What events carry.\nenum {\n")
	for pl := ops.PNone; pl <= ops.PBytes; pl++ {
		p("\t%s = %d,\n", names.add(cPayloadNames[pl], "a payload"), pl)
	}
	p("\tAK_P_COUNT = %d,\n};\n\n", ops.PBytes+1)

	p("// Kinds.\nenum {\n")
	for i, k := range s.Kinds {
		cComment(p, "\t", k.Doc)
		p("\t%s = %d,\n", names.add("AK_KIND_"+upperSnake(k.Name), "kind "+k.Name), i+1)
	}
	p("\tAK_KIND_COUNT = %d,\n};\n\n", len(s.Kinds)+1)

	p("// Props every kind has.\nenum {\n")
	for i, pr := range s.Common {
		cComment(p, "\t", pr.Doc)
		p("\t%s = %d,\n", names.add("AK_PROP_"+upperSnake(pr.Name), "common prop "+pr.Name), i+1)
	}
	p("\tAK_COMMON_PROP_COUNT = %d,\n", len(s.Common)+1)
	p("\tAK_FIRST_KIND_PROP = %d,\n};\n\n", ops.FirstKindProp)

	for _, k := range s.Kinds {
		if len(k.Props) == 0 {
			continue
		}
		p("// %s props.\nenum {\n", k.Name)
		for i, pr := range k.Props {
			cComment(p, "\t", pr.Doc)
			p("\t%s = %d,\n", names.add("AK_PROP_"+upperSnake(k.Name)+"_"+upperSnake(pr.Name), k.Name+"'s prop "+pr.Name), ops.FirstKindProp+i)
		}
		p("};\n\n")
	}

	p("// Events.\nenum {\n")
	for i, e := range s.Events {
		cComment(p, "\t", e.Doc)
		p("\t%s = %d,\n", names.add("AK_EVENT_"+upperSnake(e.Name), "event "+e.Name), i+1)
	}
	p("\tAK_EVENT_COUNT = %d,\n};\n\n", len(s.Events)+1)

	p("// Commands.\nenum {\n")
	for i, c := range s.Commands {
		cComment(p, "\t", c.Doc)
		p("\t%s = %d,\n", names.add("AK_CMD_"+upperSnake(c.Name), "command "+c.Name), i+1)
	}
	p("\tAK_CMD_COUNT = %d,\n};\n\n", len(s.Commands)+1)

	p("// Enums, by their index in ak_enums.\nenum {\n")
	for i, e := range s.Enums {
		p("\t%s = %d,\n", names.add("AK_ENUM_"+upperSnake(e.Name), "enum "+e.Name), i)
	}
	p("\tAK_ENUM_COUNT = %d,\n};\n\n", len(s.Enums))

	for _, e := range s.Enums {
		p("// %s.\nenum {\n", e.Name)
		pre := "AK_" + upperSnake(e.Name) + "_"
		for i, v := range e.Values {
			p("\t%s = %d,\n", names.add(pre+upperSnake(v), e.Name+"'s "+v), i)
		}
		p("\t%s = %d,\n};\n\n", names.add(pre+"COUNT", e.Name+"'s count"), len(e.Values))
	}

	p(`// A prop's name and wire type; enum is its enum's index (AK_ENUM_*) for
// an AK_T_ENUM prop, else -1.
typedef struct {
	const char *name;
	uint8_t type;
	int8_t enum_id;
} ak_prop_info;

// An enum's name and its values' names, by value.
typedef struct {
	const char *name;
	const char *const *values;
	int32_t count;
} ak_enum_info;

extern const char *const ak_kind_names[AK_KIND_COUNT];
extern const char *const ak_event_names[AK_EVENT_COUNT];
extern const uint8_t ak_event_payloads[AK_EVENT_COUNT];
extern const char *const ak_cmd_names[AK_CMD_COUNT];
extern const ak_prop_info ak_common_props[AK_COMMON_PROP_COUNT];
// ak_kind_props[k] are kind k's own props, ak_kind_prop_counts[k] of them,
// from AK_FIRST_KIND_PROP.
extern const ak_prop_info *const ak_kind_props[AK_KIND_COUNT];
extern const uint8_t ak_kind_prop_counts[AK_KIND_COUNT];
extern const ak_enum_info ak_enums[AK_ENUM_COUNT];

// Names, or NULL for a number the schema doesn't have.
const char *ak_kind_name(ak_kind k);
const char *ak_event_name(ak_event_kind e);
const char *ak_cmd_name(ak_cmd c);
// ak_prop_of is prop p of kind k (a common prop for any k), or NULL.
const ak_prop_info *ak_prop_of(ak_kind k, ak_prop p);
// ak_event_payload is what events of kind e carry (AK_P_*), 255 if e is
// unknown.
uint8_t ak_event_payload(ak_event_kind e);
// ak_enum_value_name is the name of value v of enum id (AK_ENUM_*), or NULL.
const char *ak_enum_value_name(int id, int32_t v);

#endif
`)
	return b.Bytes()
}

func cSource(s ops.SchemaDef) []byte {
	var b bytes.Buffer
	p := func(format string, args ...any) { fmt.Fprintf(&b, format, args...) }
	p("// %s\n\n#include \"ops/ops_gen.h\"\n\n#include <stddef.h>\n\n", header)

	p("const char *const ak_kind_names[AK_KIND_COUNT] = {NULL")
	for _, k := range s.Kinds {
		p(", %q", k.Name)
	}
	p("};\n\n")

	p("const char *const ak_event_names[AK_EVENT_COUNT] = {NULL")
	for _, e := range s.Events {
		p(", %q", e.Name)
	}
	p("};\n\n")

	p("const uint8_t ak_event_payloads[AK_EVENT_COUNT] = {0")
	for _, e := range s.Events {
		p(", %s", cPayloadNames[e.Payload])
	}
	p("};\n\n")

	p("const char *const ak_cmd_names[AK_CMD_COUNT] = {NULL")
	for _, c := range s.Commands {
		p(", %q", c.Name)
	}
	p("};\n\n")

	info := func(pr ops.PropDef) string {
		e := -1
		if pr.Type == ops.TEnum {
			if e = enumIndex(s, pr.Enum); e < 0 {
				log.Fatalf("gen: prop %s: no enum %q", pr.Name, pr.Enum)
			}
		}
		return fmt.Sprintf("{%q, %s, %d}", pr.Name, cTypeNames[pr.Type], e)
	}
	p("const ak_prop_info ak_common_props[AK_COMMON_PROP_COUNT] = {\n\t{NULL, 0, -1},\n")
	for _, pr := range s.Common {
		p("\t%s,\n", info(pr))
	}
	p("};\n\n")

	for _, k := range s.Kinds {
		if len(k.Props) == 0 {
			continue
		}
		p("static const ak_prop_info props_%s[] = {\n", k.Name)
		for _, pr := range k.Props {
			p("\t%s,\n", info(pr))
		}
		p("};\n\n")
	}
	p("const ak_prop_info *const ak_kind_props[AK_KIND_COUNT] = {\n\tNULL,\n")
	for _, k := range s.Kinds {
		if len(k.Props) == 0 {
			p("\tNULL,\n")
		} else {
			p("\tprops_%s,\n", k.Name)
		}
	}
	p("};\n\n")
	p("const uint8_t ak_kind_prop_counts[AK_KIND_COUNT] = {0")
	for _, k := range s.Kinds {
		if len(k.Props) > 255 {
			log.Fatalf("gen: %s has more than 255 props", k.Name)
		}
		p(", %d", len(k.Props))
	}
	p("};\n\n")

	for _, e := range s.Enums {
		p("static const char *const enum_%s[] = {", e.Name)
		for i, v := range e.Values {
			if i > 0 {
				p(", ")
			}
			p("%q", v)
		}
		p("};\n")
	}
	p("\nconst ak_enum_info ak_enums[AK_ENUM_COUNT] = {\n")
	for _, e := range s.Enums {
		p("\t{%q, enum_%s, %d},\n", e.Name, e.Name, len(e.Values))
	}
	p("};\n\n")

	b.WriteString(`const char *ak_kind_name(ak_kind k) { return k < AK_KIND_COUNT ? ak_kind_names[k] : NULL; }
const char *ak_event_name(ak_event_kind e) { return e < AK_EVENT_COUNT ? ak_event_names[e] : NULL; }
const char *ak_cmd_name(ak_cmd c) { return c < AK_CMD_COUNT ? ak_cmd_names[c] : NULL; }

const ak_prop_info *ak_prop_of(ak_kind k, ak_prop p) {
	if (p > 0 && p < AK_COMMON_PROP_COUNT) return &ak_common_props[p];
	if (k < AK_KIND_COUNT && p >= AK_FIRST_KIND_PROP && p - AK_FIRST_KIND_PROP < ak_kind_prop_counts[k]) return &ak_kind_props[k][p - AK_FIRST_KIND_PROP];
	return NULL;
}

uint8_t ak_event_payload(ak_event_kind e) { return e < AK_EVENT_COUNT ? ak_event_payloads[e] : 255; }

const char *ak_enum_value_name(int id, int32_t v) {
	if (id < 0 || id >= AK_ENUM_COUNT || v < 0 || v >= ak_enums[id].count) return NULL;
	return ak_enums[id].values[v];
}
`)
	return b.Bytes()
}
