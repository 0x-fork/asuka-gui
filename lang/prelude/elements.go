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
	// Builds: the handler's block is views, and the handler returns the
	// View it builds (a Boundary's error: what shows instead).
	Builds bool
	Doc    string
}

// Slot is a named place for views: toolbar { }.
type Slot struct {
	Name string
	// Lazy: the slot's views are passed as a builder (ui.slot with a
	// closure returning a View), built only when they show (a Popover's
	// content, as it opens).
	Lazy bool
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
	// LazyChildren: its children are passed as a builder (ui.lazy(v,
	// builder)), built only when they show and disposed when they go: a
	// Tab's page as it's first chosen, a Sheet's content as it opens.
	// Params are what the builder is called with (a Route's: its
	// parameters, an accessor).
	LazyChildren bool
	Params       []Param
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
		{Name: "href", Type: "string", Doc: "goes to this route when it's pressed, after press (the Router around it navigates there)"},
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
		{Name: "paging", Type: "bool", Doc: "it stops only at multiples of its own size, a page each, and hides its scroll bars"},
		{Name: "page", Type: "int", Bind: true, Event: "page", Doc: "the page it shows (paging): a swipe that settles writes it, and writing it scrolls there"},
	}, Events: []Event{
		{Name: "scroll", Params: []Param{{"x", "float"}, {"y", "float"}}, Doc: "its content offset as it scrolls"},
		{Name: "refresh", Doc: "pulled to refresh"},
		{Name: "page", Params: []Param{{"page", "int"}}, Doc: "a paging swipe is settling on a page: its index from the content's start (its leading edge, right to left)"},
	}, Doc: "scrolls its children along one axis (horizontal: a row); its wrap, justify, align, gap and padding lay out its content. Give it a size (or grow) to scroll within"},
	{Name: "List", Children: true, LazyFor: true, Keys: []Key{
		{Name: "refreshing", Type: "bool", Doc: "shows the pull to refresh spinner while true (with on refresh)"},
		{Name: "columns", Type: "int", Doc: "a grid of this many columns (gap apart)"},
		{Name: "appearance", Type: "ListAppearance"},
		{Name: "anchor", Type: "ScrollAnchor", Doc: ".bottom: a chat's, which rests at its end"},
		{Name: "swipeActions", Type: "fn(int) -> [Action]", Doc: "row index's actions, revealed by swiping it from its trailing edge (a destructive one red; the first a full swipe's)"},
		{Name: "leadingSwipeActions", Type: "fn(int) -> [Action]", Doc: "row index's actions revealed from its leading edge (Mail's Unread)"},
		{Name: "section", Type: "fn(int) -> string", Doc: "row index's section: rows next to each other with the same key are one, headed by header"},
		{Name: "header", Type: "fn(string) -> View", Doc: "a section's header, given its key (an accessor: a recycled header follows its new section)"},
		{Name: "selecting", Type: "bool", Doc: "selection mode (Mail's Edit): rows show the platform's selection control, a tap selects instead of pressing, and rows don't swipe"},
		{Name: "selected", Type: "fn(int) -> bool", Doc: "whether row index is selected, followed as what it reads changes"},
	}, Events: []Event{
		{Name: "scroll", Params: []Param{{"x", "float"}, {"y", "float"}}},
		{Name: "refresh", Doc: "pulled to refresh"},
		{Name: "endReached", Doc: "the last rows are about to show: load more (once per length)"},
		{Name: "startReached", Doc: "the first rows are about to show: load earlier ones (once per first item)"},
		{Name: "select", Params: []Param{{"index", "int"}, {"selected", "bool"}}, Doc: "the user selected or deselected row index; what selected doesn't take back goes back as it was"},
		{Name: "selectBegin", Params: []Param{{"index", "int"}}, Doc: "the user started selecting at row index while the list isn't selecting (a two-finger pan on iOS, a long press on Android)"},
		{Name: "action", Params: []Param{{"index", "int"}, {"action", "int"}}, Doc: "a swipe action of row index was chosen: its index among swipeActions, or -1 - its index among leadingSwipeActions (its run has run)"},
	}, Doc: "a for in it is the platform's recycling list: rows exist only while they're on screen. It grows into its room unless it's sized"},
	// Navigation (ui/nav.go, router.go, split.go, window.go).
	{Name: "Stack", Children: true, Doc: "a navigation stack (UINavigationController; a view stack with a top app bar and predictive back on Android): its child is its first screen; push(…) and pop() in it add and remove more, and the user's back pops. It grows into its room unless it's sized"},
	{Name: "Screen", Children: true, Keys: []Key{
		{Name: "title", Type: "string", Doc: "the bar's title (a tab's, a page's in a browser)"},
		{Name: "largeTitle", Type: "bool", Doc: "the title large at the top, collapsing into the bar as it scrolls (iOS)"},
		{Name: "backGesture", Type: "bool", Default: "true", Doc: "the edge swipe pops it (iOS)"},
		{Name: "zoomFrom", Type: "string", Doc: "the shared key of a view on the screen below: the screen zooms out of it as it's pushed, and back as it's popped (iOS 18)"},
		{Name: "barColor", Type: "Color", Doc: "the top app bar's color while it's on top (Android)"},
		{Name: "search", Type: "string", Bind: true, Event: "search", Doc: "a search field in the bar: what's typed (typing writes it)"},
		{Name: "searchPlaceholder", Type: "string", Doc: "the search field's placeholder (\"\": Search, in the app's language)"},
		{Name: "searchButton", Type: "bool", Doc: "the search field stays out of the way until it's asked for"},
		{Name: "primaryExtended", Type: "bool", Doc: "primary's title beside its icon, shrinking to the icon as the content scrolls (Android's extended FAB)"},
	}, Events: []Event{
		{Name: "search", Params: []Param{{"text", "string"}}, Doc: "the search field's text changed"},
		{Name: "searchSubmit", Doc: "return pressed in the search field"},
	}, Slots: []Slot{
		{Name: "actions", Doc: "the bar's buttons: Buttons (title or icon, menu, disabled), shown by the platform's bar"},
		{Name: "toolbar", Doc: "buttons in a bar at the bottom of the screen: Buttons, as actions'"},
		{Name: "primary", Doc: "the screen's main action (compose, add): a Button; Android's floating action button, iOS's prominent toolbar item"},
	}, Doc: "a screen of a Stack (a screen declaration's root), sized by the platform"},
	{Name: "Tabs", Children: true, Keys: []Key{
		{Name: "selected", Type: "int", Bind: true, Event: "change", Doc: "the shown tab's index: the user's choice writes it"},
	}, Events: []Event{{Name: "change", Params: []Param{{"index", "int"}}}}, Doc: "pages with a tab bar (UITabBarController; Material's navigation bar): its children are Tabs. It grows into its room unless it's sized"},
	{Name: "Tab", Children: true, LazyChildren: true, Keys: []Key{
		{Name: "title", Type: "string", Positional: true},
		{Name: "icon", Type: "string", Doc: "a symbol"},
		{Name: "badge", Type: "string", Doc: "shown on the tab: a count (\"3\"), or short text; empty: none"},
	}, Doc: "a page of Tabs: its content is built when it's first chosen"},
	{Name: "Sheet", Children: true, LazyChildren: true, Keys: []Key{
		{Name: "open", Type: "bool", Bind: true, Event: "change", Doc: "shows the sheet while true; the user dismissing it writes false"},
		{Name: "detents", Type: "[Detent]", Doc: "the heights it rests at ([]: large)"},
		{Name: "grabber", Type: "bool"},
		{Name: "fullScreen", Type: "bool", Doc: "covers the screen; the user can't swipe it away"},
	}, Events: []Event{
		{Name: "change", Params: []Param{{"open", "bool"}}},
		{Name: "dismiss", Doc: "the user dismissed it"},
	}, Doc: "content in a modal sheet while open: built as it opens, disposed as it closes. It takes no room where it's declared"},
	{Name: "Popover", Children: true, Keys: []Key{
		{Name: "open", Type: "bool", Bind: true, Event: "change", Doc: "shows the popover while true; a tap outside writes false"},
		{Name: "maxWidth", Type: "float", Doc: "how wide it may be (0: 320); it's as big as its content"},
	}, Events: []Event{
		{Name: "change", Params: []Param{{"open", "bool"}}},
		{Name: "dismiss", Doc: "the user dismissed it"},
	}, Slots: []Slot{
		{Name: "content", Lazy: true, Doc: "what the popover shows: built as it opens, disposed as it closes"},
	}, Doc: "content in a bubble pointing at its children while open (UIPopoverPresentationController, a PopupWindow, NSPopover)"},
	{Name: "Menu", Children: true, Keys: []Key{
		{Name: "actions", Type: "[Action]", Positional: true},
		{Name: "longPress", Type: "bool", Doc: "a context menu: shown on a long press (a right click on a desktop) rather than a tap"},
	}, Doc: "shows a menu of actions when its children are tapped (UIMenu, PopupMenu)"},
	{Name: "Split", Children: true, Keys: []Key{
		{Name: "title", Type: "string", Doc: "the sidebar's"},
		{Name: "selected", Type: "int", Bind: true, Event: "change", Doc: "the chosen destination's index: the user's choice writes it"},
	}, Events: []Event{{Name: "change", Params: []Param{{"index", "int"}}}}, Doc: "destinations in a sidebar and the chosen one's screens beside it, as a Stack (UISplitViewController; Material's navigation drawer): its children are Destinations"},
	{Name: "Destination", Children: true, LazyChildren: true, Keys: []Key{
		{Name: "title", Type: "string", Positional: true},
		{Name: "icon", Type: "string"},
		{Name: "badge", Type: "string"},
		{Name: "section", Type: "string", Doc: "the sidebar section it's in: destinations next to each other with the same section are one"},
	}, Doc: "a place a Split's sidebar leads to: its content is its first screen, built when it's chosen"},
	{Name: "Window", Children: true, LazyChildren: true, Keys: []Key{
		{Name: "open", Type: "bool", Bind: true, Event: "change", Doc: "the window is open while true; the user closing it writes false"},
		{Name: "title", Type: "string"},
		{Name: "options", Type: "WindowOptions", Doc: "how it opens: its size, its place, its kind"},
		{Name: "edited", Type: "bool", Doc: "its document has unsaved changes"},
		{Name: "file", Type: "string", Doc: "the path of the file it shows"},
		{Name: "watchState", Type: "bool", Doc: "follow its state (windowState(w) reads it)"},
	}, Events: []Event{
		{Name: "change", Params: []Param{{"open", "bool"}}},
		{Name: "close", Doc: "the user closed it"},
		{Name: "closeRequest", Doc: "the user asked to close it: it stays open until closeWindow(w)"},
	}, Doc: "a desktop window of its own while open (a window in the page on the web; phones have none): its content is built as it opens"},
	{Name: "Router", Children: true, Keys: []Key{
		{Name: "initial", Type: "string", Default: "\"/\"", Doc: "where it starts when nothing else says (a link the app was opened with, the address its window was left at)"},
	}, Doc: "shows its routes' screens from the address the app is at: its children are Routes"},
	{Name: "Route", Children: true, LazyChildren: true, Params: []Param{{"params", "Params"}}, Keys: []Key{
		{Name: "path", Type: "string", Positional: true, Doc: "relative to its parent's unless it starts with /: :name a parameter, *name the rest; \"\" its parent's index"},
		{Name: "layout", Type: "RouteLayout", Doc: "a navigator of its child routes, or a group"},
		{Name: "title", Type: "string"},
		{Name: "icon", Type: "string"},
		{Name: "badge", Type: "string"},
		{Name: "initial", Type: "string", Doc: "the child a navigator starts with (a stack's first screen, under the one a link names)"},
		{Name: "redirect", Type: "string", Doc: "sends whoever comes here to this href, its parameters filled in"},
		{Name: "sheet", Type: "bool", Doc: "presented in a sheet over its navigator"},
		{Name: "detents", Type: "[Detent]"},
		{Name: "guard", Type: "fn(string) -> string", Doc: "asked before its screens show: \"\" lets the user in, an href sends them there"},
	}, Doc: "maps a path to a screen (its content, built with its parameters), or holds routes"},
	{Name: "Boundary", Children: true, LazyChildren: true, Events: []Event{
		{Name: "error", Params: []Param{{"error", "Error"}, {"reset", "fn()"}}, Builds: true, Doc: "what shows instead when building its content, or an effect or handler in it, fails; reset builds it again"},
	}, Doc: "an error boundary: an error its content doesn't catch shows on error's views"},
	{Name: "PageControl", Keys: []Key{
		{Name: "count", Type: "int", Positional: true},
		{Name: "page", Type: "int", Bind: true, Event: "change"},
	}, Events: []Event{{Name: "change", Params: []Param{{"page", "int"}}}}, Doc: "a row of dots, one per page, the current one highlighted (UIPageControl)"},
}
