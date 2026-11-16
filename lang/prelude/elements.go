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
	// Variants: the element takes the style's variants, blocks of style
	// keys laid over the rest while it's in an interaction state
	// (Variants below): pressed { opacity: 0.6 }.
	Variants bool
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
	// Keys and screen readers' actions.
	{Name: "keys", Type: "[Shortcut]", Doc: "what it does with keys while the keyboard's focus is in it (it, or a view inside it), before the focused control or the menu bar: the nearest view out from the focused one with the key takes it"},
	{Name: "actions", Type: "[A11yAction]", Doc: "what a screen reader offers to do with it (VoiceOver's rotor, TalkBack's actions menu)"},
}

// Variants are the interaction states a style variant follows, in the
// order they're laid over the style (a later one wins): a block of
// style keys named after the state, in the block of an element whose
// Variants is set. The element reports a state once a variant (or
// ui.state) asks for it.
var Variants = []string{"hovered", "pressed", "dragging", "dragOver"}

// containerKeys are what View, Row, Column and SafeArea take besides the
// style: drags and drops.
var containerKeys = []Key{
	{Name: "dragItems", Type: "[DragItem]", Doc: "makes it draggable, within the app and to other apps: a long press then a move lifts it on a touch screen, a move with the button down with a mouse. [] (the default): not draggable"},
	{Name: "dropTypes", Type: "[string]", Doc: "makes it take drops of these MIME types: exact (\"text/plain\"), a family (\"image/*\"), anything (\"*/*\"), or the app's own"},
}

// containerEvents are what View, Row, Column and SafeArea report: a
// container with any of them has a native view of its own.
var containerEvents = []Event{
	{Name: "press", Doc: "a tap on it"},
	{Name: "longPress", Doc: "a press held on it"},
	{Name: "layout", Params: []Param{{"frame", "Frame"}}, Doc: "its frame once it's laid out, and again each time layout changes it"},
	{Name: "pressedChange", Params: []Param{{"pressed", "bool"}}, Doc: "a finger, or the mouse's primary button, down on it (CSS's :active), and up, off it, or taken by a scroll or a gesture; it then draws its own pressed look, not the platform's"},
	{Name: "hoverChange", Params: []Param{{"hovered", "bool"}}, Doc: "a pointer over it or its children (CSS's :hover), and off it; a touch screen alone never hovers"},
	{Name: "pan", Params: []Param{{"gesture", "Gesture"}}, Doc: "a drag across it, as it goes"},
	{Name: "pinch", Params: []Param{{"gesture", "Gesture"}}},
	{Name: "rotate", Params: []Param{{"gesture", "Gesture"}}, Doc: "two fingers turning: the gesture's rotation, in radians clockwise"},
	{Name: "swipe", Params: []Param{{"direction", "SwipeDirection"}}},
	{Name: "adjust", Params: []Param{{"by", "int"}}, Doc: "makes it adjustable, as a slider is: a screen reader's swipe up or down steps it, by 1 or -1"},
	{Name: "dragPhase", Params: []Param{{"phase", "DragPhase"}}, Doc: "its drag: began as it lifts, then dropped (a target took it: for a move, remove what was dragged) or cancelled"},
	{Name: "dropOver", Params: []Param{{"over", "bool"}}, Doc: "a drag it takes coming over it, and leaving it (a drop zone's highlight)"},
	{Name: "dropMove", Params: []Param{{"x", "float"}, {"y", "float"}}, Doc: "where a drag it takes is over it, in points from its top left"},
	{Name: "drop", Params: []Param{{"drop", "Drop"}}, Doc: "what's dropped on it: the items it takes"},
}

// Elements are the built-in elements. core/src/ui implements each.
var Elements = []Element{
	{Name: "View", Children: true, Variants: true, Keys: containerKeys, Events: containerEvents, Doc: "a container: a column by default. Without a look (background, border...), events or an accessibility prop it makes no native view: its children go in the nearest one around it"},
	{Name: "Row", Children: true, Variants: true, Keys: containerKeys, Events: containerEvents, Doc: "a container whose children stack on a row"},
	{Name: "Column", Children: true, Variants: true, Keys: containerKeys, Events: containerEvents, Doc: "a container whose children stack on a column"},
	{Name: "SafeArea", Children: true, Variants: true, Keys: containerKeys, Events: containerEvents, Doc: "a column that keeps its children out of the system's bars, notches and the keyboard: padded by the safe area's insets, over its own padding"},
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
		{Name: "rich", Type: "[Span]", Bind: true, Event: "richChange", Doc: "makes it a text area of styled text: bold, italic, underline, strikethrough, monospaced, links. The user's typing and formatting write it; writing it replaces the text, and text follows its plain text. Inline components are left out. It makes the field multiline"},
		{Name: "pasteTypes", Type: "[string]", Doc: "the MIME types (patterns, as \"image/*\") of what on paste takes instead of the text: pasted, or put in by the keyboard (a GIF keyboard's images)"},
	}, Events: []Event{
		{Name: "change", Params: []Param{{"text", "string"}}},
		{Name: "submit", Doc: "return pressed (not in a multiline field)"},
		{Name: "focus"},
		{Name: "blur"},
		{Name: "select", Params: []Param{{"selection", "TextRange"}}},
		{Name: "richChange", Params: []Param{{"rich", "[Span]"}}},
		{Name: "formats", Params: []Param{{"formats", "TextFormats"}}, Doc: "the styles where the cursor is, or that all the selection has, as they change: a toolbar's buttons show them"},
		{Name: "paste", Params: []Param{{"items", "[SharedItem]"}}, Doc: "what was pasted of pasteTypes: images and files are the app's own copies, in its caches"},
	}, Slots: []Slot{
		{Name: "accessory", Doc: "a bar on top of the keyboard while the field is focused (a count and Done, formatting buttons): as wide as the window, as tall as its content, built and gone with the field. Desktops don't show it"},
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
		{Name: "blurHash", Type: "string", Doc: "a BlurHash (blurha.sh) shown until source loads, decoded once per hash: at the style's aspectRatio, or square and stretched to the box"},
		{Name: "thumbHash", Type: "string", Doc: "a ThumbHash (base64) shown until source loads: it keeps its image's aspect ratio and alpha"},
		{Name: "placeholderMode", Type: "PlaceholderMode", Doc: "how the placeholder fills the box: as the image does, or its own way"},
		{Name: "animated", Type: "bool?", Doc: "plays an animated image (GIF, APNG, WebP) while true, stops it where it is while false; none: it plays unless the user asked for less motion (Reduce Motion), when it shows its first frame"},
	}, Events: []Event{
		{Name: "loaded", Params: []Param{{"width", "float"}, {"height", "float"}}},
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
	{Name: "RichText", Children: false, Keys: []Key{
		{Name: "text", Type: "[Span]", Positional: true},
	}, Events: []Event{
		{Name: "link", Params: []Param{{"url", "string"}}, Doc: "a span with a link tapped"},
	}, Doc: "styled paragraphs (spans), typeset and drawn by the platform's text engine; the style's font, color, textAlign and lines are its spans' defaults. Each paragraph's layout is cached by its content and width"},
	{Name: "Selectable", Children: true, Doc: "makes the RichTexts inside it selectable as one text, across them: a long press selects a word, the handles extend it over any of them, the edit menu copies it"},
	{Name: "Canvas", Keys: []Key{
		{Name: "draw", Type: "fn(Graphics)", Positional: true, Doc: "draws with g, in points from the canvas's top left: called once it's laid out, again when its size changes and when what it read changes; only a drawing that differs is sent"},
	}, Doc: "draws what draw draws: a chart, a gauge, a signature. It takes the size its style gives it; with none, it's empty"},
	{Name: "Shape", Keys: []Key{
		{Name: "path", Type: "fn(float, float) -> Path", Positional: true, Doc: "the outline for its size (width, height), in points"},
		{Name: "fill", Type: "Color"},
		{Name: "stroke", Type: "Color"},
		{Name: "lineWidth", Type: "float", Default: "1"},
		{Name: "lineCap", Type: "LineCap"},
		{Name: "lineJoin", Type: "LineJoin"},
		{Name: "trimStart", Type: "float", Default: "0", Doc: "the part of the outline drawn, from trimStart to trimEnd (0 to 1): a progress ring; it animates in animate { }"},
		{Name: "trimEnd", Type: "float", Default: "1"},
		{Name: "dash", Type: "[float]", Doc: "lengths drawn and skipped in turn; []: solid"},
		{Name: "dashPhase", Type: "float"},
	}, Doc: "an outline, filled, stroked or both. It takes the size its style gives it"},
	{Name: "Shader", Children: true, Keys: []Key{
		{Name: "program", Type: "bytes", Positional: true, Doc: "the compiled shader: shader(\"\"\"…\"\"\"), a Kage program the compiler compiles"},
		{Name: "uniforms", Type: "[Uniform]", Doc: "its uniform variables' values, by name; a change draws it again"},
		{Name: "animated", Type: "bool", Doc: "runs its Time: drawn again every frame the display draws"},
	}, Doc: "a box a GPU program draws, under its children, over its background"},
	{Name: "Video", Keys: []Key{
		{Name: "source", Type: "string", Positional: true, Doc: "an http(s) URL (a file, an HLS stream), or an asset"},
		{Name: "mode", Type: "ImageMode", Doc: ".fit (letterboxed) or .fill (cropped)"},
		{Name: "hideControls", Type: "bool"},
		{Name: "autoPlay", Type: "bool"},
		{Name: "loop", Type: "bool"},
		{Name: "muted", Type: "bool"},
	}, Events: []Event{
		{Name: "end", Doc: "played to its end (each time, looping)"},
		{Name: "error", Params: []Param{{"message", "string"}}},
	}, Doc: "a video in the platform's player; sized as an Image is. Drive it with play(), pause(), seek(seconds)"},
	{Name: "WebView", Keys: []Key{
		{Name: "url", Type: "string", Doc: "the page: an http(s) URL, or an asset"},
		{Name: "html", Type: "string", Doc: "markup to show instead"},
		{Name: "preload", Type: "string", Doc: "a script run before each page's own, in every frame (set when it's made)"},
		{Name: "userAgent", Type: "string"},
		{Name: "block", Type: "[string]", Doc: "requests kept from loading: a host (and its subdomains), or with a /, a part of the URL (set when it's made)"},
	}, Events: []Event{
		{Name: "loaded", Params: []Param{{"url", "string"}}, Doc: "a page loaded"},
		{Name: "error", Params: []Param{{"message", "string"}}},
		{Name: "message", Params: []Param{{"message", "string"}}, Doc: "what the page posts: window.asuka.postMessage(text)"},
		{Name: "state", Params: []Param{{"state", "WebState"}}},
		{Name: "request", Params: []Param{{"url", "string"}}, Doc: "each request the page makes (Android)"},
		{Name: "download", Params: []Param{{"download", "WebDownload"}}},
		{Name: "newWindow", Params: []Param{{"url", "string"}}, Doc: "a page it would open in a new window; without a handler it loads here"},
	}, Doc: "a web page, in the platform's web view; with no height and no grow, it grows"},
}
