// asuka/network on Windows: the Network List Manager (connectivity, and
// the connection's cost: metered, roaming, over its data limit as
// constrained), made on the main thread, whose apartment its events come
// back to; the connection's adapter's kind from GetAdaptersAddresses.
#include <winsock2.h>
#include <ws2ipdef.h>
#include "asuka_windows.h"
#include <iphlpapi.h>
#include <stdio.h>
#include "network.asuka.h"

#include <stdlib.h>
#include <string.h>

static const GUID clsid_nlm = {0xDCB00C01, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
static const GUID iid_nlm = {0xDCB00000, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
static const GUID iid_events = {0xDCB00001, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
static const GUID iid_cost = {0xDCB00008, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
static const GUID iid_cpc = {0xB196B284, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};

// NLM_CONNECTIVITY's and NLM_CONNECTION_COST's bits.
enum {
	connected = 0x10 | 0x20 | 0x40 | 0x100 | 0x200 | 0x400,  // IPv4 and IPv6 subnet, local network, internet
	internet = 0x40 | 0x400,
	cost_fixed = 0x2,
	cost_variable = 0x4,
	cost_over = 0x10000,
	cost_roaming = 0x40000,
	cost_near = 0x80000,
};

typedef HRESULT(STDMETHODCALLTYPE *get_u32)(void *, DWORD *);

static void *manager(void) {
	static void *nlm;
	if (!nlm) CoCreateInstance(&clsid_nlm, NULL, CLSCTX_ALL, &iid_nlm, &nlm);
	return nlm;
}

static network_ConnectionType adapter_kind(const GUID *id) {
	ULONG size = 16 << 10;
	IP_ADAPTER_ADDRESSES *buf = NULL;
	for (int i = 0; i < 3; i++) {
		free(buf);
		buf = malloc(size);
		ULONG r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, buf, &size);
		if (r == NO_ERROR) break;
		if (r != ERROR_BUFFER_OVERFLOW) {
			free(buf);
			return network_ConnectionType_other;
		}
	}
	char want[40];
	char g[37];
	asuka_guid_string(id, g);
	snprintf(want, sizeof want, "{%s}", g);
	network_ConnectionType k = network_ConnectionType_other;
	for (IP_ADAPTER_ADDRESSES *a = buf; a; a = a->Next) {
		if (!a->AdapterName || _stricmp(a->AdapterName, want) != 0) continue;
		switch (a->IfType) {
		case IF_TYPE_IEEE80211: k = network_ConnectionType_wifi; break;
		case 243:
		case 244: k = network_ConnectionType_cellular; break;  // WWAN
		case IF_TYPE_ETHERNET_CSMACD: k = network_ConnectionType_ethernet; break;
		}
		break;
	}
	free(buf);
	return k;
}

// primary is the adapter of the connection to the internet (else of the
// first connected one).
static bool primary(void *nlm, GUID *out) {
	void *en = NULL;
	if (FAILED(ASUKA_SLOT(nlm, 9, HRESULT(STDMETHODCALLTYPE *)(void *, void **))(nlm, &en)) || !en) return false;  // GetNetworkConnections
	bool found = false;
	for (int i = 0; i < 64; i++) {
		void *c = NULL;
		ULONG got = 0;
		if (ASUKA_SLOT(en, 8, HRESULT(STDMETHODCALLTYPE *)(void *, ULONG, void **, ULONG *))(en, 1, &c, &got) != S_OK || !got || !c) break;  // Next
		DWORD conn = 0;
		GUID id;
		memset(&id, 0, sizeof id);
		ASUKA_SLOT(c, 10, get_u32)(c, &conn);                                       // GetConnectivity
		ASUKA_SLOT(c, 12, HRESULT(STDMETHODCALLTYPE *)(void *, GUID *))(c, &id);  // GetAdapterId
		asuka_com_release(c);
		if (conn & internet) {
			*out = id;
			found = true;
			break;
		}
		if ((conn & connected) && !found) *out = id, found = true;
	}
	asuka_com_release(en);
	return found;
}

static network_Status state(void) {
	network_Status s = {0};
	void *nlm = manager();
	if (!nlm) return s;
	DWORD conn = 0;
	ASUKA_SLOT(nlm, 13, get_u32)(nlm, &conn);  // GetConnectivity
	s.connected = (conn & connected) != 0;
	if (!s.connected) return s;
	s.type = network_ConnectionType_other;
	GUID id;
	if (primary(nlm, &id)) s.type = adapter_kind(&id);
	void *cm = asuka_com_query(nlm, &iid_cost);
	if (cm) {
		DWORD cost = 0;
		if (SUCCEEDED(ASUKA_SLOT(cm, 3, HRESULT(STDMETHODCALLTYPE *)(void *, DWORD *, void *))(cm, &cost, NULL))) {  // GetCost
			s.metered = (cost & (cost_fixed | cost_variable | cost_roaming | cost_over)) != 0;
			s.constrained = (cost & (cost_over | cost_near)) != 0;
		}
		asuka_com_release(cm);
	}
	return s;
}

static network_Status m_current(network_Monitor *self, asuka_error *err) {
	if (!manager()) {
		asuka_error_unsupported(err, "the Network List Manager isn't there");
		return (network_Status){0};
	}
	return state();
}

typedef struct {
	network_fn_Status changed;
	asuka_call *call;
	void *point;
	DWORD cookie;
	network_Status last;
	bool first, done;
} watching;

static void report(watching *w) {
	if (w->done) return;
	network_Status s = state();
	if (!w->first && memcmp(&s, &w->last, sizeof s) == 0) return;
	w->first = false;
	w->last = s;
	network_fn_Status_call(w->changed, &s);
}

// on_event is INetworkListManagerEvents' ConnectivityChanged, on the
// main thread.
static void on_event(void *p, void *a, void *b) { report(p); }

static void stop(void *p) {
	watching *w = p;
	w->done = true;
	if (w->point) {
		ASUKA_SLOT(w->point, 6, HRESULT(STDMETHODCALLTYPE *)(void *, DWORD))(w->point, w->cookie);  // Unadvise
		asuka_com_release(w->point);
	}
	asuka_error e = {0};
	asuka_error_cancelled(&e);
	asuka_call_fail(w->call, &e);
	free(w);  // its events stopped with Unadvise (all on the main thread)
}

static void m_watch(network_Monitor *self, network_fn_Status changed, asuka_call *call) {
	void *nlm = manager();
	if (!nlm) {
		asuka_error e = {0};
		asuka_error_unsupported(&e, "the Network List Manager isn't there");
		asuka_call_fail(call, &e);
		return;
	}
	watching *w = calloc(1, sizeof *w);
	w->changed = changed;
	w->call = call;
	w->first = true;
	void *cpc = asuka_com_query(nlm, &iid_cpc);
	void *sink = asuka_winrt_delegate(&iid_events, on_event, w, NULL);
	if (cpc) {
		void *cp = NULL;
		if (SUCCEEDED(ASUKA_SLOT(cpc, 4, HRESULT(STDMETHODCALLTYPE *)(void *, const GUID *, void **))(cpc, &iid_events, &cp)) && cp) {  // FindConnectionPoint
			if (SUCCEEDED(ASUKA_SLOT(cp, 5, HRESULT(STDMETHODCALLTYPE *)(void *, void *, DWORD *))(cp, sink, &w->cookie))) w->point = cp;  // Advise
			else asuka_com_release(cp);
		}
		asuka_com_release(cpc);
	}
	report(w);
	asuka_call_on_cancel(call, stop, w);
	asuka_com_release(sink);  // the connection point holds it while advised
}

static const network_Monitor_vtable vt = {.current = m_current, .watch = m_watch};

network_Monitor *network_make_Monitor(asuka_error *err) {
	network_Monitor *m = malloc(sizeof *m);
	m->vt = &vt;
	return m;
}
