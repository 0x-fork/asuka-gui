// Video: an AVPlayerView (AVKit's controls, floating) playing an AVPlayer.
// Loaded reports the video's natural size once AVFoundation knows it, Ended
// each play to its end (looping, it plays again), Failed what couldn't play.
import AppKit
import AVFoundation
import AVKit
#if canImport(AsukaOps)
import AsukaOps
#endif

/// A Video node's: its player, its item and what it was asked.
@MainActor final class AsukaVideo: NSObject {
    let view: AVPlayerView
    let player = AVPlayer()
    var item: AVPlayerItem?
    var autoPlay = false
    var loop = false
    var muted = false
    /// Loaded was reported for the item.
    var sized = false
    weak var node: AsukaNode?
    private var sizeObservation: NSKeyValueObservation?
    /// Times it played to its end (the automation's).
    var ends = 0

    init(_ n: AsukaNode) {
        view = AVPlayerView(frame: .zero)
        node = n
        super.init()
        view.controlsStyle = .floating
        view.player = player
    }

    func setProp(_ rt: AsukaRuntime, _ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        switch p {
        case AsukaProp.Video.source:
            setSource(rt, n, v.string)
        case AsukaProp.Video.mode:
            view.videoGravity = AsukaImageMode(rawValue: v.enumValue) == .fill ? .resizeAspectFill : .resizeAspect
        case AsukaProp.Video.hideControls:
            view.controlsStyle = v.bool ? .none : .floating
        case AsukaProp.Video.autoPlay:
            autoPlay = v.bool
            if autoPlay && item != nil { player.play() }
        case AsukaProp.Video.loop:
            loop = v.bool
        case AsukaProp.Video.muted:
            muted = v.bool
            player.isMuted = muted
        default:
            break
        }
    }

    func setSource(_ rt: AsukaRuntime, _ n: AsukaNode, _ s: String) {
        forgetItem()
        guard !s.isEmpty else {
            player.replaceCurrentItem(with: nil)
            return
        }
        let url: URL?
        if s.contains("://") {
            url = URL(string: s)
        } else if s.hasPrefix("/") {
            url = URL(fileURLWithPath: s)
        } else {
            url = rt.assets.appendingPathComponent(s).standardizedFileURL
        }
        guard let url else { return }
        let it = AVPlayerItem(url: url)
        item = it
        sized = false
        let c = NotificationCenter.default
        c.addObserver(self, selector: #selector(ended(_:)), name: .AVPlayerItemDidPlayToEndTime, object: it)
        c.addObserver(self, selector: #selector(failed(_:)), name: .AVPlayerItemFailedToPlayToEndTime, object: it)
        // KVO's change may come on another thread.
        sizeObservation = it.observe(\.presentationSize, options: [.initial, .new]) { [weak self] _, _ in
            DispatchQueue.main.async { MainActor.assumeIsolated { self?.reportSize() } }
        }
        // The item failing to load at all: its status.
        statusObservation = it.observe(\.status, options: [.new]) { [weak self] item, _ in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    guard let self, item === self.item, item.status == .failed else { return }
                    self.reportFailure(item.error)
                }
            }
        }
        player.replaceCurrentItem(with: it)
        player.isMuted = muted
        if autoPlay { player.play() }
    }

    private var statusObservation: NSKeyValueObservation?

    func forgetItem() {
        guard let it = item else { return }
        let c = NotificationCenter.default
        c.removeObserver(self, name: .AVPlayerItemDidPlayToEndTime, object: it)
        c.removeObserver(self, name: .AVPlayerItemFailedToPlayToEndTime, object: it)
        sizeObservation?.invalidate()
        statusObservation?.invalidate()
        sizeObservation = nil
        statusObservation = nil
        item = nil
    }

    func play() { player.play() }
    func pause() { player.pause() }
    func seek(_ sec: Double) { player.seek(to: CMTime(value: CMTimeValue(sec * 600), timescale: 600)) }

    func close() {
        player.pause()
        forgetItem()
        view.player = nil
    }

    /// Reports the video's natural size once AVFoundation knows it.
    func reportSize() {
        guard let it = item, !sized, let n = node else { return }
        let s = it.presentationSize
        guard s.width > 0, s.height > 0 else { return }
        sized = true
        AsukaRuntime.shared.emit(n, .loaded) { AsukaEvents.loaded($0, x: Double(s.width), y: Double(s.height)) }
    }

    @objc func ended(_ note: Notification) {
        guard let n = node, (note.object as AnyObject?) === item else { return }
        ends += 1
        AsukaRuntime.shared.emit(n, .ended) { AsukaEvents.ended($0) }
        if loop {
            seek(0)
            play()
        }
    }

    @objc func failed(_ note: Notification) {
        guard (note.object as AnyObject?) === item else { return }
        reportFailure((note.userInfo?[AVPlayerItemFailedToPlayToEndTimeErrorKey] as? Error) ?? item?.error)
    }

    func reportFailure(_ e: Error?) {
        guard let n = node else { return }
        let msg = e.map { asukaErrorText($0) } ?? "can't play"
        AsukaRuntime.shared.emit(n, .failed) { AsukaEvents.failed($0, msg) }
    }
}

@MainActor extension AsukaRuntime {
    func makeVideo(_ n: AsukaNode) -> NSView {
        let v = AsukaVideo(n)
        n.video = v
        return v.view
    }

    func setVideoProp(_ n: AsukaNode, _ p: UInt16, _ v: AsukaValue) {
        n.video?.setProp(self, n, p, v)
    }

    /// Play, Pause and Seek (f64 seconds).
    func videoCommand(_ n: AsukaNode, _ c: AsukaCommand, _ args: [UInt8]) {
        guard let v = n.video else { return }
        switch c {
        case .play: v.play()
        case .pause: v.pause()
        case .seek:
            if args.count >= 8 { v.seek(Double(bitPattern: args.u64(0))) }
        default: break
        }
    }

    func videoJSON(_ v: AsukaVideo, _ d: inout [String: Any]) {
        var j: [String: Any] = ["autoPlay": v.autoPlay, "loop": v.loop, "muted": v.player.isMuted, "sized": v.sized,
                                "rate": Double(v.player.rate), "controls": v.view.controlsStyle.rawValue,
                                "gravity": v.view.videoGravity.rawValue, "ends": v.ends,
                                "time": CMTimeGetSeconds(v.player.currentTime())]
        if let it = v.item {
            j["presentationSize"] = [Double(it.presentationSize.width), Double(it.presentationSize.height)]
            j["status"] = it.status.rawValue
        }
        d["video"] = j
    }
}
