// Package prelude is what every Asuka program sees without importing
// it: the core types (types.asuka) and the built-in elements, declared
// here once. go generate writes, from this table:
//
//   - elements.asuka: the elements as Asuka declarations (native
//     components), which the checker reads like any source, and which
//     editors show and jump to;
//   - core/src/ui/elements_gen.h: the C UI runtime's constants (AK_EL_,
//     AK_KEY_, AK_EV_, AK_SLOT_) and tables.
//
// Adding an element: its entry here, go generate ./lang/prelude, its
// behavior in core/src/ui (and its native view in every host, when it
// makes a new kind of view: ops/schema.go).
package prelude

//go:generate go run ./gen

// Key is a prop of an element: set as name: value in its block, or
// passed in its parentheses when Positional.
type Key struct {
	Name string
	// Type is an Asuka type (types.asuka's or a built-in one).
	Type string
	// Positional keys are the element's header parameters, in order:
	// Text("Hi"). The rest are named props.
	Positional bool
	// Default is the Asuka source of the default; "" means the type's
	// zero value (or, for an optional, none).
	Default string
	// Bind marks a key a control writes back: bind text: query.
	Bind bool
	// Event is, for a Bind key, the event the control writes it with.
	Event string
	Doc   string
}

// Param is an event's parameter.
type Param struct {
	Name string
	Type string
}

// Event is what an element reports: on press { }.
type Event struct {
	Name   string
	Params []Param
	Doc    string
}

// Slot is a named place for views: toolbar { }.
type Slot struct {
	Name string
	Doc  string
}

// Element is a built-in element.
type Element struct {
	Name string
	// Children: the element takes children in its block.
	Children bool
	// LazyFor: a for directly in its block builds its rows only while
	// they're on screen (a List).
	LazyFor bool
	// NoStyle: the element takes none of the style keys (a fragment).
	NoStyle bool
	Keys    []Key
	Events  []Event
	Slots   []Slot
	Doc     string
}

// Style are the keys every element takes (unless NoStyle): layout, then
// look. They're fields of the element's style, as the Go ui's Style.
var Style = []Key{
	// Layout.
	{Name: "direction", Type: "Axis", Doc: "the axis children stack on: .column (the default) or .row"},
	{Name: "wrap", Type: "bool", Doc: "children wrap onto more lines"},
	{Name: "justify", Type: "Justify", Doc: "children's placement on the main axis"},
	{Name: "align", Type: "Align", Doc: "children's alignment on the cross axis"},
	{Name: "alignSelf", Type: "Align", Doc: "this element's alignment in its parent, over the parent's align"},
	{Name: "gap", Type: "float", Doc: "space between children, and between lines"},
	{Name: "padding", Type: "Edges", Doc: "space inside the border: 16, [8, 16] (vertical, horizontal), [t, r, b, l] or Edges(top: 4)"},
	{Name: "margin", Type: "Edges", Doc: "space outside the border"},
	{Name: "width", Type: "Length", Doc: "100, 50% or .auto (the default)"},
	{Name: "height", Type: "Length"},
	{Name: "minWidth", Type: "Length"},
	{Name: "minHeight", Type: "Length"},
	{Name: "maxWidth", Type: "Length"},
	{Name: "maxHeight", Type: "Length"},
	{Name: "aspectRatio", Type: "float", Doc: "width / height"},
	{Name: "grow", Type: "float", Doc: "its share of the space left on its parent's main axis"},
	{Name: "shrink", Type: "float", Doc: "how much it gives up when the line overflows (0: the default)"},
	{Name: "basis", Type: "Length", Doc: "its starting size on the main axis (.auto: its content's)"},
	{Name: "absolute", Type: "bool", Doc: "out of the flow, placed by top, right, bottom and left in its parent"},
	{Name: "top", Type: "Length"},
	{Name: "right", Type: "Length"},
	{Name: "bottom", Type: "Length"},
	{Name: "left", Type: "Length"},
	{Name: "start", Type: "Length", Doc: "from the side a line of text starts on (left, or right in Arabic)"},
	{Name: "end", Type: "Length"},
	{Name: "grid", Type: "Grid?", Doc: "lays children out on a grid"},
	{Name: "gridColumn", Type: "GridLine"},
	{Name: "gridRow", Type: "GridLine"},
	// Look.
	{Name: "background", Type: "Color"},
	{Name: "radius", Type: "float", Doc: "corner radius"},
	{Name: "border", Type: "float", Doc: "border width, drawn inside the box"},
	{Name: "borderColor", Type: "Color"},
	{Name: "opacity", Type: "float", Default: "1"},
	{Name: "hidden", Type: "bool"},
	{Name: "clip", Type: "bool", Doc: "clips children to its bounds"},
	{Name: "transform", Type: "Transform", Doc: "moves, scales and rotates it as it's drawn; layout doesn't see it"},
	{Name: "shadow", Type: "Shadow?"},
	{Name: "gradient", Type: "Gradient?"},
	{Name: "material", Type: "Material", Doc: "a translucent background that blurs what's behind"},
	{Name: "transition", Type: "Transition", Doc: "how it comes in and goes out during animate { }"},
	{Name: "shared", Type: "string", Doc: "a key it shares with another view: during animate { }, the one that comes moves from where the one that goes was"},
	{Name: "font", Type: "Font"},
	{Name: "color", Type: "Color", Doc: "text color, or a control's tint"},
	{Name: "textAlign", Type: "TextAlign"},
	{Name: "lines", Type: "int", Doc: "the most lines of text (0: no limit)"},
	// Accessibility.
	{Name: "label", Type: "string", Doc: "what screen readers read"},
	{Name: "hint", Type: "string"},
	{Name: "role", Type: "Role"},
	{Name: "a11yHidden", Type: "bool", Doc: "hidden from screen readers, with what's in it"},
	{Name: "tooltip", Type: "string"},
	{Name: "disabled", Type: "bool"},
}

// Elements are the built-in elements. core/src/ui implements each.
var Elements = []Element{
	{Name: "View", Children: true, Doc: "a container: a column by default"},
	{Name: "Row", Children: true, Doc: "a container whose children stack on a row"},
	{Name: "Column", Children: true, Doc: "a container whose children stack on a column"},
	{Name: "SafeArea", Children: true, Doc: "a container that keeps its children out of the system's bars, notches and the keyboard"},
	{Name: "Text", Keys: []Key{
		{Name: "text", Type: "string", Positional: true},
		{Name: "selectable", Type: "bool", Doc: "the text can be selected and copied"},
	}, Doc: "a text"},
	{Name: "Button", Keys: []Key{
		{Name: "title", Type: "string", Positional: true},
		{Name: "variant", Type: "ButtonVariant", Doc: "how the platform draws it"},
		{Name: "icon", Type: "string", Doc: "a symbol before the title"},
		{Name: "menu", Type: "[Action]", Doc: "actions shown when it's tapped, instead of press"},
		{Name: "href", Type: "string", Doc: "goes to this route when it's pressed, after press"},
	}, Events: []Event{{Name: "press"}}, Doc: "the platform's button"},
	{Name: "TextField", Keys: []Key{
		{Name: "text", Type: "string", Bind: true, Event: "change"},
		{Name: "placeholder", Type: "string"},
		{Name: "keyboard", Type: "Keyboard"},
		{Name: "secure", Type: "bool"},
		{Name: "multiline", Type: "bool"},
		{Name: "autofocus", Type: "bool"},
	}, Events: []Event{
		{Name: "change", Params: []Param{{"text", "string"}}},
		{Name: "submit"},
		{Name: "focus"},
		{Name: "blur"},
	}, Doc: "a text field"},
	{Name: "Toggle", Keys: []Key{
		{Name: "on", Type: "bool", Bind: true, Event: "change"},
		{Name: "title", Type: "string"},
	}, Events: []Event{{Name: "change", Params: []Param{{"on", "bool"}}}}, Doc: "a switch"},
}
