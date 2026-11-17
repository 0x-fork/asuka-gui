package diag

import "testing"

// Every translation is of a code the table has.
func TestTranslationsAreCodes(t *testing.T) {
	for lang, texts := range translations {
		for name := range texts {
			if Lookup(name) == nil {
				t.Errorf("%s: %s isn't a code", lang, name)
			}
		}
	}
}
