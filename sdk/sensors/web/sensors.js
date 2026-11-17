// platform/sensors in the browser: the Generic Sensor API where the browser
// has it (Accelerometer, Gyroscope, Magnetometer,
// AbsoluteOrientationSensor, AmbientLightSensor: Chromium's), else the
// DeviceMotion and DeviceOrientation events (Safari asks the user, on a
// tap). Accelerations in g, rotation rates in radians per second, the
// field in microteslas, device motion's attitude as a quaternion. A page
// can't read a barometer or a pedometer.
import { Sensor, SensorError } from "./sensors.asuka.js";

const G = 9.80665;
const deg = Math.PI / 180;

const generic = (name) => (typeof globalThis[name] === "function" ? globalThis[name] : null);
const motionEvents = () => typeof DeviceMotionEvent !== "undefined" && (navigator.maxTouchPoints || 0) > 0;
// Safari asks for motion and orientation (on a tap).
const askable = () => typeof DeviceMotionEvent !== "undefined" && typeof DeviceMotionEvent.requestPermission === "function";

/**
 * quaternion is the device-to-Earth (East, North, Up) rotation of a
 * DeviceOrientation's alpha, beta and gamma (degrees, Z-X'-Y''), as x, y,
 * z, w: the W3C spec's.
 */
export function quaternion(alpha, beta, gamma) {
  const x = ((beta || 0) * deg) / 2, y = ((gamma || 0) * deg) / 2, z = ((alpha || 0) * deg) / 2;
  const cX = Math.cos(x), cY = Math.cos(y), cZ = Math.cos(z);
  const sX = Math.sin(x), sY = Math.sin(y), sZ = Math.sin(z);
  return [sX * cY * cZ - cX * sY * sZ, cX * sY * cZ + sX * cY * sZ, cX * cY * sZ + sX * sY * cZ, cX * cY * cZ - sX * sY * sZ];
}

/** motionValues is device motion's 9 values (gravity and the user's acceleration in g, the rotation rate in rad/s) from a DeviceMotionEvent. */
export function motionValues(e) {
  const ag = e.accelerationIncludingGravity || {}, a = e.acceleration || {}, r = e.rotationRate || {};
  const lin = [a.x ?? 0, a.y ?? 0, a.z ?? 0];
  const grav = [(ag.x ?? 0) - lin[0], (ag.y ?? 0) - lin[1], (ag.z ?? 0) - lin[2]];
  // rotationRate's alpha, beta, gamma are about x, y, z (as browsers give them).
  return [...grav.map((v) => v / G), ...lin.map((v) => v / G), (r.alpha ?? 0) * deg, (r.beta ?? 0) * deg, (r.gamma ?? 0) * deg];
}

/** probeGeneric starts a Generic Sensor briefly: whether it reads. */
function probeGeneric(Cls) {
  return new Promise((resolve) => {
    let s;
    try { s = new Cls({ frequency: 10 }); } catch { resolve(false); return; }
    const done = (ok) => { clearTimeout(t); try { s.stop(); } catch {} resolve(ok); };
    const t = setTimeout(() => done(false), 1500);
    s.addEventListener("reading", () => done(true));
    s.addEventListener("error", () => done(false));
    try { s.start(); } catch { done(false); }
  });
}

async function allowMotion() {
  if (!askable()) return true;
  try {
    const [m, o] = await Promise.all([
      DeviceMotionEvent.requestPermission(),
      typeof DeviceOrientationEvent?.requestPermission === "function" ? DeviceOrientationEvent.requestPermission() : "granted",
    ]);
    return m === "granted" && o === "granted";
  } catch {
    return false;
  }
}

const genericName = { [Sensor.accelerometer]: "Accelerometer", [Sensor.gyroscope]: "Gyroscope", [Sensor.magnetometer]: "Magnetometer", [Sensor.light]: "AmbientLightSensor" };

const probed = new Map(); // sensor -> Promise of "generic", "events" or false

/** source finds where a sensor's readings come from. */
function source(s) {
  if (!probed.has(s)) {
    probed.set(s, (async () => {
      const name = genericName[s];
      if (name && generic(name) && (await probeGeneric(generic(name)))) return "generic";
      if ((s === Sensor.accelerometer || s === Sensor.gyroscope || s === Sensor.motion) && motionEvents()) return "events";
      return false;
    })());
  }
  return probed.get(s);
}

let orientation = null; // Promise: whether AbsoluteOrientationSensor reads

function listen(name, f) {
  addEventListener(name, f);
  return () => removeEventListener(name, f);
}

function genericSensor(Cls, hz, read) {
  const s = new Cls({ frequency: hz });
  s.addEventListener("reading", () => read(s));
  s.addEventListener("error", (e) => console.warn("asuka: sensor:", e.error?.message || e));
  s.start();
  return () => s.stop();
}

export function makeSensors() {
  return {
    // Sync, so not probed: what the browser has the API for (watch finds
    // whether it reads).
    available(s) {
      const name = genericName[s];
      if (name && generic(name)) return true;
      return (s === Sensor.accelerometer || s === Sensor.gyroscope || s === Sensor.motion) && motionEvents();
    },

    async watch(signal, s, every, reading) {
      const from = await source(s);
      if (!from) throw new SensorError("notAvailable");
      if (from === "events" && !(await allowMotion())) throw new SensorError("notAllowed");
      const hz = Math.min(60, Math.max(1, 1000 / Math.max(every, 5)));
      let last = -1;
      const send = (t, values) => {
        if (last >= 0 && t - last < every * 0.9) return;
        last = t;
        reading({ values, time: t });
      };
      const stops = [];
      switch (s) {
        case Sensor.accelerometer:
          if (from === "generic") stops.push(genericSensor(generic("Accelerometer"), hz, (x) => send(x.timestamp, [x.x / G, x.y / G, x.z / G])));
          else stops.push(listen("devicemotion", (e) => {
            const a = e.accelerationIncludingGravity;
            if (a?.x != null) send(e.timeStamp, [a.x / G, a.y / G, a.z / G]);
          }));
          break;
        case Sensor.gyroscope:
          if (from === "generic") stops.push(genericSensor(generic("Gyroscope"), hz, (x) => send(x.timestamp, [x.x, x.y, x.z])));
          else stops.push(listen("devicemotion", (e) => {
            const r = e.rotationRate;
            if (r?.alpha != null) send(e.timeStamp, [r.alpha * deg, r.beta * deg, r.gamma * deg]); // about x, y, z
          }));
          break;
        case Sensor.magnetometer:
          stops.push(genericSensor(generic("Magnetometer"), hz, (x) => send(x.timestamp, [x.x, x.y, x.z])));
          break;
        case Sensor.light:
          stops.push(genericSensor(generic("AmbientLightSensor"), hz, (x) => send(x.timestamp, [x.illuminance])));
          break;
        case Sensor.motion: {
          // The newest orientation, sent with each motion: a quaternion where
          // it's relative to north, else none (the tilt, without a heading).
          let q = null;
          const O = generic("AbsoluteOrientationSensor");
          orientation ??= O ? probeGeneric(O) : Promise.resolve(false);
          const sensor = await orientation;
          if (sensor) stops.push(genericSensor(O, hz, (x) => { q = Array.from(x.quaternion); }));
          else {
            stops.push(listen("ondeviceorientationabsolute" in globalThis ? "deviceorientationabsolute" : "deviceorientation", (e) => {
              let alpha = e.alpha;
              if (e.webkitCompassHeading != null) alpha = 360 - e.webkitCompassHeading; // Safari: north
              else if (!e.absolute) return;
              if (alpha != null) q = quaternion(alpha, e.beta, e.gamma);
            }));
          }
          stops.push(listen("devicemotion", (e) => {
            if (e.accelerationIncludingGravity?.x == null || (sensor && !q)) return; // the sensor's first reading to come
            const v = motionValues(e);
            send(e.timeStamp, q ? [...v, ...q] : v);
          }));
          break;
        }
        default:
          throw new SensorError("notAvailable");
      }
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      for (const f of stops) f();
    },

    async steps() {
      throw new SensorError("notAvailable");
    },

    async simulate() {
      throw new Error("unsupported: only on the iOS simulator");
    },
  };
}
