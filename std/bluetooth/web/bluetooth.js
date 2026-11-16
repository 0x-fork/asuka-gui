// asuka/bluetooth in the browser: Web Bluetooth (Chromium, on the
// desktop and Android; not Safari or Firefox).
//
//   - state: getAvailability(), on or off; availabilitychanged. Without
//     navigator.bluetooth (Safari, Firefox, a page not on https):
//     unsupported. The browser asks for each device, in its chooser:
//     request asks nothing.
//   - choose: requestDevice, the filters the app's (acceptAllDevices
//     with none). The browser shows it only while the user's tap is
//     recent (its transient activation).
//   - scan: only through requestLEScan (Chromium's, behind a flag);
//     without it, notAvailable: choose instead.
//   - connect: gatt.connect(); gattserverdisconnected ends it. A device
//     has one GATT server for the page; its operations go one at a time
//     (Chromium refuses one while another is in progress). Only the
//     services the chooser's filters and optionalServices name are
//     there.
//   - A page can't be a peripheral: serve and advertise are
//     notAvailable.
//   - Nor is it started for Bluetooth, or scanning without its page:
//     nothing is restored (watchRestored returns).
import { State, ConnectionState, Property, BluetoothError } from "./bluetooth.asuka.js";

const BASE = "-0000-1000-8000-00805f9b34fb";
const short = (u) => {
  const s = String(u).toLowerCase();
  return s.startsWith("0000") && s.endsWith(BASE) ? s.slice(4, 8) : s;
};
const full = (u) => (u.length === 4 ? `0000${u}${BASE}` : u);
const bytesOf = (dv) => (dv ? new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength).slice() : new Uint8Array(0));

const bt = () => globalThis.navigator?.bluetooth;

const devices = new Map(); // id → BluetoothDevice
let available = null; // known once asked

function failure(e) {
  switch (e?.name) {
    case "NotFoundError": return new BluetoothError("canceled");
    case "SecurityError":
    case "NotAllowedError": return new BluetoothError("denied");
    case "NetworkError": return new BluetoothError("disconnected");
    case "NotSupportedError": return new BluetoothError("unsupported");
    default: return e;
  }
}

if (bt()?.getAvailability) bt().getAvailability().then((a) => { available = a; }, () => { available = false; });

/** One connection's GATT, and its operations, one at a time. */
class Conn {
  constructor(device, changed) {
    this.device = device;
    this.changed = changed;
    this.chain = Promise.resolve();
    this.chars = new Map(); // "service char" → BluetoothRemoteGATTCharacteristic
  }

  op(f) {
    const p = this.chain.then(f, f);
    this.chain = p.catch(() => {});
    return p;
  }

  async char(service, char) {
    const key = `${service} ${char}`;
    if (!this.chars.has(key)) {
      const s = await this.op(() => this.device.gatt.getPrimaryService(full(service)));
      this.chars.set(key, await this.op(() => s.getCharacteristic(full(char))));
    }
    return this.chars.get(key);
  }
}

const conns = new Map(); // handle → Conn
let handles = 0;

function conn(h) {
  const c = conns.get(h);
  if (!c || !c.device.gatt?.connected) throw new BluetoothError("disconnected");
  return c;
}

const props = (p) => {
  const out = [];
  if (p.broadcast) out.push(Property.broadcast);
  if (p.read) out.push(Property.read);
  if (p.writeWithoutResponse) out.push(Property.writeWithoutResponse);
  if (p.write) out.push(Property.write);
  if (p.notify) out.push(Property.notify);
  if (p.indicate) out.push(Property.indicate);
  if (p.authenticatedSignedWrites) out.push(Property.authenticatedSignedWrites);
  return out;
};

export function makeAdapter() {
  return {
    state() {
      if (!bt()) return State.unsupported;
      return available === false ? State.off : State.on;
    },

    async watchState(signal, changed) {
      if (!bt()) {
        changed(State.unsupported);
        return;
      }
      available = await bt().getAvailability?.() ?? true;
      changed(available ? State.on : State.off);
      const on = (e) => {
        available = e.value;
        changed(e.value ? State.on : State.off);
      };
      bt().addEventListener("availabilitychanged", on);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      bt().removeEventListener("availabilitychanged", on);
    },

    async request() {
      return this.state();
    },

    async scan(signal, o, found) {
      if (!bt()?.requestLEScan) throw new BluetoothError("notAvailable");
      let scan;
      try {
        scan = await bt().requestLEScan({ acceptAllAdvertisements: o.services.length === 0,
          filters: o.services.length ? [{ services: o.services.map(full) }] : undefined, keepRepeatedDevices: o.duplicates });
      } catch (e) {
        throw failure(e);
      }
      const seen = new Set();
      const on = (e) => {
        devices.set(e.device.id, e.device);
        if (!o.duplicates && seen.has(e.device.id)) return;
        seen.add(e.device.id);
        const sd = {}, md = {};
        e.serviceData?.forEach((v, k) => { sd[short(k)] = bytesOf(v); });
        e.manufacturerData?.forEach((v, k) => { md[k.toString(16).padStart(4, "0")] = bytesOf(v); });
        found({ device: { id: e.device.id, name: e.device.name ?? "" }, rssi: e.rssi ?? 0, localName: e.name ?? "",
          services: (e.uuids ?? []).map(short), solicited: [], serviceData: sd, manufacturerData: md,
          txPower: e.txPower ?? 0, hasTxPower: e.txPower != null, connectable: true, time: Date.now() * 1e6 });
      };
      bt().addEventListener("advertisementreceived", on);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      bt().removeEventListener("advertisementreceived", on);
      scan.stop();
    },

    async choose(signal, filters, optionalServices) {
      if (!bt()) throw new BluetoothError("notAvailable");
      const fs = filters.map((f) => {
        const x = {};
        if (f.services.length) x.services = f.services.map(full);
        if (f.name) x.name = f.name;
        if (f.namePrefix) x.namePrefix = f.namePrefix;
        return x;
      });
      let d;
      try {
        d = await bt().requestDevice(fs.length ? { filters: fs, optionalServices: optionalServices.map(full) }
          : { acceptAllDevices: true, optionalServices: optionalServices.map(full) });
      } catch (e) {
        throw failure(e);
      }
      devices.set(d.id, d);
      return { id: d.id, name: d.name ?? "" };
    },

    async connect(signal, device, o, changed) {
      let d = devices.get(device.id);
      if (!d && bt()?.getDevices) {
        d = (await bt().getDevices()).find((x) => x.id === device.id);
        if (d) devices.set(d.id, d);
      }
      if (!d) throw new BluetoothError("notFound");
      const h = ++handles;
      const c = new Conn(d, changed);
      conns.set(h, c);
      changed({ state: ConnectionState.connecting, handle: 0, error: "" });
      let lost;
      const ended = new Promise((resolve) => { lost = resolve; });
      const gone = () => lost();
      d.addEventListener("gattserverdisconnected", gone);
      try {
        const connecting = d.gatt.connect();
        const t = o.timeout > 0 ? new Promise((_, reject) => setTimeout(() => reject(new BluetoothError("timeout")), o.timeout)) : null;
        await (t ? Promise.race([connecting, t]) : connecting);
        changed({ state: ConnectionState.connected, handle: h, error: "" });
        await Promise.race([ended, new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }))]);
        if (!signal.aborted) changed({ state: ConnectionState.disconnected, handle: 0, error: "bluetooth: disconnected" });
      } catch (e) {
        throw failure(e);
      } finally {
        d.removeEventListener("gattserverdisconnected", gone);
        conns.delete(h);
        if (![...conns.values()].some((x) => x.device === d)) d.gatt.disconnect();
      }
    },

    async services(signal, h) {
      const c = conn(h);
      const ss = await c.op(() => c.device.gatt.getPrimaryServices()).catch((e) => { throw failure(e); });
      const out = [];
      for (const s of ss) {
        const cs = await c.op(() => s.getCharacteristics()).catch(() => []);
        const chars = [];
        for (const ch of cs) {
          c.chars.set(`${short(s.uuid)} ${short(ch.uuid)}`, ch);
          const ds = await c.op(() => ch.getDescriptors()).catch(() => []);
          chars.push({ uuid: short(ch.uuid), properties: props(ch.properties), descriptors: ds.map((x) => short(x.uuid)) });
        }
        out.push({ uuid: short(s.uuid), characteristics: chars });
      }
      return out;
    },

    // The web doesn't say: the default ATT MTU.
    async mtu(signal, h) {
      conn(h);
      return 23;
    },

    async rssi() {
      throw new BluetoothError("unsupported");
    },

    async read(signal, h, service, char) {
      const c = conn(h);
      const ch = await c.char(service, char).catch((e) => { throw failure(e); });
      return bytesOf(await c.op(() => ch.readValue()).catch((e) => { throw failure(e); }));
    },

    async write(signal, h, service, char, value, response) {
      const c = conn(h);
      const ch = await c.char(service, char).catch((e) => { throw failure(e); });
      await c.op(() => (response ? ch.writeValueWithResponse(value) : ch.writeValueWithoutResponse(value))).catch((e) => { throw failure(e); });
    },

    async subscribe(signal, h, service, char, value) {
      const c = conn(h);
      const ch = await c.char(service, char).catch((e) => { throw failure(e); });
      const on = (e) => value(bytesOf(e.target.value));
      ch.addEventListener("characteristicvaluechanged", on);
      try {
        await c.op(() => ch.startNotifications()).catch((e) => { throw failure(e); });
        await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      } finally {
        ch.removeEventListener("characteristicvaluechanged", on);
        if (c.device.gatt?.connected) c.op(() => ch.stopNotifications()).catch(() => {});
      }
    },

    async serve() {
      throw new BluetoothError("notAvailable");
    },

    respond() {},

    notify() {
      return 0;
    },

    async advertise() {
      throw new BluetoothError("notAvailable");
    },

    // A page isn't started for Bluetooth, nor scans with it gone:
    // nothing is restored.
    async watchRestored() {},

    stopBackgroundScan() {},
  };
}
