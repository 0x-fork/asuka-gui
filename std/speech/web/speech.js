// asuka/speech on the web: the browser's speechSynthesis.
import { SpeechError, Quality } from "./speech.asuka.js";

function synth() {
  if (!globalThis.speechSynthesis) throw new globalThis.asuka.native.UnsupportedError("speech isn't in this browser");
  return speechSynthesis;
}

// The utterances waiting, kept (Chrome drops an utterance's events if
// it's collected).
const pending = new Map();

function voices() {
  return new Promise((resolve) => {
    const vs = synth().getVoices();
    if (vs.length) return resolve(vs);
    // Chrome loads them after the first ask.
    const t = setTimeout(() => resolve(synth().getVoices()), 1000);
    synth().addEventListener("voiceschanged", () => { clearTimeout(t); resolve(synth().getVoices()); }, { once: true });
  });
}

function stop() {
  const ends = [...pending.values()];
  pending.clear();
  globalThis.speechSynthesis?.cancel();
  for (const end of ends) end(new SpeechError("stopped"));
}

export function makeSynthesizer() {
  synth();
  return {
    async speak(signal, text, o) {
      const s = synth();
      const u = new SpeechSynthesisUtterance(text || "");
      const vs = o.voice || o.language ? await voices() : [];
      let voice = o.voice ? vs.find((v) => v.voiceURI === o.voice || v.name === o.voice) : null;
      if (!voice && o.language) {
        const lang = o.language.toLowerCase();
        voice = vs.find((v) => v.lang.toLowerCase() === lang) || vs.find((v) => v.lang.toLowerCase().startsWith(lang.split("-")[0]));
      }
      if (voice) u.voice = voice;
      u.lang = voice?.lang || o.language || "";
      u.pitch = Math.min(Math.max(o.pitch || 1, 0.5), 2);
      u.rate = Math.min(Math.max(o.rate || 1, 0.5), 2);
      u.volume = Math.min(Math.max(o.volume ?? 1, 0), 1);
      return new Promise((resolve, reject) => {
        const end = (err) => {
          if (!pending.has(u)) return;
          pending.delete(u);
          err ? reject(err) : resolve();
        };
        pending.set(u, end);
        u.onend = () => end(null);
        u.onerror = (e) => end(e.error === "canceled" || e.error === "interrupted" ? new SpeechError("stopped") : new Error(`speech: ${e.error}`));
        // The task that waits went: it stops speaking.
        signal.addEventListener("abort", stop, { once: true });
        s.speak(u);
      });
    },
    stop,
    pause() { synth().pause(); },
    resume() { synth().resume(); },
    speaking() {
      const s = globalThis.speechSynthesis;
      return Boolean(s && (s.speaking || s.paused || s.pending || pending.size));
    },
    async voices() {
      return (await voices()).map((v) => ({ id: v.voiceURI, name: v.name, language: v.lang, quality: Quality.standard, network: !v.localService }));
    },
  };
}
