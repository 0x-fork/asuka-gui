// eds.c: asuka/contacts and asuka/calendar. With Evolution Data Server
// they'd read and write its address books and calendars; until that
// lands here, they're as on a system without it (system.c): contacts
// granted but not listed, calendars write only, adding one opens a file
// in the user's app for it.
#include "host.h"

void lx_contacts_request(ak_span args, reply *r);
void lx_contacts_list(ak_span args, reply *r);
void lx_contacts_pick(ak_span args, reply *r);
void lx_contacts_add(ak_span args, reply *r);
void lx_calendar_request(ak_span args, reply *r);
void lx_calendar_not_allowed(ak_span args, reply *r);
void lx_calendar_add(ak_span args, reply *r);

static const method_t methods[] = {
	{"contacts", "request", lx_contacts_request},
	{"contacts", "list", lx_contacts_list},
	{"contacts", "pick", lx_contacts_pick},
	{"contacts", "add", lx_contacts_add},
	{"calendar", "request", lx_calendar_request},
	{"calendar", "calendars", lx_calendar_not_allowed},
	{"calendar", "events", lx_calendar_not_allowed},
	{"calendar", "add", lx_calendar_add},
};

void lx_eds_modules(void) { lx_register_methods(methods, AK_LEN(methods)); }
