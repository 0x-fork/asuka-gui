// asuka/tracking on the web: browsers have no prompt; Global Privacy
// Control (and Do Not Track, where a browser still sends it) is the
// user's no.
import { Status } from "./tracking.asuka.js";

function current() {
  const no = navigator.globalPrivacyControl === true || navigator.doNotTrack === "1" || window.doNotTrack === "1";
  return no ? Status.denied : Status.allowed;
}

export function makeTracker() {
  return {
    current,
    async request(_signal) {
      return current();
    },
  };
}
