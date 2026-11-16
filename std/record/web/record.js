// asuka/record on the web: the microphone (getUserMedia) through a
// MediaRecorder: AAC in MPEG-4 (.m4a) where the browser records it
// (Safari, Chrome), else Opus in WebM (.webm, Firefox), written to the
// app's files. The level is an analyser's RMS, on iOS's meter's scale
// (silence below -60 dB). Video is asuka/camera's.
import { Quality, RecordError } from "./record.asuka.js";
import { Recording, mediaError, recordingType, uuid } from "./media.js";

let rec = null; // a Recording
let meter = null; // { ctx, analyser, data }

function level() {
  if (!rec?.recording || !meter) return 0;
  meter.analyser.getFloatTimeDomainData(meter.data);
  let sum = 0;
  for (const x of meter.data) sum += x * x;
  const rms = Math.sqrt(sum / meter.data.length);
  return rms > 0.001 ? Math.min(rms, 1) : 0;
}

export function makeRecorder() {
  return {
    async record(signal, quality, maxMs, path, background, title) {
      if (rec) throw new RecordError("busy");
      const music = quality === Quality.music;
      if (!navigator.mediaDevices?.getUserMedia || typeof MediaRecorder === "undefined") {
        throw new globalThis.asuka.native.UnsupportedError("recording isn't in this browser");
      }
      let stream;
      try {
        stream = await navigator.mediaDevices.getUserMedia({
          audio: music
            ? { channelCount: { ideal: 2 }, echoCancellation: false, noiseSuppression: false, autoGainControl: false }
            : { channelCount: { ideal: 1 }, echoCancellation: true, noiseSuppression: true, autoGainControl: true },
        });
      } catch (e) {
        const err = mediaError(e);
        throw err?.message === "not allowed" ? new RecordError("notAllowed") : err || new Error("no microphone");
      }
      const { type, ext } = recordingType((t) => MediaRecorder.isTypeSupported(t));
      const file = path || `/tmp/asuka-audio/${uuid()}.${ext}`;
      try {
        const ctx = new AudioContext();
        ctx.resume().catch(() => {});
        const analyser = ctx.createAnalyser();
        analyser.fftSize = 1024;
        ctx.createMediaStreamSource(stream).connect(analyser);
        meter = { ctx, analyser, data: new Float32Array(analyser.fftSize) };
      } catch {
        meter = null;
      }
      return new Promise((resolve, reject) => {
        rec = new Recording(stream, { type, path: file, bitrate: music ? 128000 : 64000, maxMs }, (err, p) => {
          stream.getTracks().forEach((t) => t.stop());
          meter?.ctx.close().catch(() => {});
          meter = null;
          rec = null;
          err ? reject(err) : resolve(p);
        });
        // The task that waits went: the recording ends.
        signal.addEventListener("abort", () => rec?.stop(), { once: true });
      });
    },
    stop() { rec?.stop(); },
    pause() { rec?.pause(); },
    resume() { rec?.resume(); },
    status() {
      return { level: level(), recording: Boolean(rec?.recording), elapsed: rec ? rec.ms() : 0 };
    },
  };
}
