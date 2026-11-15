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
	// Direction.
	{Name: "layoutDirection", Type: "LayoutDirection", Doc: "lays it and what's in it out this way, whatever the app's language: what reads one way everywhere (a phone number, a player's controls)"},
}

// containerEvents are what View, Row, Column and SafeArea report: a
// container with any of them has a native view of its own.
var containerEvents = []Event{
	{Name: "press", Doc: "a tap on it"},
	{Name: "longPress", Doc: "a press held on it"},
	{Name: "layout", Params: []Param{{"frame", "Frame"}}, Doc: "its frame once it's laid out, and again each time layout changes it"},
}

// Elements are the built-in elements. core/src/ui implements each.
var Elements = []Element{
	{Name: "View", Children: true, Events: containerEvents, Doc: "a container: a column by default. Without a look (background, border...), events or an accessibility prop it makes no native view: its children go in the nearest one around it"},
	{Name: "Row", Children: true, Events: containerEvents, Doc: "a container whose children stack on a row"},
	{Name: "Column", Children: true, Events: containerEvents, Doc: "a container whose children stack on a column"},
	{Name: "SafeArea", Children: true, Events: containerEvents, Doc: "a column that keeps its children out of the system's bars, notches and the keyboard: padded by the safe area's insets, over its own padding"},
	{Name: "Text", Keys: []Key{
		{Name: "text", Type: "string", Positional: true},
		{Name: "selectable", Type: "bool", Doc: "the text can be selected and copied"},
	}, Doc: "a text: it wraps, unless lines limits it"},
	{Name: "Button", Keys: []Key{
		{Name: "title", Type: "string", Positional: true},
		{Name: "variant", Type: "ButtonVariant", Doc: "how the platform draws it"},
		{Name: "icon", Type: "string", Doc: "a symbol before the title (see Symbol); with no title, the icon alone"},
		{Name: "menu", Type: "[Action]", Doc: "actions shown when it's tapped, instead of press"},
		{Name: "href", Type: "string", Doc: "goes to this route when it's pressed, after press"},
	}, Events: []Event{{Name: "press"}}, Doc: "the platform's button"},
	{Name: "TextField", Keys: []Key{
		{Name: "text", Type: "string", Bind: true, Event: "change", Doc: "its text: typing writes it, and writing it replaces what the field shows"},
		{Name: "placeholder", Type: "string"},
		{Name: "keyboard", Type: "Keyboard"},
		{Name: "returnKey", Type: "ReturnKey"},
		{Name: "secure", Type: "bool", Doc: "a password's: its characters hidden"},
		{Name: "autocapitalize", Type: "Autocapitalize"},
		{Name: "autocorrect", Type: "Autocorrect"},
		{Name: "contentType", Type: "string", Doc: "what it holds, for the system's AutoFill: username, password, newPassword, oneTimeCode, email, ..."},
		{Name: "multiline", Type: "bool", Doc: "lines of text: return is a new line, not submit, and it grows with its text up to lines lines"},
		{Name: "plain", Type: "bool", Doc: "without the platform's border, background and inset: its text alone"},
		{Name: "autofocus", Type: "bool", Doc: "takes the keyboard as it shows"},
		{Name: "selection", Type: "TextRange", Bind: true, Event: "select", Doc: "the cursor or the selected text, as byte offsets into text: the user's moves write it, and writing it moves them"},
	}, Events: []Event{
		{Name: "change", Params: []Param{{"text", "string"}}},
		{Name: "submit", Doc: "return pressed (not in a multiline field)"},
		{Name: "focus"},
		{Name: "blur"},
		{Name: "select", Params: []Param{{"selection", "TextRange"}}},
	}, Doc: "a text field: a line, or lines with multiline"},
	{Name: "Toggle", Keys: []Key{
		{Name: "on", Type: "bool", Bind: true, Event: "change"},
		{Name: "title", Type: "string", Doc: "what screen readers call it (the text beside it is the app's)"},
		{Name: "checkbox", Type: "bool", Doc: "a checkbox rather than a switch"},
	}, Events: []Event{{Name: "change", Params: []Param{{"on", "bool"}}}}, Doc: "a switch, or a checkbox"},
	{Name: "Slider", Keys: []Key{
		{Name: "value", Type: "float", Bind: true, Event: "change"},
		{Name: "min", Type: "float", Default: "0"},
		{Name: "max", Type: "float", Default: "1"},
	}, Events: []Event{{Name: "change", Params: []Param{{"value", "float"}}}}, Doc: "picks a value in a range"},
	{Name: "Stepper", Keys: []Key{
		{Name: "value", Type: "float", Bind: true, Event: "change"},
		{Name: "min", Type: "float", Default: "0"},
		{Name: "max", Type: "float", Default: "100"},
		{Name: "step", Type: "float", Default: "1"},
	}, Events: []Event{{Name: "change", Params: []Param{{"value", "float"}}}}, Doc: "steps a value with minus and plus buttons; show the value beside it"},
	{Name: "Segmented", Keys: []Key{
		{Name: "segments", Type: "[string]", Positional: true},
		{Name: "selected", Type: "int", Bind: true, Event: "change"},
	}, Events: []Event{{Name: "change", Params: []Param{{"index", "int"}}}}, Doc: "one of a few options side by side"},
	{Name: "Picker", Keys: []Key{
		{Name: "items", Type: "[string]", Positional: true},
		{Name: "selected", Type: "int", Default: "-1", Bind: true, Event: "change", Doc: "the chosen item's index; out of range: none, and it shows its prompt"},
		{Name: "prompt", Type: "string", Doc: "what it picks: shown while none is chosen (Android's floating label), and read by screen readers"},
	}, Events: []Event{{Name: "change", Params: []Param{{"index", "int"}}}}, Doc: "picks one of many options from a menu, showing the chosen one"},
	{Name: "Progress", Keys: []Key{
		{Name: "value", Type: "float?", Doc: "done, from 0 to 1; none: under way, how far unknown"},
	}, Doc: "a progress bar; its color is its fill"},
	{Name: "ProgressRing", Keys: []Key{
		{Name: "value", Type: "float?", Doc: "done, from 0 to 1; none: the platform's spinning indicator"},
	}, Doc: "circular progress; its color is its fill"},
	{Name: "Activity", Doc: "a spinner"},
	{Name: "Symbol", Keys: []Key{
		{Name: "name", Type: "string", Positional: true},
	}, Doc: "a symbol (an SF Symbol, the Material Symbol for it), sized as text of its font and colored with its color"},
	{Name: "Image", Keys: []Key{
		{Name: "source", Type: "string", Positional: true, Doc: "an http(s) URL, an asset's path, or a file's"},
		{Name: "mode", Type: "ImageMode", Doc: "how it fills its box"},
		{Name: "placeholder", Type: "string", Doc: "an image shown until source loads, and if it fails"},
		{Name: "transition", Type: "float", Doc: "seconds it takes to fade in as it arrives (0: the platform's; -1: none)"},
	}, Events: []Event{
		{Name: "load", Params: []Param{{"width", "float"}, {"height", "float"}}},
		{Name: "error", Params: []Param{{"message", "string"}}},
	}, Doc: "a picture the host loads; without a size it takes its natural one, no wider than its room, and keeps its aspect ratio when one side is set"},
	{Name: "Scroll", Children: true, Keys: []Key{
		{Name: "horizontal", Type: "bool"},
		{Name: "refreshing", Type: "bool", Doc: "shows the pull to refresh spinner while true (with on refresh)"},
	}, Events: []Event{
		{Name: "scroll", Params: []Param{{"x", "float"}, {"y", "float"}}, Doc: "its content offset as it scrolls"},
		{Name: "refresh", Doc: "pulled to refresh"},
	}, Doc: "scrolls its children along one axis (horizontal: a row); its wrap, justify, align, gap and padding lay out its content. Give it a size (or grow) to scroll within"},
	{Name: "List", Children: true, LazyFor: true, Keys: []Key{
		{Name: "refreshing", Type: "bool", Doc: "shows the pull to refresh spinner while true (with on refresh)"},
		{Name: "columns", Type: "int", Doc: "a grid of this many columns (gap apart)"},
		{Name: "appearance", Type: "ListAppearance"},
		{Name: "anchor", Type: "ScrollAnchor", Doc: ".bottom: a chat's, which rests at its end"},
	}, Events: []Event{
		{Name: "scroll", Params: []Param{{"x", "float"}, {"y", "float"}}},
		{Name: "refresh", Doc: "pulled to refresh"},
		{Name: "endReached", Doc: "the last rows are about to show: load more (once per length)"},
		{Name: "startReached", Doc: "the first rows are about to show: load earlier ones (once per first item)"},
	}, Doc: "a for in it is the platform's recycling list: rows exist only while they're on screen. It grows into its room unless it's sized"},
}
