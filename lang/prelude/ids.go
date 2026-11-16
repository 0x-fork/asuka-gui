package prelude

// The numbers the UI runtime knows elements, keys, events and slots by:
// the same as gen writes into core/src/ui/elements_gen.h (AK_EL_,
// AK_KEY_, AK_EV_), computed from the same tables.

// ElementID is AK_EL_<name>, 0 when there's no such element.
func ElementID(name string) int {
	for i, e := range Elements {
		if e.Name == name {
			return i + 1
		}
	}
	return 0
}

// FindElement returns the element named name.
func FindElement(name string) *Element {
	for i := range Elements {
		if Elements[i].Name == name {
			return &Elements[i]
		}
	}
	return nil
}

// KeyID is AK_KEY_ of element el's key (or slot) name, or of the style's
// key name when el doesn't have one of its own; 0 when neither does.
func KeyID(el, name string) int {
	n := len(Style)
	for _, e := range Elements {
		for _, k := range e.Keys {
			n++
			if e.Name == el && k.Name == name {
				return n
			}
		}
		for _, s := range e.Slots {
			n++
			if e.Name == el && s.Name == name {
				return n
			}
		}
	}
	for i, k := range Style {
		if k.Name == name {
			return i + 1
		}
	}
	return 0
}

// EventID is AK_EV_ of element el's event name, 0 when it has none.
func EventID(el, name string) int {
	n := 0
	for _, e := range Elements {
		for _, ev := range e.Events {
			n++
			if e.Name == el && ev.Name == name {
				return n
			}
		}
	}
	return 0
}

// StateID is AK_STATE_<name>, an interaction state's bit (Variants'
// order), 0 when there's no such state.
func StateID(name string) int {
	for i, v := range Variants {
		if v == name {
			return 1 << i
		}
	}
	return 0
}
