// platform/sensors on iOS: Core Motion. One CMMotionManager for the app (as
// Apple asks), each sensor run at the fastest interval a stream asks
// for, each stream given its samples no closer than its own. A Mac has
// none of these sensors that apps read: available is false there.
import Foundation
import Asuka
#if os(iOS)
import CoreMotion
#endif

final class CoreMotionSensors: Sensors {
    func available(sensor: Sensor) throws -> Bool {
        #if os(iOS)
        return MainActorRun { Hub.shared.available(sensor) }
        #else
        return false
        #endif
    }

    func watch(sensor: Sensor, every: TimeInterval, reading: @escaping (Sample) -> Void) async throws {
        #if os(iOS)
        guard await MainActor.run(body: { Hub.shared.available(sensor) }) else { throw SensorError.notAvailable }
        if sensor == .pedometer, CMPedometer.authorizationStatus() == .denied || CMPedometer.authorizationStatus() == .restricted {
            throw SensorError.notAllowed
        }
        let send = Send(reading)
        let id = await MainActor.run { Hub.shared.add(sensor, max(every, 0.005), send) }
        defer { Task { @MainActor in Hub.shared.remove(id) } }
        // Until the task is cancelled.
        while !Task.isCancelled {
            try? await Task.sleep(nanoseconds: 3_600_000_000_000)
        }
        #else
        throw SensorError.notAvailable
        #endif
    }

    func steps(from: Int, to: Int) async throws -> Steps {
        #if os(iOS)
        guard CMPedometer.isStepCountingAvailable() else { throw SensorError.notAvailable }
        let pedometer = CMPedometer()
        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<Steps, Error>) in
            pedometer.queryPedometerData(from: Date(timeIntervalSince1970: Double(from) / 1000), to: Date(timeIntervalSince1970: Double(to) / 1000)) { d, e in
                if let d {
                    c.resume(returning: Steps(steps: d.numberOfSteps.intValue, distance: d.distance?.doubleValue ?? -1))
                } else {
                    c.resume(throwing: Self.failure(e))
                }
            }
        }
        #else
        throw SensorError.notAvailable
        #endif
    }

    func simulate(json: String) async throws {
        #if os(iOS) && targetEnvironment(simulator)
        let o = (try? JSONSerialization.jsonObject(with: Data(json.utf8))) as? [String: Any] ?? [:]
        func v3(_ k: String) -> [Double]? { (o[k] as? [NSNumber]).map { $0.map(\.doubleValue) }.flatMap { $0.count == 3 ? $0 : nil } }
        let t = ProcessInfo.processInfo.systemUptime
        switch o["kind"] as? Int ?? -1 {
        case Sensor.accelerometer.rawValue:
            guard let a = v3("acceleration") else { throw AsukaNativeError.unsupported("simulate: no acceleration") }
            await MainActor.run { Hub.shared.add(.accelerometer, t, Hub.accelerometerValues(CMAcceleration(x: a[0], y: a[1], z: a[2]))) }
        case Sensor.motion.rawValue:
            guard let g = v3("gravity") else { throw AsukaNativeError.unsupported("simulate: no gravity") }
            let u = v3("user") ?? [0, 0, 0], r = v3("rotation") ?? [0, 0, 0]
            let f = v3("field").map { CMMagneticField(x: $0[0], y: $0[1], z: $0[2]) }
            await MainActor.run {
                Hub.shared.add(.motion, t, Hub.motionValues(gravity: CMAcceleration(x: g[0], y: g[1], z: g[2]), user: CMAcceleration(x: u[0], y: u[1], z: u[2]),
                                                            rotation: CMRotationRate(x: r[0], y: r[1], z: r[2]), field: f))
            }
        default:
            throw AsukaNativeError.unsupported("can't simulate that sensor")
        }
        #else
        throw AsukaNativeError.unsupported("only on the iOS simulator")
        #endif
    }

    #if os(iOS)
    static func failure(_ e: Error?) -> Error {
        if let e = e as NSError?, e.domain == CMErrorDomain, e.code == Int(CMErrorMotionActivityNotAuthorized.rawValue) {
            return SensorError.notAllowed
        }
        return e ?? SensorError.notAvailable
    }
    #endif
}

/// A stream's reading callback, called on the main thread.
final class Send: @unchecked Sendable {
    let f: (Sample) -> Void
    init(_ f: @escaping (Sample) -> Void) { self.f = f }
}

/// Runs body on the main thread and gives its result (sync methods run on
/// the thread that calls them).
func MainActorRun<T>(_ body: @MainActor () -> T) -> T {
    if Thread.isMainThread { return MainActor.assumeIsolated(body) }
    return DispatchQueue.main.sync { MainActor.assumeIsolated(body) }
}

#if os(iOS)
/// The sensors, as the streams read them. Main thread.
@MainActor final class Hub {
    static let shared = Hub()
    private let motion = CMMotionManager()
    private let altimeter = CMAltimeter()
    private var pedometer: CMPedometer?

    struct Watcher {
        let sensor: Sensor
        let every: TimeInterval
        var last: TimeInterval = -1
        let send: Send
    }

    private var watchers: [Int: Watcher] = [:]
    private var next = 0

    func available(_ s: Sensor) -> Bool {
        switch s {
        case .accelerometer: return motion.isAccelerometerAvailable
        case .gyroscope: return motion.isGyroAvailable
        case .magnetometer: return motion.isDeviceMotionAvailable && CMMotionManager.availableAttitudeReferenceFrames().contains(.xArbitraryCorrectedZVertical)
        case .motion: return motion.isDeviceMotionAvailable
        case .barometer: return CMAltimeter.isRelativeAltitudeAvailable()
        case .pedometer: return CMPedometer.isStepCountingAvailable()
        default: return false // light: no sensor apps can read
        }
    }

    func add(_ s: Sensor, _ every: TimeInterval, _ send: Send) -> Int {
        next += 1
        watchers[next] = Watcher(sensor: s, every: every, send: send)
        restart(s)
        return next
    }

    func remove(_ id: Int) {
        guard let w = watchers.removeValue(forKey: id) else { return }
        restart(w.sensor)
    }

    /// The fastest interval the streams of s ask for, nil when none reads it.
    private func interval(_ s: Sensor) -> TimeInterval? {
        watchers.values.filter { $0.sensor == s }.map(\.every).min()
    }

    /// A sample of s, to each stream of it that's due one.
    func add(_ s: Sensor, _ t: TimeInterval, _ v: [Double]) {
        for (id, w) in watchers where w.sensor == s {
            if w.last >= 0, t - w.last < w.every * 0.9 { continue }
            watchers[id]?.last = t
            w.send.f(Sample(values: v, time: t))
        }
    }

    // Core Motion's readings in the package's terms: accelerations negated
    // (Core Motion's read gravity's way, -1 g on z face up; the
    // accelerometer's, Android's and the W3C's read +1), rotation as is;
    // device motion as gravity, user acceleration, rotation and, if
    // calibrated, the magnetic field (µT).
    nonisolated static func accelerometerValues(_ a: CMAcceleration) -> [Double] { [-a.x, -a.y, -a.z] }

    nonisolated static func motionValues(gravity g: CMAcceleration, user u: CMAcceleration, rotation r: CMRotationRate, field f: CMMagneticField?) -> [Double] {
        var v = [-g.x, -g.y, -g.z, -u.x, -u.y, -u.z, r.x, r.y, r.z]
        if let f { v += [f.x, f.y, f.z] }
        return v
    }

    /// Starts s at the fastest interval asked, or stops it when nothing
    /// reads it.
    private func restart(_ s: Sensor) {
        let every = interval(s)
        switch s {
        case .accelerometer:
            guard let every else { motion.stopAccelerometerUpdates(); return }
            motion.accelerometerUpdateInterval = every
            if !motion.isAccelerometerActive {
                motion.startAccelerometerUpdates(to: .main) { [weak self] d, _ in
                    guard let d else { return }
                    self?.add(.accelerometer, d.timestamp, Self.accelerometerValues(d.acceleration))
                }
            }
        case .gyroscope:
            guard let every else { motion.stopGyroUpdates(); return }
            motion.gyroUpdateInterval = every
            if !motion.isGyroActive {
                motion.startGyroUpdates(to: .main) { [weak self] d, _ in
                    guard let r = d?.rotationRate, let t = d?.timestamp else { return }
                    self?.add(.gyroscope, t, [r.x, r.y, r.z])
                }
            }
        case .magnetometer, .motion:
            restartDeviceMotion()
        case .barometer:
            altimeter.stopRelativeAltitudeUpdates()
            guard every != nil else { return }
            altimeter.startRelativeAltitudeUpdates(to: .main) { [weak self] d, _ in
                guard let d else { return }
                self?.add(.barometer, d.timestamp, [d.pressure.doubleValue * 10, d.relativeAltitude.doubleValue])
            }
        case .pedometer:
            pedometer?.stopUpdates()
            pedometer = nil
            guard every != nil else { return }
            let p = CMPedometer()
            pedometer = p
            p.startUpdates(from: Date()) { d, _ in
                guard let d else { return }
                let steps = d.numberOfSteps.doubleValue, meters = d.distance?.doubleValue ?? -1
                DispatchQueue.main.async {
                    Hub.shared.add(.pedometer, ProcessInfo.processInfo.systemUptime, [steps, meters])
                }
            }
        default: break
        }
    }

    /// Device motion feeds both the magnetometer and motion: one stream, at
    /// the faster's interval, corrected by the magnetometer where there's
    /// one.
    private func restartDeviceMotion() {
        let mag = interval(.magnetometer), fused = interval(.motion)
        motion.stopDeviceMotionUpdates()
        guard let every = [mag, fused].compactMap({ $0 }).min() else { return }
        motion.deviceMotionUpdateInterval = every
        let frame: CMAttitudeReferenceFrame = CMMotionManager.availableAttitudeReferenceFrames().contains(.xArbitraryCorrectedZVertical)
            ? .xArbitraryCorrectedZVertical : .xArbitraryZVertical
        motion.startDeviceMotionUpdates(using: frame, to: .main) { [weak self] d, _ in
            guard let self, let d else { return }
            let f = d.magneticField
            let calibrated = f.accuracy != .uncalibrated
            if calibrated {
                self.add(.magnetometer, d.timestamp, [f.field.x, f.field.y, f.field.z])
            }
            self.add(.motion, d.timestamp, Self.motionValues(gravity: d.gravity, user: d.userAcceleration, rotation: d.rotationRate,
                                                              field: calibrated ? f.field : nil))
        }
    }
}
#endif

func makeSensors() throws -> any Sensors { CoreMotionSensors() }
