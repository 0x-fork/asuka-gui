// a11y.c: screen readers on Windows (Narrator, NVDA), through UI
// Automation.
//
// Each view of the host's own drawing answers WM_GETOBJECT with a
// provider of its own (IRawElementProviderSimple) on top of its window's
// (its host provider: position, focus): its name (Label; a text's text; a
// pressable view's texts, as VoiceOver reads a row), its help text (Hint,
// else Tooltip), its control type (Role: a button, a heading, an image, a
// link, a text, a search field; a pressable view is a button), whether a
// screen reader skips it (Decorative, with everything in it), the Invoke
// pattern for a view that presses, and a ProgressRing's value (the
// RangeValue pattern, read only, as WinUI's ring has it). Announce raises
// a notification. A view's screen reader actions are its context menu
// (Shift+F10, the menu key, a right-click), and an adjustable view takes
// the arrow keys, as a slider does.
//
// A control with no text of its own to be named by (a drop-down list
// combo box, whose text is its selection) is named through Dynamic
// Annotation (IAccPropServices), which UI Automation's proxy for the
// control reads. Tooltips are the common controls' tooltip control: one
// per app, with a tool per window that has a tooltip.
#include "nav.h"

#include <oleacc.h>
#include <uiautomation.h>

#define HEADING_LEVEL_1 80051

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_simple = {0xd6dd68d1, 0x86fd, 0x4332, {0x86, 0x66, 0x9a, 0xbe, 0xde, 0xa2, 0xd2, 0x4c}};
static const GUID iid_invoke = {0x54fcb24b, 0xe18e, 0x47a2, {0xb4, 0xd3, 0xec, 0xcb, 0xe7, 0x75, 0x99, 0xa2}};
static const GUID iid_range = {0x36dc7aef, 0x33e6, 0x4691, {0xaf, 0xe1, 0x2b, 0xe7, 0x27, 0x4b, 0x3d, 0x33}};
static const GUID clsid_acc_prop_services = {0xb5f8350b, 0x0548, 0x48b1, {0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7}};
static const GUID iid_acc_prop_services = {0x6e26e776, 0x04f0, 0x495d, {0x80, 0xe4, 0x33, 0x30, 0x35, 0x2e, 0x31, 0x69}};
static const GUID propid_acc_name = {0x608d3df8, 0x8128, 0x4aa7, {0xa4, 0x28, 0xf5, 0x5e, 0x49, 0x26, 0x72, 0x91}};

// ---- What a view says ----

// a11y_hidden reports whether n, or a view it's in, is hidden from
// screen readers.
static bool a11y_hidden(const node *n) {
	for (const node *x = n; x; x = x->parent)
		if (x->a11y_hidden) return true;
	return false;
}

// collect_texts gathers the texts in n, as its name: their labels, or
// their texts, ", " between them.
static void collect_texts(const node *n, ak_buf *out) {
	for (uint32_t i = 0; i < n->nkids; i++) {
		const node *c = n->kids[i];
		if (c->hidden || c->a11y_hidden) continue;
		const wstr *s = NULL;
		if (c->a11y_label.n) s = &c->a11y_label;
		else if (c->kind == AK_KIND_TEXT && c->text.n) s = &c->text;
		if (!s) {
			collect_texts(c, out);
			continue;
		}
		if (out->len) ak_buf_put(out, L", ", 2 * sizeof(wchar_t));
		ak_buf_put(out, s->p, (size_t)s->n * sizeof(wchar_t));
	}
}

// badge_number is a badge that's a count ("3"): its number; -1: not one.
static long badge_number(const wchar_t *s) {
	size_t n = wcslen(s);
	if (!n || n > 9) return -1;
	long v = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] < L'0' || s[i] > L'9') return -1;
		v = v * 10 + (s[i] - L'0');
	}
	return v;
}

// badge_words is a badge as a screen reader says it: a count as so many
// items ("3 items"), else the badge (a new BSTR).
static BSTR badge_words(const wchar_t *badge) {
	long count = badge_number(badge);
	if (count < 0) return SysAllocString(badge);
	char num[16], out[256];
	snprintf(num, sizeof num, "%ld", count);
	const char *vars[] = {"count", num};
	dsk_word_format(out, sizeof out, H.locale, "badgeCount", vars, 2);
	wchar_t *w = wh_wide(out, (int)strlen(out));
	BSTR b = SysAllocString(w);
	free(w);
	return b;
}

// badged is a title with its badge, if it has one, for a screen reader:
// "Inbox, 3 items" (a new BSTR).
static BSTR badged(const wchar_t *title, const wchar_t *badge) {
	if (!badge || !*badge) return SysAllocString(title);
	BSTR b = badge_words(badge);
	size_t tl = wcslen(title), bl = SysStringLen(b);
	BSTR out = SysAllocStringLen(NULL, (UINT)(tl + 2 + bl));
	if (out) {
		memcpy(out, title, tl * sizeof(wchar_t));
		memcpy(out + tl, L", ", 2 * sizeof(wchar_t));
		memcpy(out + tl + 2, b, bl * sizeof(wchar_t));
	}
	SysFreeString(b);
	return out;
}

// chosen is a tab bar's selected tab's, or a pane's chosen item's, title
// and badge.
static bool chosen(node *n, const wchar_t **title, const wchar_t **badge) {
	*title = *badge = L"";
	if (n->kind == KIND_TAB_BAR && n->nav && n->nav->tabs) {
		node *tabs = n->nav->tabs;
		if (tabs->selected < 0 || (uint32_t)tabs->selected >= tabs->nkids) return false;
		node *s = tabs->kids[tabs->selected];
		*title = wstr_c(&s->title), *badge = wstr_c(&s->badge);
		return true;
	}
	if (n->kind == KIND_SIDEBAR) return wh_pane_chosen(n, title, badge);
	return false;
}

static void set_bool(VARIANT *v, bool b) {
	v->vt = VT_BOOL;
	v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
}

static void set_i4(VARIANT *v, LONG i) {
	v->vt = VT_I4;
	v->lVal = i;
}

static void set_str(VARIANT *v, const wchar_t *s, int n) {
	v->vt = VT_BSTR;
	v->bstrVal = SysAllocStringLen(s, (UINT)n);
}

// choice_property is a tab bar's or a navigation pane's: the focused
// window is the bar, the arrows moving its choice, so it stands for the
// chosen tab (a tab item) or item (a list item): its title and its badge,
// a count as so many items ("Inbox, 3 items"), the badge its item status
// too, as WinUI's InfoBadge says it.
static void choice_property(node *n, PROPERTYID id, VARIANT *v) {
	const wchar_t *title, *badge;
	chosen(n, &title, &badge);
	switch (id) {
	case UIA_NamePropertyId:
		v->vt = VT_BSTR;
		v->bstrVal = badged(title, badge);
		break;
	case UIA_ItemStatusPropertyId:
		if (*badge) {
			v->vt = VT_BSTR;
			v->bstrVal = badge_words(badge);
		}
		break;
	case UIA_ControlTypePropertyId:
		set_i4(v, n->kind == KIND_TAB_BAR ? UIA_TabItemControlTypeId : UIA_ListItemControlTypeId);
		break;
	case UIA_IsControlElementPropertyId:
	case UIA_IsContentElementPropertyId:
		set_bool(v, !a11y_hidden(n));
		break;
	}
}

// property is a view's property, or VT_EMPTY for its window's own.
static void property(node *n, PROPERTYID id, VARIANT *v) {
	if (n->kind == KIND_TAB_BAR || n->kind == KIND_SIDEBAR) {
		choice_property(n, id, v);
		return;
	}
	switch (id) {
	case UIA_NamePropertyId:
		if (n->a11y_label.n) {
			set_str(v, n->a11y_label.p, n->a11y_label.n);
		} else if (n->kind == AK_KIND_TEXT && n->text.n) {
			set_str(v, n->text.p, n->text.n);
		} else if (wh_pressable(n)) {
			ak_buf b = {0};
			collect_texts(n, &b);
			if (b.len) set_str(v, (const wchar_t *)b.data, (int)(b.len / sizeof(wchar_t)));
			ak_buf_free(&b);
		}
		break;
	case UIA_HelpTextPropertyId:
		if (n->a11y_hint.n) set_str(v, n->a11y_hint.p, n->a11y_hint.n);
		else if (n->tooltip.n) set_str(v, n->tooltip.p, n->tooltip.n);
		break;
	case UIA_ControlTypePropertyId:
		switch (n->a11y_role) {
		case AK_ROLE_BUTTON: set_i4(v, UIA_ButtonControlTypeId); return;
		case AK_ROLE_HEADER: case AK_ROLE_TEXT: set_i4(v, UIA_TextControlTypeId); return;
		case AK_ROLE_IMAGE: set_i4(v, UIA_ImageControlTypeId); return;
		case AK_ROLE_LINK: set_i4(v, UIA_HyperlinkControlTypeId); return;
		case AK_ROLE_SEARCH: set_i4(v, UIA_EditControlTypeId); return;
		}
		if (wh_pressable(n)) set_i4(v, UIA_ButtonControlTypeId);
		else if (n->kind == AK_KIND_TEXT) set_i4(v, UIA_TextControlTypeId);
		else if (n->kind == AK_KIND_IMAGE || n->kind == AK_KIND_SYMBOL) set_i4(v, UIA_ImageControlTypeId);
		else if (n->kind == AK_KIND_PROGRESS_RING) set_i4(v, UIA_ProgressBarControlTypeId);  // as WinUI's ring's peer
		else if (n->a11y_label.n) set_i4(v, UIA_GroupControlTypeId);
		break;
	case UIA_HeadingLevelPropertyId:
		if (n->a11y_role == AK_ROLE_HEADER) set_i4(v, HEADING_LEVEL_1);
		break;
	case UIA_IsControlElementPropertyId:
	case UIA_IsContentElementPropertyId:
		if (a11y_hidden(n)) set_bool(v, false);
		else if (n->kind == AK_KIND_TEXT || n->kind == AK_KIND_PROGRESS_RING || wh_pressable(n) || n->a11y_label.n) set_bool(v, true);
		break;
	}
}

static bool invokes(const node *n) { return n->hwnd && !n->disabled && node_listening(n, AK_EVENT_PRESS); }

// range is a ProgressRing's value for screen readers, as a percentage
// (read only, as WinUI's ring has it); an indeterminate one has none.
static bool range(const node *n, double *value) {
	if (n->kind != AK_KIND_PROGRESS_RING || n->indeterminate) return false;
	*value = n->value * 100;
	return true;
}

// ---- The providers: COM objects of the host's ----

// A view's provider holds the view's ID (the view may go while a screen
// reader holds it: it then says nothing); its patterns hold the provider.
struct wh_uia {
	IRawElementProviderSimple iface;
	LONG refs;
	uint32_t id;
	HWND hwnd;  // NULL once its window is gone
};

typedef struct {
	IInvokeProvider iface;
	LONG refs;
	struct wh_uia *p;
} invoker;

typedef struct {
	IRangeValueProvider iface;
	LONG refs;
	struct wh_uia *p;
} ranger;

static node *provider_node(struct wh_uia *p) {
	if (!p->hwnd) return NULL;
	node *n = wh_node(p->id);
	return n && n->hwnd == p->hwnd ? n : NULL;
}

static HRESULT STDMETHODCALLTYPE p_query(IRawElementProviderSimple *t, REFIID riid, void **out) {
	if (!out) return E_POINTER;
	*out = NULL;
	if (!IsEqualIID(riid, &iid_unknown) && !IsEqualIID(riid, &iid_simple)) return E_NOINTERFACE;
	*out = t;
	t->lpVtbl->AddRef(t);
	return S_OK;
}

static ULONG STDMETHODCALLTYPE p_add_ref(IRawElementProviderSimple *t) { return (ULONG)InterlockedIncrement(&((struct wh_uia *)t)->refs); }

static ULONG STDMETHODCALLTYPE p_release(IRawElementProviderSimple *t) {
	LONG r = InterlockedDecrement(&((struct wh_uia *)t)->refs);
	if (r == 0) free(t);
	return (ULONG)r;
}

static HRESULT STDMETHODCALLTYPE p_options(IRawElementProviderSimple *t, enum ProviderOptions *out) {
	(void)t;
	*out = ProviderOptions_ServerSideProvider;
	return S_OK;
}

static IInvokeProvider *new_invoker(struct wh_uia *p);
static IRangeValueProvider *new_ranger(struct wh_uia *p);

static HRESULT STDMETHODCALLTYPE p_pattern(IRawElementProviderSimple *t, PATTERNID id, IUnknown **out) {
	*out = NULL;
	struct wh_uia *p = (struct wh_uia *)t;
	node *n = provider_node(p);
	if (!n) return S_OK;
	double v;
	if (id == UIA_InvokePatternId && invokes(n)) *out = (IUnknown *)new_invoker(p);
	else if (id == UIA_RangeValuePatternId && range(n, &v)) *out = (IUnknown *)new_ranger(p);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_property(IRawElementProviderSimple *t, PROPERTYID id, VARIANT *out) {
	VariantInit(out);  // VT_EMPTY: the window's own
	node *n = provider_node((struct wh_uia *)t);
	if (n) property(n, id, out);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE p_host(IRawElementProviderSimple *t, IRawElementProviderSimple **out) {
	*out = NULL;
	struct wh_uia *p = (struct wh_uia *)t;
	if (p->hwnd && IsWindow(p->hwnd)) UiaHostProviderFromHwnd(p->hwnd, out);
	return S_OK;
}

static IRawElementProviderSimpleVtbl provider_vtbl = {p_query, p_add_ref, p_release, p_options, p_pattern, p_property, p_host};

// Invoke: the view's press.

static HRESULT STDMETHODCALLTYPE i_query(IInvokeProvider *t, REFIID riid, void **out) {
	if (!out) return E_POINTER;
	*out = NULL;
	if (!IsEqualIID(riid, &iid_unknown) && !IsEqualIID(riid, &iid_invoke)) return E_NOINTERFACE;
	*out = t;
	t->lpVtbl->AddRef(t);
	return S_OK;
}

static ULONG STDMETHODCALLTYPE i_add_ref(IInvokeProvider *t) { return (ULONG)InterlockedIncrement(&((invoker *)t)->refs); }

static ULONG STDMETHODCALLTYPE i_release(IInvokeProvider *t) {
	invoker *x = (invoker *)t;
	LONG r = InterlockedDecrement(&x->refs);
	if (r == 0) {
		p_release(&x->p->iface);
		free(x);
	}
	return (ULONG)r;
}

static HRESULT STDMETHODCALLTYPE i_invoke(IInvokeProvider *t) {
	node *n = provider_node(((invoker *)t)->p);
	if (!n || !invokes(n)) return UIA_E_ELEMENTNOTENABLED;
	wh_press(n, n->hwnd);
	return S_OK;
}

static IInvokeProviderVtbl invoke_vtbl = {i_query, i_add_ref, i_release, i_invoke};

static IInvokeProvider *new_invoker(struct wh_uia *p) {
	invoker *x = wh_alloc(sizeof *x);
	x->iface.lpVtbl = &invoke_vtbl;
	x->refs = 1;
	x->p = p;
	p_add_ref(&p->iface);
	return &x->iface;
}

// RangeValue, read only: setting it refuses.

static HRESULT STDMETHODCALLTYPE r_query(IRangeValueProvider *t, REFIID riid, void **out) {
	if (!out) return E_POINTER;
	*out = NULL;
	if (!IsEqualIID(riid, &iid_unknown) && !IsEqualIID(riid, &iid_range)) return E_NOINTERFACE;
	*out = t;
	t->lpVtbl->AddRef(t);
	return S_OK;
}

static ULONG STDMETHODCALLTYPE r_add_ref(IRangeValueProvider *t) { return (ULONG)InterlockedIncrement(&((ranger *)t)->refs); }

static ULONG STDMETHODCALLTYPE r_release(IRangeValueProvider *t) {
	ranger *x = (ranger *)t;
	LONG r = InterlockedDecrement(&x->refs);
	if (r == 0) {
		p_release(&x->p->iface);
		free(x);
	}
	return (ULONG)r;
}

static double r_now(IRangeValueProvider *t) {
	node *n = provider_node(((ranger *)t)->p);
	double v = 0;
	if (n && range(n, &v)) return v;
	return 0;
}

static HRESULT STDMETHODCALLTYPE r_set(IRangeValueProvider *t, double v) {
	(void)t, (void)v;
	return UIA_E_ELEMENTNOTENABLED;
}

static HRESULT STDMETHODCALLTYPE r_value(IRangeValueProvider *t, double *out) {
	*out = r_now(t);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_read_only(IRangeValueProvider *t, BOOL *out) {
	(void)t;
	*out = TRUE;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_max(IRangeValueProvider *t, double *out) {
	(void)t;
	*out = 100;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_min(IRangeValueProvider *t, double *out) {
	(void)t;
	*out = 0;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_large(IRangeValueProvider *t, double *out) {
	(void)t;
	*out = 10;
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_small(IRangeValueProvider *t, double *out) {
	(void)t;
	*out = 1;
	return S_OK;
}

static IRangeValueProviderVtbl range_vtbl = {r_query, r_add_ref, r_release, r_set, r_value, r_read_only, r_max, r_min, r_large, r_small};

static IRangeValueProvider *new_ranger(struct wh_uia *p) {
	ranger *x = wh_alloc(sizeof *x);
	x->iface.lpVtbl = &range_vtbl;
	x->refs = 1;
	x->p = p;
	p_add_ref(&p->iface);
	return &x->iface;
}

// wh_get_object answers WM_GETOBJECT for UI Automation: n's provider.
LRESULT wh_get_object(node *n, HWND hwnd, WPARAM wp, LPARAM lp, bool *handled) {
	*handled = false;
	if ((LONG)lp != UiaRootObjectId || n->ctl || n->kind == AK_KIND_NATIVE) return 0;  // a control's window, or the app's: theirs
	if (!n->uia) {
		struct wh_uia *p = wh_alloc(sizeof *p);
		p->iface.lpVtbl = &provider_vtbl;
		p->refs = 1;
		p->id = n->id, p->hwnd = hwnd;
		n->uia = p;
	}
	*handled = true;
	return UiaReturnRawElementProvider(hwnd, wp, lp, &n->uia->iface);
}

// wh_drop_provider lets go of a view's provider as its window goes.
void wh_drop_provider(node *n, HWND hwnd) {
	if (!n->uia) return;
	UiaReturnRawElementProvider(hwnd, 0, 0, NULL);
	n->uia->hwnd = NULL;
	p_release(&n->uia->iface);
	n->uia = NULL;
}

// wh_uia_range_changed tells screen readers a ProgressRing's value
// changed, so they read its new value as it fills.
void wh_uia_range_changed(node *n, double old, double now) {
	if (!n->uia || !UiaClientsAreListening()) return;
	VARIANT o, v;
	VariantInit(&o);
	VariantInit(&v);
	o.vt = v.vt = VT_R8;
	o.dblVal = old, v.dblVal = now;
	UiaRaiseAutomationPropertyChangedEvent(&n->uia->iface, UIA_RangeValueValuePropertyId, o, v);
}

// ---- Props ----

// focusable makes a view with actions or adjustable a tab stop: the
// keyboard reaches its context menu and its arrow keys.
static void focusable(node *n) {
	if (!n->hwnd || n->kind != AK_KIND_VIEW) return;
	if (n->na11y_actions || n->adjustable) SetWindowLongPtrW(n->hwnd, GWL_STYLE, GetWindowLongPtrW(n->hwnd, GWL_STYLE) | WS_TABSTOP);
}

// wh_set_a11y sets a screen reader prop (Label is views.c's).
void wh_set_a11y(node *n, uint16_t p, const ak_value *v) {
	switch (p) {
	case AK_PROP_A11Y_HINT:
		wstr_set(&n->a11y_hint, v->s);
		break;
	case AK_PROP_A11Y_ROLE:
		n->a11y_role = (int32_t)v->i;
		break;
	case AK_PROP_A11Y_HIDDEN:
		n->a11y_hidden = v->i != 0;
		break;
	case AK_PROP_A11Y_ACTIONS:
		wh_free_strings(n->a11y_actions, n->na11y_actions);
		n->na11y_actions = wh_decode_strings(&n->a11y_actions, (const uint8_t *)v->s.p, v->s.len);
		focusable(n);
		break;
	case AK_PROP_A11Y_ADJUSTABLE:
		n->adjustable = v->i != 0;
		focusable(n);
		break;
	}
	if (n->hwnd && UiaClientsAreListening()) NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, n->hwnd, OBJID_CLIENT, CHILDID_SELF);
}

// wh_adjust_key adjusts an adjustable view with the arrow keys: up and
// the reading direction's forward arrow by one step, the others back.
bool wh_adjust_key(node *n, WPARAM vk) {
	if (!n->adjustable) return false;
	int64_t by;
	switch (vk) {
	case VK_UP: by = 1; break;
	case VK_DOWN: by = -1; break;
	case VK_RIGHT:
	case VK_LEFT:
		by = (vk == VK_LEFT) != wh_is_rtl(n) ? -1 : 1;
		break;
	default:
		return false;
	}
	wh_emit_int(n, AK_EVENT_ADJUST, by);
	return true;
}

typedef struct {
	uint32_t id;
	bool from_keys;
} actions_job;

static void actions_now(void *ctx) {
	actions_job *j = ctx;
	node *n = wh_node(j->id);
	if (n && n->hwnd && n->na11y_actions) {
		actions a = {wh_alloc(n->na11y_actions * sizeof *a.v), n->na11y_actions};
		for (uint32_t i = 0; i < a.n; i++) a.v[i] = (action){.title = n->a11y_actions[i], .index = i};
		bool rtl = wh_is_rtl(n);
		POINT p;
		GetCursorPos(&p);
		if (j->from_keys) {
			RECT r;
			GetWindowRect(n->hwnd, &r);
			p = (POINT){rtl ? r.right : r.left, r.bottom};
		}
		int64_t got = wh_track_actions(&a, rtl, p, GetAncestor(n->hwnd, GA_ROOT));
		free(a.v);
		if (got >= 0 && (n = wh_node(j->id))) wh_emit_int(n, AK_EVENT_A11Y_ACTION, got);
	}
	free(j);
}

// wh_actions_menu shows a view's screen reader actions as a popup menu
// (its context menu), at the mouse or, from the keyboard (lp -1), at the
// view; the action taken is A11yAction.
bool wh_actions_menu(node *n, HWND hwnd, LPARAM lp) {
	(void)hwnd;
	if (!n->na11y_actions) return false;
	actions_job *j = wh_alloc(sizeof *j);
	j->id = n->id;
	j->from_keys = (LONG)lp == -1;
	wh_after_core(actions_now, j);
	return true;
}

// ---- Announcements ----

typedef HRESULT(WINAPI *notify_fn)(IRawElementProviderSimple *, enum NotificationKind, enum NotificationProcessing, BSTR, BSTR);

// announce has screen readers say text, through hwnd's provider (Windows
// 10 1709 and later).
static void announce(HWND hwnd, const wchar_t *text) {
	static notify_fn notify;
	static bool looked;
	if (!looked) {
		looked = true;
		HMODULE m = GetModuleHandleW(L"uiautomationcore.dll");
		if (!m) m = LoadLibraryExW(L"uiautomationcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (m) notify = (notify_fn)(void (*)(void))GetProcAddress(m, "UiaRaiseNotificationEvent");
	}
	if (!notify) return;
	IRawElementProviderSimple *host = NULL;
	if (FAILED(UiaHostProviderFromHwnd(hwnd, &host)) || !host) return;
	BSTR s = SysAllocString(text), id = SysAllocString(L"asuka.announce");
	notify(host, NotificationKind_Other, NotificationProcessing_ImportantAll, s, id);
	SysFreeString(s);
	SysFreeString(id);
	host->lpVtbl->Release(host);
}

// wh_announce has screen readers say text (the Announce command).
void wh_announce(const char *s, size_t len) {
	if (!len || !H.window) return;
	wchar_t *w = wh_wide(s, (int)len);
	announce(wh_window_in_use(), w);
	free(w);
}

// wh_announce_choice says the tab or item the keyboard chose.
void wh_announce_choice(node *b) {
	const wchar_t *title, *badge;
	if (!chosen(b, &title, &badge) || !*title || !H.window) return;
	BSTR s = badged(title, badge);
	if (s) announce(wh_window_in_use(), s);
	SysFreeString(s);
}

// ---- A control's accessible name ----

static IAccPropServices *acc_props;  // made once

// wh_set_accessible_name names hwnd (its client object) for screen
// readers; empty: its own name again.
void wh_set_accessible_name(HWND hwnd, const wchar_t *name) {
	if (!hwnd) return;
	if (!name || !*name) {
		wh_clear_accessible_name(hwnd);
		return;
	}
	if (!acc_props && FAILED(CoCreateInstance(&clsid_acc_prop_services, NULL, CLSCTX_INPROC_SERVER, &iid_acc_prop_services, (void **)&acc_props)))
		acc_props = NULL;
	if (!acc_props) return;
	IAccPropServices_SetHwndPropStr(acc_props, hwnd, (DWORD)OBJID_CLIENT, CHILDID_SELF, propid_acc_name, name);
}

// wh_clear_accessible_name drops the name wh_set_accessible_name gave
// hwnd, as Dynamic Annotation asks before the window goes.
void wh_clear_accessible_name(HWND hwnd) {
	if (!acc_props || !hwnd) return;
	MSAAPROPID props[1] = {propid_acc_name};
	IAccPropServices_ClearHwndProps(acc_props, hwnd, (DWORD)OBJID_CLIENT, CHILDID_SELF, props, 1);
}

// ---- Tooltips ----

#define TOOLTIP_MAX_WIDTH_PX 400

// wh_tooltip_on gives the window hwnd the tooltip text, replacing the one
// it had; NULL or empty: none. The tooltip control watches each tool's
// mouse itself (TTF_SUBCLASS), and shows the text where the pointer
// rests, themed as the system's.
void wh_tooltip_on(HWND hwnd, const wchar_t *tip) {
	if (!hwnd) return;
	bool has = tip && *tip;
	if (!H.tips) {
		if (!has) return;
		H.tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, L"", WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, H.window, NULL,
			GetModuleHandleW(NULL), NULL);
		if (!H.tips) return;
		SendMessageW(H.tips, TTM_SETMAXTIPWIDTH, 0, TOOLTIP_MAX_WIDTH_PX);
	}
	TTTOOLINFOW ti = {.cbSize = sizeof ti, .uFlags = TTF_IDISHWND | TTF_SUBCLASS, .hwnd = hwnd, .uId = (UINT_PTR)hwnd,
		.hinst = GetModuleHandleW(NULL)};
	SendMessageW(H.tips, TTM_DELTOOLW, 0, (LPARAM)&ti);
	if (!has) return;
	ti.lpszText = (wchar_t *)tip;  // the control copies it
	SendMessageW(H.tips, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// wh_set_tooltip is a view's Tooltip.
void wh_set_tooltip(node *n, const wchar_t *tip) { wh_tooltip_on(n->hwnd, tip); }
