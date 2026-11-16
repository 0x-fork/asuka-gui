// asuka/power on the web: the Battery Status API where the browser has it
// (Chromium). Chrome answers a computer without a battery as one
// charging, full, charged in no time and never discharging: no battery. A
// browser tells a page of no sleep, lock screen or idle time.
import { PowerError, Thermal } from "./power.asuka.js";

let manager; // a promise of the BatteryManager or null, once asked
let known = null; // the status last read
const battery = () => (manager ??= typeof navigator.getBattery === "function" ? navigator.getBattery().catch(() => null) : Promise.resolve(null));

function status(b) {
  const none = { battery: false, onBattery: false, level: -1, charging: false, lowPower: false, thermal: Thermal.nominal };
  if (!b || (b.charging && b.level === 1 && b.chargingTime === 0 && b.dischargingTime === Infinity)) return none;
  return { ...none, battery: true, onBattery: !b.charging, level: Math.min(Math.max(b.level, 0), 1), charging: b.charging && b.level < 1 };
}

export function makeMonitor() {
  battery().then((b) => { known ??= status(b); });
  return {
    current: () => known ?? status(null),
    async watch(signal, changed) {
      const b = await battery();
      let last = "";
      const report = () => {
        const s = status(b);
        known = s;
        const k = JSON.stringify(s);
        if (k !== last) {
          last = k;
          changed(s);
        }
      };
      report();
      const events = ["levelchange", "chargingchange", "chargingtimechange", "dischargingtimechange"];
      for (const e of events) b?.addEventListener?.(e, report);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      for (const e of events) b?.removeEventListener?.(e, report);
    },
    async events(signal, _happened) {
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
    },
    async idle(_signal) { throw new PowerError("idleUnknown"); },
  };
}
