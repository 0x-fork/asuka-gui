// platform/network on the web: navigator.onLine, and the Network
// Information API where the browser has it (Chromium's: the kind of
// connection, and saveData). Browsers don't say whether a connection is
// metered: cellular is taken as metered.
import { ConnectionType } from "./network.asuka.js";

function status() {
  if (!navigator.onLine) return { connected: false, type: ConnectionType.none, metered: false, constrained: false };
  const c = navigator.connection;
  let type = ConnectionType.other;
  switch (c?.type) {
    case "wifi": type = ConnectionType.wifi; break;
    case "cellular": type = ConnectionType.cellular; break;
    case "ethernet": type = ConnectionType.ethernet; break;
  }
  return {
    connected: true,
    type,
    metered: type === ConnectionType.cellular,
    constrained: Boolean(c?.saveData),
  };
}

export function makeMonitor() {
  return {
    current: status,
    async watch(signal, changed) {
      let last = "";
      const report = () => {
        const s = status();
        const k = JSON.stringify(s);
        if (k !== last) {
          last = k;
          changed(s);
        }
      };
      report();
      window.addEventListener("online", report);
      window.addEventListener("offline", report);
      navigator.connection?.addEventListener("change", report);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      window.removeEventListener("online", report);
      window.removeEventListener("offline", report);
      navigator.connection?.removeEventListener("change", report);
    },
  };
}
