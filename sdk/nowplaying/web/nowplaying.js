// platform/nowplaying on the web: the page's Media Session
// (navigator.mediaSession), which the browser's media controls, the media
// keys and a phone's lock screen show and drive while the page plays. The
// session has no toggle: the play/pause key sends play or pause as
// playbackState says.
import { State, Command } from "./nowplaying.asuka.js";

// The Media Session's actions, by the command each sends.
const actions = [
  ["play", Command.play], ["pause", Command.pause], ["stop", Command.stop], ["nexttrack", Command.next],
  ["previoustrack", Command.previous], ["seekto", Command.seek], ["seekforward", Command.skipForward],
  ["seekbackward", Command.skipBackward],
];

const quiet = (f) => { try { f(); } catch { /* an action or a state the browser doesn't take */ } };

export function makeCenter() {
  let info = null;
  const watchers = new Set();
  const enabled = (c) => info != null && (info.commands & (1 << c)) !== 0;
  const command = (c, v) => {
    if (!enabled(c)) return;
    for (const w of [...watchers]) w(c, v);
  };
  const handlers = new Map(actions.map(([name, c]) => [name, (d) => {
    if (c === Command.seek) command(c, Number(d?.seekTime) || 0);
    else if (c === Command.skipForward || c === Command.skipBackward) command(c, Number(d?.seekOffset) || info?.skip || 0);
    else command(c, 0);
  }]));
  const session = () => globalThis.navigator?.mediaSession;
  const setHandlers = (s) => {
    for (const [name, c] of actions) quiet(() => s.setActionHandler(name, enabled(c) ? handlers.get(name) : null));
  };
  return {
    set(i) {
      info = i;
      const s = session();
      if (!s) return;
      const meta = { title: i.title || "", artist: i.artist || "", album: i.album || "" };
      if (i.artwork) meta.artwork = [{ src: globalThis.asuka.native.assetURL(i.artwork) }];
      quiet(() => { s.metadata = typeof MediaMetadata === "function" ? new MediaMetadata(meta) : meta; });
      quiet(() => { s.playbackState = i.state === State.stopped ? "none" : i.state === State.playing ? "playing" : "paused"; });
      setHandlers(s);
      if (typeof s.setPositionState === "function") {
        quiet(() => {
          if (i.duration > 0) {
            s.setPositionState({ duration: i.duration, position: Math.min(Math.max(i.position, 0), i.duration), playbackRate: i.rate > 0 ? i.rate : 1 });
          } else {
            s.setPositionState();
          }
        });
      }
    },
    clear() {
      info = null;
      const s = session();
      if (!s) return;
      quiet(() => { s.metadata = null; });
      quiet(() => { s.playbackState = "none"; });
      setHandlers(s);
      if (typeof s.setPositionState === "function") quiet(() => s.setPositionState());
    },
    async watch(signal, command) {
      watchers.add(command);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      watchers.delete(command);
    },
  };
}
