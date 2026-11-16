// util.c: the Windows engine's portable part (util.h): advertisements'
// sections, scan responses, addresses, errors, the picker's selectors.
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has(const bt_strings *s, const char *x) {
	for (size_t i = 0; i < s->len; i++)
		if (!strcmp(s->data[i], x)) return true;
	return false;
}

char *btw_uuid_le(const uint8_t *b, size_t n) {
	uint8_t u[16] = {0, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0x80, 0x5f, 0x9b, 0x34, 0xfb};
	if (n == 2) u[2] = b[1], u[3] = b[0];
	else if (n == 4) u[0] = b[3], u[1] = b[2], u[2] = b[1], u[3] = b[0];
	else if (n == 16)
		for (int i = 0; i < 16; i++) u[i] = b[15 - i];
	else return NULL;
	char s[37];
	snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10],
	         u[11], u[12], u[13], u[14], u[15]);
	return asuka_strdup(s);
}

static void uuids(bt_strings *dst, const uint8_t *b, size_t len, size_t n) {
	for (size_t i = 0; i + n <= len; i += n) {
		char *u = btw_uuid_le(b + i, n);
		if (u && !has(dst, u)) bt_strings_push(dst, u);
		else free(u);
	}
}

static void put_entry(bt_entries *e, char *key, const uint8_t *p, size_t n) {
	for (size_t i = 0; i < e->len; i++)
		if (!strcmp(e->data[i].key, key)) {  // the last one's
			free(e->data[i].value.data);
			e->data[i].value = asuka_bytes_copy(p, n);
			free(key);
			return;
		}
	bt_entries_push(e, key, p, n);
}

btw_ad btw_parse_sections(const btw_section *s, size_t n) {
	btw_ad a = {0};
	for (size_t i = 0; i < n; i++) {
		const uint8_t *d = s[i].data;
		size_t len = s[i].len;
		switch (s[i].type) {
		case 0x02:
		case 0x03:  // incomplete, complete 16-bit service UUIDs
			uuids(&a.services, d, len, 2);
			break;
		case 0x04:
		case 0x05:  // 32-bit
			uuids(&a.services, d, len, 4);
			break;
		case 0x06:
		case 0x07:  // 128-bit
			uuids(&a.services, d, len, 16);
			break;
		case 0x08:
		case 0x09:  // shortened, complete local name
			if (s[i].type == 0x09 || !a.name) {
				size_t k = len;
				while (k > 0 && d[k - 1] == 0) k--;
				free(a.name);
				a.name = asuka_strndup((const char *)d, k);
			}
			break;
		case 0x0a:  // Tx Power Level
			if (len == 1) a.has_tx_power = true, a.tx_power = (int8_t)d[0];
			break;
		case 0x14:  // solicitation: 16-bit
			uuids(&a.solicited, d, len, 2);
			break;
		case 0x1f:  // 32-bit
			uuids(&a.solicited, d, len, 4);
			break;
		case 0x15:  // 128-bit
			uuids(&a.solicited, d, len, 16);
			break;
		case 0x16:  // service data: a 16-bit UUID
		case 0x20:  // 32-bit
		case 0x21: {  // 128-bit
			size_t k = s[i].type == 0x16 ? 2 : s[i].type == 0x20 ? 4 : 16;
			if (len >= k) put_entry(&a.service_data, btw_uuid_le(d, k), d + k, len - k);
			break;
		}
		case 0xff:  // manufacturer specific: the company first, little endian
			if (len >= 2) put_entry(&a.manufacturer, asuka_sprintf("%04x", d[0] | d[1] << 8), d + 2, len - 2);
			break;
		}
	}
	return a;
}

void btw_ad_free(btw_ad *a) {
	free(a->name);
	bt_strings_free(&a->services);
	bt_strings_free(&a->solicited);
	bt_entries_free(&a->service_data);
	bt_entries_free(&a->manufacturer);
	*a = (btw_ad){0};
}

void btw_take(btw_heard *h, btw_packet *p) {
	h->rssi = p->rssi, h->time_ms = p->time_ms;
	if (p->response) {
		btw_ad_free(&h->rsp);
		h->rsp = p->ad, h->has_rsp = true;
		p->ad = (btw_ad){0};
		return;
	}
	btw_ad_free(&h->adv);
	h->adv = p->ad, h->has_adv = true;
	p->ad = (btw_ad){0};
	h->scannable = p->scannable;
	if (!h->has_rsp) h->advs++;
	if (p->connectable >= 0) h->connectable = p->connectable;
	else if (p->type == BTW_ADV_CONNECTABLE_UNDIRECTED || p->type == BTW_ADV_CONNECTABLE_DIRECTED) h->connectable = 1;
	else if (p->type == BTW_ADV_SCANNABLE_UNDIRECTED || p->type == BTW_ADV_NON_CONNECTABLE_UNDIRECTED) h->connectable = 0;
}

bool btw_complete(const btw_heard *h) { return h->has_rsp || (h->has_adv && (!h->scannable || h->advs >= 2)); }

bt_advertisement btw_advertisement(const btw_heard *h, const char *id) {
	bt_advertisement a = {.id = asuka_strdup(id), .rssi = h->rssi, .time_ms = h->time_ms, .connectable = h->connectable != 0};
	const btw_ad *parts[2];
	size_t n = 0;
	if (h->has_adv) parts[n++] = &h->adv;
	if (h->has_rsp) parts[n++] = &h->rsp;
	for (size_t i = 0; i < n; i++) {
		const btw_ad *p = parts[i];
		if ((!a.local_name || !*a.local_name) && p->name) {
			free(a.local_name);
			a.local_name = asuka_strdup(p->name);
		}
		for (size_t k = 0; k < p->services.len; k++)
			if (!has(&a.services, p->services.data[k])) bt_strings_push(&a.services, asuka_strdup(p->services.data[k]));
		for (size_t k = 0; k < p->solicited.len; k++)
			if (!has(&a.solicited, p->solicited.data[k])) bt_strings_push(&a.solicited, asuka_strdup(p->solicited.data[k]));
		for (size_t k = 0; k < p->service_data.len; k++)
			put_entry(&a.service_data, asuka_strdup(p->service_data.data[k].key), p->service_data.data[k].value.data, p->service_data.data[k].value.len);
		for (size_t k = 0; k < p->manufacturer.len; k++)
			put_entry(&a.manufacturer_data, asuka_strdup(p->manufacturer.data[k].key), p->manufacturer.data[k].value.data, p->manufacturer.data[k].value.len);
		if (!a.has_tx_power && p->has_tx_power) a.has_tx_power = true, a.tx_power = p->tx_power;
	}
	if (!a.local_name) a.local_name = asuka_strdup("");
	a.name = asuka_strdup(a.local_name);
	return a;
}

void btw_heard_free(btw_heard *h) {
	btw_ad_free(&h->adv);
	btw_ad_free(&h->rsp);
}

bool btw_matches(const bt_advertisement *a, const bt_strings *services) {
	if (services->len == 0) return true;
	for (size_t i = 0; i < services->len; i++) {
		char *u = bt_normalize(services->data[i]);
		bool m = has(&a->services, u);
		free(u);
		if (m) return true;
	}
	return false;
}

char *btw_address(uint64_t a) {
	return asuka_sprintf("%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)(a >> 40 & 255), (unsigned)(a >> 32 & 255), (unsigned)(a >> 24 & 255),
	                     (unsigned)(a >> 16 & 255), (unsigned)(a >> 8 & 255), (unsigned)(a & 255));
}

bool btw_parse_address(const char *s, uint64_t *out) {
	while (*s == ' ' || *s == '\t') s++;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
	char digits[13];
	size_t k = 0;
	if (n == 17) {
		char sep = s[2];
		if (sep != ':' && sep != '-') return false;
		for (size_t i = 0; i < 17; i++) {
			if (i % 3 == 2) {
				if (s[i] != sep) return false;
				continue;
			}
			digits[k++] = s[i];
		}
	} else if (n == 12) {
		memcpy(digits, s, 12);
		k = 12;
	} else {
		return false;
	}
	uint64_t v = 0;
	for (size_t i = 0; i < k; i++) {
		char c = digits[i];
		int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (d < 0) return false;
		v = v << 4 | (uint64_t)d;
	}
	*out = v;
	return true;
}

char *btw_central_id(const char *device_id) {
	const char *dash = strrchr(device_id, '-');
	uint64_t a;
	if (dash && btw_parse_address(dash + 1, &a)) return btw_address(a);
	return asuka_strdup(device_id);
}

static const char *att_names[] = {
    NULL,
    "invalid handle",
    "read not permitted",
    "write not permitted",
    "invalid PDU",
    "insufficient authentication",
    "request not supported",
    "invalid offset",
    "insufficient authorization",
    "prepare queue full",
    "attribute not found",
    "attribute not long",
    "insufficient encryption key size",
    "invalid attribute value length",
    "unlikely error",
    "insufficient encryption",
    "unsupported group type",
    "insufficient resources",
};

char *btw_att(int code) {
	if (code > 0 && code < (int)(sizeof att_names / sizeof *att_names)) return asuka_sprintf("att %d: %s", code, att_names[code]);
	return asuka_sprintf("att %d: error 0x%02x", code, code);
}

char *btw_gatt_error(int status, int proto, uint32_t hr) {
	switch (status) {
	case 0:  // Success
		return NULL;
	case 1:  // Unreachable
		return asuka_strdup(BT_DISCONNECTED);
	case 2:  // ProtocolError
		return btw_att(proto < 0 ? 0x0e : proto);
	case 3:  // AccessDenied
		return asuka_strdup(BT_DENIED);
	}
	switch (hr) {
	case 0x80000013:  // RO_E_CLOSED
	case 0x8007048f:  // ERROR_DEVICE_NOT_CONNECTED
	case 0x800710df:  // ERROR_DEVICE_NOT_AVAILABLE
		return asuka_strdup(BT_DISCONNECTED);
	case 0x80070005:  // E_ACCESSDENIED
		return asuka_strdup(BT_DENIED);
	case 0:
		return asuka_strdup("failed");
	}
	return asuka_sprintf("failed: 0x%x", hr);
}

const char *btw_error(int e) {
	switch (e) {
	case 0: return NULL;
	case 1: return BT_OFF;  // RadioNotAvailable
	case 2: return "failed: another app uses the radio";
	case 3: return BT_DISCONNECTED;
	case 5:
	case 7:
	case 8: return BT_DENIED;  // DisabledByPolicy, DisabledByUser, ConsentRequired
	case 6:
	case 9: return "not available: this Bluetooth adapter can't";  // NotSupported, TransportNotSupported
	}
	return "failed";
}

const char *btw_radio_state(int st) {
	switch (st) {
	case 1: return BT_ON;
	case 2:
	case 3: return BT_OFF;  // Off, Disabled
	}
	return BT_UNKNOWN;
}

bool btw_cccd(int props, bool indicate, int *v) {
	if (!(props & (BT_PROP_NOTIFY | BT_PROP_INDICATE))) return false;
	*v = (indicate && (props & BT_PROP_INDICATE)) || !(props & BT_PROP_NOTIFY) ? 2 : 1;
	return true;
}

static char *quoted(const char *s) {
	asuka_writer w = {0};
	asuka_writer_u8(&w, '"');
	for (; *s; s++)
		if (*s != '"') asuka_writer_u8(&w, (uint8_t)*s);
	asuka_writer_u8(&w, '"');
	asuka_writer_u8(&w, 0);
	return (char *)w.data;
}

bt_strings btw_chooser_selectors(const bt_strings *bases, const bluetooth_list_Filter *filters) {
	bt_strings out = {0};
	for (size_t i = 0; filters && i < filters->len; i++) {
		const bluetooth_Filter *f = &filters->data[i];
		char *clause;
		if (f->name && *f->name) {
			char *q = quoted(f->name);
			clause = asuka_sprintf(" AND System.ItemNameDisplay:=%s", q);
			free(q);
		} else if (f->namePrefix && *f->namePrefix) {
			char *q = quoted(f->namePrefix);
			clause = asuka_sprintf(" AND System.ItemNameDisplay:~<%s", q);
			free(q);
		} else {
			bt_strings_free(&out);
			break;  // a filter without a name: every device the bases find
		}
		for (size_t k = 0; k < bases->len; k++) bt_strings_push(&out, asuka_sprintf("(%s)%s", bases->data[k], clause));
		free(clause);
	}
	if (out.len == 0)
		for (size_t k = 0; k < bases->len; k++) bt_strings_push(&out, asuka_strdup(bases->data[k]));
	return out;
}

void btw_diff(const bt_strings *old, const bt_strings *now, bt_strings *on, bt_strings *off) {
	for (size_t i = 0; i < now->len; i++)
		if (!has(old, now->data[i]) && !has(on, now->data[i])) bt_strings_push(on, asuka_strdup(now->data[i]));
	for (size_t i = 0; i < old->len; i++)
		if (!has(now, old->data[i]) && !has(off, old->data[i])) bt_strings_push(off, asuka_strdup(old->data[i]));
}
