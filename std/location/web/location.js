// asuka/location in the browser: the Geolocation API, and the
// permission (navigator.permissions' geolocation). A page has no
// background: the updates come while it's open (a watch, each position
// a batch), and the regions are the package's own, computed from the
// positions (support's regions is false). A browser has no geocoder.
import { Level, LocationError } from "./location.asuka.js";

let status = null; // the PermissionStatus, once asked for
let state = "prompt";
const authReaders = new Set();

function authorization() {
  const level = { granted: Level.whenInUse, denied: Level.denied }[state] ?? Level.notAsked;
  // A browser doesn't say whether positions are precise.
  return { level, precise: true, off: false };
}

function changed() {
  state = status?.state ?? state;
  const a = authorization();
  for (const r of authReaders) r(a);
}

async function query() {
  if (!status && navigator.permissions?.query) {
    try {
      status = await navigator.permissions.query({ name: "geolocation" });
      status.addEventListener("change", changed);
      state = status.state;
    } catch { /* a browser without the geolocation permission's name */ }
  }
  return authorization();
}

function geo() {
  if (!navigator.geolocation) throw new LocationError("unavailable");
  return navigator.geolocation;
}

function position(p) {
  const c = p.coords;
  const known = (v) => (v == null || Number.isNaN(v) || v < 0 ? -1 : v);
  return {
    latitude: c.latitude, longitude: c.longitude, accuracy: c.accuracy, altitude: c.altitude ?? 0,
    speed: known(c.speed), course: known(c.heading), time: Math.round(p.timestamp) * 1e6,
  };
}

function failure(e) {
  if (e?.code === 1) {
    state = "denied";
    changed();
    return new LocationError("denied");
  }
  return new Error(e?.message || "location: no position");
}

// The background updates, while the page is open.
let updates = null; // { watch }
const eventReaders = new Set();
let nextEvent = 0;

export function makeLocator() {
  query();
  return {
    authorization,

    async watchAuthorization(signal, f) {
      await query();
      f(authorization());
      authReaders.add(f);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      authReaders.delete(f);
    },

    async request(signal, always) {
      await query();
      if (state === "prompt") {
        await new Promise((resolve) => geo().getCurrentPosition(
          () => { if (!status) state = "granted"; resolve(); },
          (e) => { if (!status && e?.code === 1) state = "denied"; resolve(); }));
      }
      changed();
      return authorization();
    },

    async requestPrecise() {
      return query();
    },

    current() {
      return new Promise((resolve, reject) => {
        geo().getCurrentPosition((p) => { if (!status) state = "granted"; resolve(position(p)); }, (e) => reject(failure(e)), { enableHighAccuracy: true, maximumAge: 0 });
      });
    },

    async watch(signal, moved) {
      let fail;
      const failed = new Promise((_, reject) => { fail = reject; });
      const id = geo().watchPosition((p) => moved(position(p)), (e) => { if (e.code === 1) fail(failure(e)); }, { enableHighAccuracy: true });
      try {
        await Promise.race([failed, new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }))]);
      } finally {
        geo().clearWatch(id);
      }
    },

    async placeName() {
      throw new Error("unsupported: a browser has no geocoder (ask a geocoding service with asuka/http)");
    },

    openSettings() {
      throw new Error("unsupported: a page can't open the browser's settings");
    },

    support() {
      return { updates: false, regions: false, maxRegions: 0, maxRadius: 0, dwell: true, relaunch: false };
    },

    async startUpdates(signal, options) {
      if (updates) geo().clearWatch(updates.watch);
      const watch = geo().watchPosition((p) => {
        const e = { id: ++nextEvent, positions: [position(p)], crossing: false, region: "", kind: 0, at: null, time: 0 };
        for (const r of eventReaders) r(e);
      }, () => {}, { enableHighAccuracy: options.accuracy <= 1 });
      updates = { watch };
    },

    stopUpdates() {
      if (updates) geo().clearWatch(updates.watch);
      updates = null;
    },

    async updating() {
      return updates != null;
    },

    async monitor() { throw new LocationError("unavailable"); },
    async stopMonitoring() {},
    async stopMonitoringAll() {},
    async monitored() { return []; },

    async events(signal, f) {
      eventReaders.add(f);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      eventReaders.delete(f);
    },

    hold() {},
    done() {},
  };
}
