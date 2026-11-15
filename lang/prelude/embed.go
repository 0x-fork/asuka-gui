package prelude

import _ "embed"

// The prelude's Asuka sources, which the checker reads before any
// package: the core types, the schema's enums and the elements.
var (
	//go:embed types.asuka
	typesSrc []byte
	//go:embed enums.asuka
	enumsSrc []byte
	//go:embed elements.asuka
	elementsSrc []byte
)

// Source is a prelude file: its name and its text.
type Source struct {
	Name string
	Text []byte
}

// Sources are the prelude's files, in the order the checker reads them.
func Sources() []Source {
	return []Source{
		{"prelude/types.asuka", typesSrc},
		{"prelude/enums.asuka", enumsSrc},
		{"prelude/elements.asuka", elementsSrc},
	}
}
