// asuka/audio on the web: an audio element a sound.
const native = () => globalThis.asuka.native;

// The sounds playing once, kept until they end.
const playingOnce = new Set();

export function makeSound() {
  let el = null;
  let once = false, ended = false, rate = 1;
  const s = {
    open(source) {
      s.close();
      el = new Audio();
      el.preload = "auto";
      el.preservesPitch = true; // a voice keeps its pitch at any rate
      el.addEventListener("ended", () => {
        if (el?.loop) return;
        if (once) {
          s.close();
          playingOnce.delete(s);
        } else {
          ended = true;
        }
      });
      el.src = native().assetURL(source);
    },
    play() {
      if (!el) return;
      if (ended) { ended = false; el.currentTime = 0; }
      el.playbackRate = rate;
      el.play().catch((e) => console.warn("asuka: audio:", e.message));
    },
    playOnce() {
      once = true;
      playingOnce.add(s);
      if (el) el.currentTime = 0;
      s.play();
    },
    pause() { el?.pause(); },
    seek(seconds) { if (el) { ended = false; el.currentTime = seconds; } },
    setVolume(v) { if (el) el.volume = Math.min(Math.max(v, 0), 1); },
    setRate(r) { rate = r || 1; if (el) el.playbackRate = rate; },
    setLoop(on) { if (el) el.loop = on; },
    status() {
      if (!el) return { position: 0, duration: 0, playing: false, ended: false };
      const d = el.duration;
      return { position: el.currentTime || 0, duration: Number.isFinite(d) ? d : 0, playing: !el.paused && !el.ended, ended };
    },
    close() {
      if (!el) return;
      el.pause();
      el.removeAttribute("src");
      el.load();
      el = null;
      ended = false;
    },
    // Let go: it stops, unless it's playing once.
    released() { if (!playingOnce.has(s)) s.close(); },
  };
  return s;
}
