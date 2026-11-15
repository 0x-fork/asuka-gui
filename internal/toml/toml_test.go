package toml

import (
	"encoding/json"
	"errors"
	"math"
	"reflect"
	"strings"
	"testing"
)

type (
	m = map[string]any
	a = []any
)

// dt is a date or a time, as the tests expect it.
type dt struct {
	kind kind
	s    string
}

// plain is a tree as Go values, ints and floats apart, dates and times
// with their kinds.
func plain(v *value) any {
	switch v.kind {
	case kString:
		return v.str
	case kInteger:
		return v.i
	case kFloat:
		return v.f
	case kBool:
		return v.b
	case kArray:
		out := a{}
		for _, e := range v.arr {
			out = append(out, plain(e))
		}
		return out
	case kTable:
		return plainTable(v.tbl)
	}
	return dt{v.kind, v.str}
}

func plainTable(t *table) m {
	out := m{}
	for _, k := range t.keys {
		out[k] = plain(t.m[k])
	}
	return out
}

func TestValid(t *testing.T) {
	for _, c := range []struct {
		name, doc string
		want      m
	}{
		{"empty", "", m{}},
		{"comments", "# a\n  # b\r\n\n", m{}},
		{"bom", "\ufeffa = 1", m{"a": int64(1)}},
		{"bare keys", "key = 1\nbare_key = 2\nbare-key = 3\n1234 = 4\ntrue = 5", m{"key": int64(1), "bare_key": int64(2), "bare-key": int64(3), "1234": int64(4), "true": int64(5)}},
		{"quoted keys", `"127.0.0.1" = 1` + "\n" + `"ʎǝʞ" = 2` + "\n'quoted \"value\"' = 3\n\"\" = 4", m{"127.0.0.1": int64(1), "ʎǝʞ": int64(2), `quoted "value"`: int64(3), "": int64(4)}},
		{"dotted keys", "name = \"Orange\"\nphysical.color = \"orange\"\nphysical . shape = \"round\"\nsite.\"google.com\" = true", m{
			"name": "Orange", "physical": m{"color": "orange", "shape": "round"}, "site": m{"google.com": true},
		}},
		{"dotted keys, out of order", "apple.type = \"fruit\"\norange.type = \"fruit\"\napple.skin = \"thin\"", m{
			"apple": m{"type": "fruit", "skin": "thin"}, "orange": m{"type": "fruit"},
		}},
		{"float-like keys", "3.14159 = \"pi\"", m{"3": m{"14159": "pi"}}},
		{"spaces", "\t a\t=\t1 \t# c", m{"a": int64(1)}},
		{"basic strings", `s = "I'm a string. \"You can quote me\". Name\tJos\u00E9\nLocation\tSF. \U0001F600 \b\f\r\\"`, m{
			"s": "I'm a string. \"You can quote me\". Name\tJosé\nLocation\tSF. 😀 \b\f\r\\",
		}},
		{"literal strings", `a = 'C:\Users\nodejs\templates'` + "\n" + `b = '<\i\c*\s*>'` + "\nc = 'Tom \"Dubs\" Preston'", m{
			"a": `C:\Users\nodejs\templates`, "b": `<\i\c*\s*>`, "c": `Tom "Dubs" Preston`,
		}},
		{"multi-line basic", "s = \"\"\"\nRoses are red\r\nViolets are blue\"\"\"", m{"s": "Roses are red\nViolets are blue"}},
		{"line-ending backslash", "s = \"\"\"\nThe quick brown \\\n\n\n  fox jumps over \\   \n    the lazy dog.\"\"\"\nt = \"\"\"\\\n       \"\"\"", m{
			"s": "The quick brown fox jumps over the lazy dog.", "t": "",
		}},
		{"quotes in multi-line", `a = """Here are two quotation marks: "". Simple enough."""` + "\n" + `b = """Here are three quotation marks: ""\"."""` + "\n" + `c = """"This," she said, "is just a pointless statement.""""`, m{
			"a": `Here are two quotation marks: "". Simple enough.`,
			"b": `Here are three quotation marks: """.`,
			"c": `"This," she said, "is just a pointless statement."`,
		}},
		{"multi-line literal", "a = '''\nThe first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n'''\nb = '''I [dw]on't need \\d{2} apples'''\nc = ''''That,' she said, 'is still pointless.''''", m{
			"a": "The first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n",
			"b": `I [dw]on't need \d{2} apples`,
			"c": `'That,' she said, 'is still pointless.'`,
		}},
		{"tabs in strings", "a = \"\t\"\nb = '\t'", m{"a": "\t", "b": "\t"}},
		{"integers", "a = +99\nb = 42\nc = 0\nd = -17\ne = 1_000\nf = 5_349_221\ng = 1_2_3_4_5\nh = -0\ni = +0\nj = 9223372036854775807\nk = -9223372036854775808", m{
			"a": int64(99), "b": int64(42), "c": int64(0), "d": int64(-17), "e": int64(1000), "f": int64(5349221), "g": int64(12345),
			"h": int64(0), "i": int64(0), "j": int64(math.MaxInt64), "k": int64(math.MinInt64),
		}},
		{"hex, octal, binary", "a = 0xDEADBEEF\nb = 0xdeadbeef\nc = 0xdead_beef\nd = 0o01234567\ne = 0o755\nf = 0b11010110\ng = 0x7FFFFFFFFFFFFFFF\nh = 0x00", m{
			"a": int64(0xDEADBEEF), "b": int64(0xDEADBEEF), "c": int64(0xDEADBEEF), "d": int64(0o1234567), "e": int64(0o755), "f": int64(0b11010110),
			"g": int64(math.MaxInt64), "h": int64(0),
		}},
		{"floats", "a = +1.0\nb = 3.1415\nc = -0.01\nd = 5e+22\ne = 1e06\nf = -2E-2\ng = 6.626e-34\nh = 224_617.445_991_228\ni = -0.0\nj = 0e0\nk = 1e1_0", m{
			"a": 1.0, "b": 3.1415, "c": -0.01, "d": 5e22, "e": 1e6, "f": -2e-2, "g": 6.626e-34, "h": 224617.445991228, "i": math.Copysign(0, -1), "j": 0.0, "k": 1e10,
		}},
		{"infinities", "a = inf\nb = +inf\nc = -inf", m{"a": math.Inf(1), "b": math.Inf(1), "c": math.Inf(-1)}},
		{"booleans", "a = true\nb = false", m{"a": true, "b": false}},
		{"date-times", "a = 1979-05-27T07:32:00Z\nb = 1979-05-27T00:32:00-07:00\nc = 1979-05-27T00:32:00.999999-07:00\nd = 1979-05-27 07:32:00Z\ne = 1979-05-27t07:32:00z\nf = 1979-05-27T07:32:00\ng = 1979-05-27 00:32:00.999\nh = 1979-05-27\ni = 07:32:00\nj = 00:32:00.999999\nk = 2000-02-29\nl = 1990-12-31T23:59:60Z", m{
			"a": dt{kDatetime, "1979-05-27T07:32:00Z"}, "b": dt{kDatetime, "1979-05-27T00:32:00-07:00"}, "c": dt{kDatetime, "1979-05-27T00:32:00.999999-07:00"},
			"d": dt{kDatetime, "1979-05-27T07:32:00Z"}, "e": dt{kDatetime, "1979-05-27T07:32:00Z"},
			"f": dt{kDatetimeLocal, "1979-05-27T07:32:00"}, "g": dt{kDatetimeLocal, "1979-05-27T00:32:00.999"},
			"h": dt{kDateLocal, "1979-05-27"}, "i": dt{kTimeLocal, "07:32:00"}, "j": dt{kTimeLocal, "00:32:00.999999"},
			"k": dt{kDateLocal, "2000-02-29"}, "l": dt{kDatetime, "1990-12-31T23:59:60Z"},
		}},
		{"date then a comment", "a = 1979-05-27 # day", m{"a": dt{kDateLocal, "1979-05-27"}}},
		{"arrays", "integers = [ 1, 2, 3 ]\ncolors = [ \"red\", \"yellow\", \"green\" ]\nnested_arrays_of_ints = [ [ 1, 2 ], [3, 4, 5] ]\nnested_mixed_array = [ [ 1, 2 ], [\"a\", \"b\", \"c\"] ]\nstring_array = [ \"all\", 'strings', \"\"\"are the same\"\"\", '''type''' ]\nnumbers = [ 0.1, 0.2, 0.5, 1, 2, 5 ]\ncontributors = [\n  \"Foo Bar <foo@example.com>\",\n  { name = \"Baz Qux\", email = \"bazqux@example.com\", url = \"https://example.com/bazqux\" }\n]\nempty = []", m{
			"integers": a{int64(1), int64(2), int64(3)}, "colors": a{"red", "yellow", "green"},
			"nested_arrays_of_ints": a{a{int64(1), int64(2)}, a{int64(3), int64(4), int64(5)}},
			"nested_mixed_array":    a{a{int64(1), int64(2)}, a{"a", "b", "c"}},
			"string_array":          a{"all", "strings", "are the same", "type"},
			"numbers":               a{0.1, 0.2, 0.5, int64(1), int64(2), int64(5)},
			"contributors":          a{"Foo Bar <foo@example.com>", m{"name": "Baz Qux", "email": "bazqux@example.com", "url": "https://example.com/bazqux"}},
			"empty":                 a{},
		}},
		{"multi-line arrays", "a = [\n  1, # one\n  2,\n\n  # nothing\n]\nb = [\n1\n,2]", m{"a": a{int64(1), int64(2)}, "b": a{int64(1), int64(2)}}},
		{"tables", "[table-1]\nkey1 = \"some string\"\nkey2 = 123\n\n[table-2]\nkey1 = \"another string\"\nkey2 = 456", m{
			"table-1": m{"key1": "some string", "key2": int64(123)}, "table-2": m{"key1": "another string", "key2": int64(456)},
		}},
		{"table names", "[dog.\"tater.man\"]\ntype.name = \"pug\"\n[ j . \"ʞ\" . 'l' ]\n[ g .  h  . i ]", m{
			"dog": m{"tater.man": m{"type": m{"name": "pug"}}}, "j": m{"ʞ": m{"l": m{}}}, "g": m{"h": m{"i": m{}}},
		}},
		{"super-table after", "[x.y.z.w]\n[x]", m{"x": m{"y": m{"z": m{"w": m{}}}}}},
		{"tables out of order", "[fruit.apple]\n[animal]\n[fruit.orange]", m{"fruit": m{"apple": m{}, "orange": m{}}, "animal": m{}}},
		{"sub-table under dotted keys", "[fruit]\napple.color = \"red\"\napple.taste.sweet = true\n\n[fruit.apple.texture]\nsmooth = true", m{
			"fruit": m{"apple": m{"color": "red", "taste": m{"sweet": true}, "texture": m{"smooth": true}}},
		}},
		{"inline tables", "name = { first = \"Tom\", last = \"Preston-Werner\" }\npoint = { x = 1, y = 2 }\nanimal = { type.name = \"pug\" }\nempty = {}\nnested = {a = {b = {}}, c = [1, {d = 2}]}", m{
			"name": m{"first": "Tom", "last": "Preston-Werner"}, "point": m{"x": int64(1), "y": int64(2)},
			"animal": m{"type": m{"name": "pug"}}, "empty": m{}, "nested": m{"a": m{"b": m{}}, "c": a{int64(1), m{"d": int64(2)}}},
		}},
		{"arrays of tables", "[[products]]\nname = \"Hammer\"\nsku = 738594937\n\n[[products]]  # empty\n\n[[products]]\nname = \"Nail\"\nsku = 284758393\ncolor = \"gray\"", m{
			"products": a{m{"name": "Hammer", "sku": int64(738594937)}, m{}, m{"name": "Nail", "sku": int64(284758393), "color": "gray"}},
		}},
		{"nested arrays of tables", "[[fruits]]\nname = \"apple\"\n\n[fruits.physical]\ncolor = \"red\"\nshape = \"round\"\n\n[[fruits.varieties]]\nname = \"red delicious\"\n\n[[fruits.varieties]]\nname = \"granny smith\"\n\n[[fruits]]\nname = \"banana\"\n\n[[fruits.varieties]]\nname = \"plantain\"", m{
			"fruits": a{
				m{"name": "apple", "physical": m{"color": "red", "shape": "round"}, "varieties": a{m{"name": "red delicious"}, m{"name": "granny smith"}}},
				m{"name": "banana", "varieties": a{m{"name": "plantain"}}},
			},
		}},
		{"array of inline tables", "points = [ { x = 1, y = 2, z = 3 },\n           { x = 7, y = 8, z = 9 } ]", m{
			"points": a{m{"x": int64(1), "y": int64(2), "z": int64(3)}, m{"x": int64(7), "y": int64(8), "z": int64(9)}},
		}},
		{"crlf", "a = 1\r\n[b]\r\nc = \"\"\"x\r\ny\"\"\"\r\n", m{"a": int64(1), "b": m{"c": "x\ny"}}},
		{"no newline at the end", "[a]\nb = 1 # c", m{"a": m{"b": int64(1)}}},
		{"unicode", "\"€\" = 'ü' # ☃", m{"€": "ü"}},
	} {
		t.Run(c.name, func(t *testing.T) {
			root, err := parse([]byte(c.doc))
			if err != nil {
				t.Fatalf("%v\n%s", err, c.doc)
			}
			if got := plainTable(root); !reflect.DeepEqual(got, c.want) {
				t.Errorf("got  %#v\nwant %#v", got, c.want)
			}
		})
	}
}

func TestNaN(t *testing.T) {
	root, err := parse([]byte("a = nan\nb = +nan\nc = -nan"))
	if err != nil {
		t.Fatal(err)
	}
	for _, k := range []string{"a", "b", "c"} {
		if v := root.m[k]; v.kind != kFloat || !math.IsNaN(v.f) {
			t.Errorf("%s: %+v", k, v)
		}
	}
}

func TestInvalid(t *testing.T) {
	for _, c := range []struct {
		name, doc string
		at        string // line:col
		msg       string // part of the message
	}{
		// Keys.
		{"no key", "= 1", "1:1", "expected a key"},
		{"no value", "a =", "1:4", "expected a value"},
		{"no value, newline", "a =\nb = 1", "1:4", "expected a value"},
		{"no equals", "a 1", "1:3", "expected = after a key"},
		{"key with space", "a b = 1", "1:3", "expected = after a key"},
		{"duplicate key", "a = 1\na = 2", "2:1", "a is defined twice"},
		{"duplicate quoted key", "spelling = 1\n\"spelling\" = 2", "2:1", "defined twice"},
		{"key on a key's line", "a = 1 b = 2", "1:7", "end of the line"},
		{"multi-line key", "\"\"\"a\"\"\" = 1", "1:1", "multi-line string can't be a key"},
		{"empty dotted part", "a. = 1", "1:4", "expected a key"},
		{"leading dot", ".a = 1", "1:1", "expected a key"},
		{"double dot", "a..b = 1", "1:3", "expected a key"},
		{"dotted over a value", "a = 1\na.b = 2", "2:1", "a is an integer, not a table"},
		{"dotted into an inline table", "a = {b = 1}\na.c = 2", "2:1", "inline table"},
		{"dotted into a header's table", "[a.b]\nx = 1\n[a]\nb.y = 2", "4:1", "has a [header]"},
		{"dotted into a header's parent", "[a.b.c]\nz = 9\n[a]\nb.c.t = 1", "4:1", "has a [header]"},
		{"dotted into an array of tables", "[[a.b]]\n[a]\nb.y = 2", "3:1", "an array, not a table"},
		{"dotted, then the same key", "a.b = 1\na.b.c = 2", "2:3", "a.b is an integer"},
		{"dotted redefinition", "a.b.c = 1\na.b = 2", "2:3", "a.b is defined twice"},

		// Tables.
		{"table twice", "[a]\nb = 1\n[a]\nc = 2", "3:2", "table a is defined twice"},
		{"table twice, implicit first", "[a.b]\n[a]\n[a]", "3:2", "defined twice"},
		{"table over a value", "a = 1\n[a]", "2:2", "a is an integer, not a table"},
		{"table over a value's parent", "[a]\nb = 1\n[a.b]", "3:4", "a.b is an integer"},
		{"table over dotted keys", "[fruit]\napple.color = \"red\"\n[fruit.apple]", "3:8", "made by dotted keys"},
		{"table over root dotted keys", "a.b = 1\n[a]", "2:2", "made by dotted keys"},
		{"table over an inline table", "a = {}\n[a]", "2:2", "inline table"},
		{"table into an inline table", "a = {b = {}}\n[a.b.c]", "2:2", "inline table"},
		{"table into an array", "a = [1]\n[a.b]", "2:2", "a is an array, not a table"},
		{"table over an array of tables", "[[a]]\n[a]", "2:2", "an array, not a table"},
		{"empty table name", "[]", "1:2", "expected a key"},
		{"unclosed table", "[a\nb = 1", "1:3", "expected ] after"},
		{"text after a header", "[a] b = 1", "1:5", "end of the line after a table's header"},
		{"space in [[", "[ [a]]", "1:3", "expected a key"},
		{"unclosed ]]", "[[a]\nb = 1", "1:5", "expected ]] after"},
		{"space in ]]", "[[a] ]", "1:5", "expected ]] after"},
		{"array of tables over a table", "[a]\n[[a]]", "2:3", "a is a table, not an array of tables"},
		{"array of tables over a static array", "a = []\n[[a]]", "2:3", "static array"},
		{"array of tables over inline tables", "a = [{b = 1}]\n[[a]]", "2:3", "static array"},
		{"array of tables' sub-table twice", "[[a]]\n[a.b]\n[a.b]", "3:4", "defined twice"},

		// Strings.
		{"unclosed string", `a = "abc`, "1:9", "not closed"},
		{"unclosed literal", "a = 'abc", "1:9", "not closed"},
		{"newline in a string", "a = \"a\nb\"", "1:7", "newline in a string"},
		{"newline in a literal", "a = 'a\nb'", "1:7", "newline in a string"},
		{"bad escape", `a = "\x41"`, "1:6", `no escape \x`},
		{"bad escape \\e", `a = "\e"`, "1:6", `no escape \e`},
		{"escaped space", `a = "\ "`, "1:6", `no escape \ `},
		{"short \\u", `a = "\u12"`, "1:6", `\u needs 4 hexadecimal digits`},
		{"short \\U", `a = "\U0001F60"`, "1:6", `\U needs 8`},
		{"surrogate", `a = "\uD800"`, "1:6", "Unicode scalar value"},
		{"beyond Unicode", `a = "\U00110000"`, "1:6", "Unicode scalar value"},
		{"control character", "a = \"\x01\"", "1:6", "control character"},
		{"DEL", "a = \"\x7f\"", "1:6", "control character"},
		{"control in a literal", "a = '\x00'", "1:6", "control character"},
		{"bare CR in a multi-line string", "a = \"\"\"a\rb\"\"\"", "1:9", "carriage return"},
		{"too many quotes", `a = """a""""""`, "1:9", "too many quotes"},
		{"unclosed multi-line", "a = \"\"\"\nabc", "2:4", "not closed"},
		{"unclosed multi-line literal", "a = '''abc", "1:11", "not closed"},
		{"backslash, space, text", "a = \"\"\"a\\  b\"\"\"", "1:9", `no escape`},
		{"not UTF-8", "a = \"\xff\"", "1:6", "not UTF-8"},
		{"bare word", "a = abc", "1:5", "expected a value"},
		{"bare true-ish", "a = truth", "1:5", `"truth"`},
		{"upper-case TRUE", "a = TRUE", "1:5", "expected a value"},

		// Numbers.
		{"leading zero", "a = 01", "1:5", "leading zero"},
		{"leading zero, signed", "a = -01", "1:5", "leading zero"},
		{"leading zero, float", "a = 03.14", "1:5", "leading zero"},
		{"leading zero, underscore", "a = 0_1", "1:5", "leading zero"},
		{"double underscore", "a = 1__000", "1:5", "isn't a value"},
		{"trailing underscore", "a = 1000_", "1:5", "isn't a value"},
		{"leading underscore", "a = _1000", "1:5", "expected a value"},
		{"signed hex", "a = +0x1", "1:5", "no sign"},
		{"upper-case 0X", "a = 0X1", "1:5", "isn't a value"},
		{"bad hex digit", "a = 0xG", "1:5", "isn't a value"},
		{"bad octal digit", "a = 0o8", "1:5", "isn't a value"},
		{"bad binary digit", "a = 0b2", "1:5", "isn't a value"},
		{"hex too big", "a = 0x8000000000000000", "1:5", "out of a 64-bit integer's range"},
		{"integer too big", "a = 9223372036854775808", "1:5", "out of a 64-bit integer's range"},
		{"float without fraction", "a = 1.", "1:5", "isn't a value"},
		{"float without integer", "a = .5", "1:5", "expected a value"},
		{"float, dot before e", "a = 1.e5", "1:5", "isn't a value"},
		{"float, empty exponent", "a = 1e", "1:5", "isn't a value"},
		{"float, underscore before dot", "a = 1_.5", "1:5", "isn't a value"},
		{"float, underscore after dot", "a = 1._5", "1:5", "isn't a value"},
		{"float, underscore in exponent sign", "a = 1e_5", "1:5", "isn't a value"},
		{"float too big", "a = 1e400", "1:5", "float's range"},
		{"Inf", "a = Inf", "1:5", "expected a value"},
		{"infinity", "a = infinity", "1:5", "isn't a value"},
		{"double sign", "a = +-1", "1:5", "isn't a value"},

		// Dates and times.
		{"no seconds", "a = 07:32", "1:5", "with seconds"},
		{"month 13", "a = 1979-13-01", "1:5", "no such month"},
		{"February 30", "a = 2000-02-30", "1:5", "no such day"},
		{"February 29, not a leap year", "a = 1900-02-29", "1:5", "no such day"},
		{"hour 24", "a = 24:00:00", "1:5", "no such time"},
		{"minute 60", "a = 1979-05-27T07:60:00", "1:5", "no such time"},
		{"offset hour", "a = 1979-05-27T07:32:00+24:00", "1:5", "no such offset"},
		{"offset without colon", "a = 1979-05-27T07:32:00+0700", "1:5", "offset"},
		{"time with offset", "a = 07:32:00Z", "1:5", "has no offset"},
		{"empty fraction", "a = 07:32:00.", "1:5", "needs digits"},
		{"bad separator", "a = 1979-05-27X07:32:00", "1:5", "expected T"},
		{"short year", "a = 979-05-27", "1:5", "isn't a value"},

		// Arrays.
		{"unclosed array", "a = [1, 2", "1:10", "not closed"},
		{"missing comma", "a = [1 2]", "1:8", "expected , or ]"},
		{"leading comma", "a = [,1]", "1:6", "expected a value"},
		{"double comma", "a = [1,,2]", "1:8", "expected a value"},
		{"array then text", "a = [1] 2", "1:9", "end of the line"},

		// Inline tables.
		{"newline in an inline table", "a = {b = 1,\nc = 2}", "1:12", "newline in an inline table"},
		{"newline before }", "a = {b = 1\n}", "1:11", "newline in an inline table"},
		{"trailing comma", "a = {b = 1,}", "1:12", "comma after an inline table's last value"},
		{"empty with comma", "a = {,}", "1:6", "expected a key"},
		{"missing comma in {}", "a = {b = 1 c = 2}", "1:12", "expected , or }"},
		{"duplicate in an inline table", "a = {b = 1, b = 2}", "1:13", "b is defined twice"},
		{"dotted redefinition in an inline table", "a = {b.c = 1, b = 2}", "1:15", "b is defined twice"},
		{"unclosed inline table", "a = {b = 1", "1:11", "expected , or }"},

		// Comments and lines.
		{"control character in a comment", "# a\x01", "1:4", "control character"},
		{"bare CR", "a = 1\rb = 2", "1:6", "carriage return"},
		{"CR in a comment", "# a\rb", "1:4", "control character"},
		{"column counts characters", "\"ü€\" = é", "1:8", "expected a value"},
	} {
		t.Run(c.name, func(t *testing.T) {
			_, err := parse([]byte(c.doc))
			var e *Error
			if !errors.As(err, &e) {
				t.Fatalf("got %v, want an error at %s", err, c.at)
			}
			if !strings.HasPrefix(e.Error(), c.at+": ") || !strings.Contains(e.Msg, c.msg) {
				t.Errorf("got %q, want %s: ...%s...", e.Error(), c.at, c.msg)
			}
		})
	}
}

// config is a manifest's kind of struct: json tags, nested structs,
// slices of them, maps, a pointer, a custom UnmarshalJSON.
type config struct {
	Name         string            `json:"name"`
	Build        int               `json:"build"`
	Ratio        float64           `json:"ratio,omitempty"`
	Orientations []string          `json:"orientations"`
	Single       *bool             `json:"single,omitempty"`
	Share        toggle            `json:"share"`
	IOS          iosConfig         `json:"ios"`
	Widgets      []widget          `json:"widgets,omitempty"`
	Env          map[string]string `json:"env,omitempty"`
	Matrix       [][]int           `json:"matrix,omitempty"`
	Skipped      string            `json:"-"`
}

type iosConfig struct {
	Team  string            `json:"team,omitempty"`
	Usage map[string]string `json:"usage,omitempty"`
	Store struct {
		KeyID string `json:"keyId,omitempty"`
	} `json:"store"`
}

type widget struct {
	Kind  string   `json:"kind"`
	Sizes []string `json:"sizes,omitempty"`
}

// toggle is true, or a table of types.
type toggle struct {
	On    bool
	Types []string
}

func (t *toggle) UnmarshalJSON(b []byte) error {
	switch string(b) {
	case "true":
		t.On = true
		return nil
	case "false", "null":
		return nil
	}
	var v struct {
		Types []string `json:"types"`
	}
	if err := json.Unmarshal(b, &v); err != nil {
		return errors.New("share: want true, or a table of types")
	}
	t.On, t.Types = true, v.Types
	return nil
}

func (t toggle) MarshalJSON() ([]byte, error) {
	if t.Types == nil {
		return json.Marshal(t.On)
	}
	return json.Marshal(map[string]any{"types": t.Types})
}

func TestUnmarshal(t *testing.T) {
	doc := `# An app.
name = "Counter"
build = 7
ratio = 1.5
orientations = ["portrait"]
single = false
share.types = ["image/*"]

[ios]
team = "ABCDE12345"
usage.NSCameraUsageDescription = "To scan receipts"
store.keyId = "K"

[[widgets]]
kind = "small"
sizes = ["s", "m"]

[[widgets]]
kind = "big"

[env]
"A B" = "c"
`
	var c config
	if err := Unmarshal([]byte(doc), &c); err != nil {
		t.Fatal(err)
	}
	f := false
	want := config{
		Name: "Counter", Build: 7, Ratio: 1.5, Orientations: []string{"portrait"}, Single: &f,
		Share:   toggle{On: true, Types: []string{"image/*"}},
		Widgets: []widget{{"small", []string{"s", "m"}}, {Kind: "big"}},
		Env:     map[string]string{"A B": "c"},
	}
	want.IOS.Team = "ABCDE12345"
	want.IOS.Usage = map[string]string{"NSCameraUsageDescription": "To scan receipts"}
	want.IOS.Store.KeyID = "K"
	if !reflect.DeepEqual(c, want) {
		t.Errorf("got  %+v\nwant %+v", c, want)
	}

	var s config
	if err := Unmarshal([]byte("share = true"), &s); err != nil || !s.Share.On {
		t.Errorf("share = true: %+v, %v", s.Share, err)
	}

	// Into an any: what encoding/json makes of the same tree.
	var v any
	if err := Unmarshal([]byte("a = 1\nb = [1979-05-27, 'x']\n[c]\nd = 2.5"), &v); err != nil {
		t.Fatal(err)
	}
	if want := (m{"a": 1.0, "b": a{"1979-05-27", "x"}, "c": m{"d": 2.5}}); !reflect.DeepEqual(v, want) {
		t.Errorf("got %#v", v)
	}
}

func TestUnmarshalErrors(t *testing.T) {
	for _, c := range []struct{ doc, want string }{
		{"build = \"7\"", `1:9: build: want an integer, not a string`},
		{"name = 1", `1:8: name: want a string, not an integer`},
		{"\n[ios]\nteam = [1]", `3:8: ios.team: want a string, not an array`},
		{"[ios]\nusage = 'x'", `2:9: ios.usage: want a table, not a string`},
		{"orientations = ['a', 2]", `1:22: orientations[1]: want a string, not an integer`},
		{"[[widgets]]\nkind = 'a'\n[[widgets]]\nkind = true", `4:8: widgets[1].kind: want a string, not a boolean`},
		{"ios = 1", `1:7: ios: want a table, not an integer`},
		{"[orientations]", `1:1: orientations: want an array, not a table`},
		{"build = 1e3", `1:9: build: want an integer, not a float`},
		{"build = 1979-05-27", `1:9: build: want an integer, not a local date`},
		{"ratio = inf", `1:9: ratio: +Inf has no JSON form, so no Go value`},
		{"share = 'x'", `share: want true, or a table of types`},
		{"build = 01", `1:9: "01": a leading zero`},
	} {
		var cfg config
		err := Unmarshal([]byte(c.doc), &cfg)
		if err == nil || err.Error() != c.want {
			t.Errorf("%q: got %v, want %s", c.doc, err, c.want)
		}
	}
}

func TestUnmarshalRange(t *testing.T) {
	var v struct {
		Matrix [][]int32 `json:"matrix"`
	}
	err := Unmarshal([]byte("matrix = [[1], [99999999999999]]"), &v)
	if want := `1:17: matrix[1][0]: 99999999999999 is out of range for int32`; err == nil || err.Error() != want {
		t.Errorf("got %v, want %s", err, want)
	}
}

func TestMarshal(t *testing.T) {
	f := false
	c := config{
		Name: "Counter \"2\"", Build: 7, Ratio: 0.25, Orientations: []string{"portrait", "landscape"}, Single: &f,
		Share:   toggle{On: true, Types: []string{"image/*", "text"}},
		Widgets: []widget{{"small", []string{"s"}}, {Kind: "big"}},
		Env:     map[string]string{"b": "2", "A B": "1\n\t\x01"},
		Matrix:  [][]int{{1, 2}, {}},
		Skipped: "not written",
	}
	c.IOS.Team = "T"
	c.IOS.Usage = map[string]string{"NSCameraUsageDescription": "To scan"}
	got, err := Marshal(c)
	if err != nil {
		t.Fatal(err)
	}
	want := `name = "Counter \"2\""
build = 7
ratio = 0.25
orientations = ["portrait", "landscape"]
single = false
matrix = [[1, 2], []]

[share]
types = ["image/*", "text"]

[ios]
team = "T"

[ios.usage]
NSCameraUsageDescription = "To scan"

[ios.store]

[[widgets]]
kind = "small"
sizes = ["s"]

[[widgets]]
kind = "big"

[env]
"A B" = "1\n\t\u0001"
b = "2"
`
	if string(got) != want {
		t.Errorf("got:\n%s\nwant:\n%s", got, want)
	}
	var back config
	if err := Unmarshal(got, &back); err != nil {
		t.Fatal(err)
	}
	c.Skipped = ""
	if !reflect.DeepEqual(back, c) {
		t.Errorf("round trip:\n got %+v\nwant %+v", back, c)
	}
}

func TestMarshalShapes(t *testing.T) {
	for _, c := range []struct {
		name string
		in   string // JSON
		want string
	}{
		{"order kept", `{"z": 1, "a": 2, "m": {"y": 1, "b": 2}, "c": 3}`, "z = 1\na = 2\nc = 3\n\n[m]\ny = 1\nb = 2\n"},
		{"nulls left out", `{"a": null, "b": {"c": null}, "d": {"e": null, "f": 1}}`, "[b]\n\n[d]\nf = 1\n"},
		{"tables of tables", `{"a": {"b": {"c": {"d": 1}}}}`, "[a.b.c]\nd = 1\n"},
		{"quoted keys", `{"a.b": {"": 1, "ü": 2, "x y": 3}}`, "[\"a.b\"]\n\"\" = 1\n\"ü\" = 2\n\"x y\" = 3\n"},
		{"mixed array", `{"a": [1, {"b": 2}, [3]]}`, "a = [\n  1,\n  {b = 2},\n  [3],\n]\n"},
		{"nested arrays of objects", `{"a": [[{"b": 1}], []]}`, "a = [[{b = 1}], []]\n"},
		{"inline tables keep nesting", `{"a": [1, {"b": {"c": [1, 2]}, "d": null}]}`, "a = [\n  1,\n  {b = {c = [1, 2]}},\n]\n"},
		{"long array", `{"permissions": ["android.permission.CAMERA", "android.permission.RECORD_AUDIO", "android.permission.NFC"]}`,
			"permissions = [\n  \"android.permission.CAMERA\",\n  \"android.permission.RECORD_AUDIO\",\n  \"android.permission.NFC\",\n]\n"},
		{"arrays of tables, nested", `{"a": [{"b": [{"c": 1}, {"c": 2}], "d": {"e": 3}}]}`, "[[a]]\n\n[[a.b]]\nc = 1\n\n[[a.b]]\nc = 2\n\n[a.d]\ne = 3\n"},
		{"empty array", `{"a": []}`, "a = []\n"},
		{"floats", `{"a": 1.5, "b": 1e300, "c": -0.001}`, "a = 1.5\nb = 1e300\nc = -0.001\n"},
		{"duplicate keys: the last", `{"a": 1, "b": 2, "a": 3}`, "a = 3\nb = 2\n"},
	} {
		t.Run(c.name, func(t *testing.T) {
			got, err := Marshal(json.RawMessage(c.in))
			if err != nil {
				t.Fatal(err)
			}
			if string(got) != c.want {
				t.Errorf("got:\n%s\nwant:\n%s", got, c.want)
			}
			// What's written reads back as the JSON's tree.
			var back, orig any
			if err := Unmarshal(got, &back); err != nil {
				t.Fatalf("%v\n%s", err, got)
			}
			json.Unmarshal([]byte(c.in), &orig)
			if !reflect.DeepEqual(dropNulls(orig), back) {
				t.Errorf("round trip: %#v, want %#v", back, orig)
			}
		})
	}
}

func dropNulls(v any) any {
	switch v := v.(type) {
	case map[string]any:
		out := map[string]any{}
		for k, e := range v {
			if e != nil {
				out[k] = dropNulls(e)
			}
		}
		return out
	case []any:
		out := []any{}
		for _, e := range v {
			out = append(out, dropNulls(e))
		}
		return out
	}
	return v
}

func TestMarshalErrors(t *testing.T) {
	for _, c := range []struct{ in, want string }{
		{`[1]`, "a document is a table, not an array"},
		{`"x"`, "a document is a table, not a string"},
		{`{"a": [1, null]}`, "toml: a: [1]: TOML has no null"},
		{`{"t": {"a": 18446744073709551615}}`, "toml: t.a: 18446744073709551615 is out of a TOML integer's range"},
	} {
		_, err := Marshal(json.RawMessage(c.in))
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: got %v, want %s", c.in, err, c.want)
		}
	}
}

// TestRoundTrip writes what the valid documents read, and reads it back.
func TestRoundTrip(t *testing.T) {
	type doc struct {
		S  string            `json:"s"`
		I  int64             `json:"i"`
		F  float64           `json:"f"`
		B  bool              `json:"b"`
		A  []any             `json:"a"`
		T  map[string]any    `json:"t"`
		TS []map[string]any  `json:"ts"`
		M  map[string]string `json:"m"`
	}
	in := doc{
		S: "a \"quoted\" \\ string\nwith lines\tand \u00e9 and \U0001F600 and \x7f", I: math.MinInt64, F: 6.626e-34, B: true,
		A:  []any{"x", 1.0, []any{true, "y"}, map[string]any{"k": "v"}},
		T:  map[string]any{"nested": map[string]any{"deep": []any{1.0, 2.0}}, "x": "y"},
		TS: []map[string]any{{"a": 1.0}, {}, {"b": map[string]any{"c": "d"}}},
		M:  map[string]string{"with.dot": "1", "with space": "2", "": "3", "ключ": "4"},
	}
	out, err := Marshal(in)
	if err != nil {
		t.Fatal(err)
	}
	var back doc
	if err := Unmarshal(out, &back); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
	if !reflect.DeepEqual(in, back) {
		t.Errorf("got  %#v\nwant %#v\n%s", back, in, out)
	}
	again, err := Marshal(back)
	if err != nil || string(again) != string(out) {
		t.Errorf("not deterministic:\n%s\n%s", out, again)
	}
}

func TestValidFunc(t *testing.T) {
	if err := Valid([]byte("a = 1")); err != nil {
		t.Error(err)
	}
	if err := Valid([]byte("a = ")); err == nil || err.Error() != "1:5: expected a value, found the end of the document" {
		t.Error(err)
	}
}
