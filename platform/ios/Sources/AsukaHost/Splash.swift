#if canImport(UIKit)
import UIKit

/// asuka.json's splash: the launch screen shows until the first frame,
/// which would be the app's; a copy of it (the asset catalog's image and
/// color, as UILaunchScreen lays them out) stays over the app until the
/// core's first flush is in, or, if the app holds it (splash.Hold), until
/// "hide".
@MainActor public final class AsukaSplash {
    public static let shared = AsukaSplash()
    private var cover: UIView?
    private var held = false
    private var ready = false

    /// Covers the controller's view, if the app has a splash.
    func show(over controller: UIViewController) {
        guard Bundle.main.object(forInfoDictionaryKey: "AsukaSplash") as? Bool == true, cover == nil else { return }
        let v = UIView(frame: controller.view.bounds)
        v.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        v.backgroundColor = UIColor(named: "AsukaSplashBackground") ?? .systemBackground
        if let img = UIImage(named: "AsukaSplash") {
            let iv = UIImageView(image: img)
            iv.center = CGPoint(x: v.bounds.midX, y: v.bounds.midY)
            iv.autoresizingMask = [.flexibleLeftMargin, .flexibleRightMargin, .flexibleTopMargin, .flexibleBottomMargin]
            v.addSubview(iv)
        }
        controller.view.addSubview(v)
        cover = v
    }

    /// The core started (its first flush applied): the app's frame is ready.
    func started() {
        ready = true
        if let c = cover { c.superview?.bringSubviewToFront(c) }
        if !held { hide() }
    }

    /// Keeps the splash up after the first frame, until release: asked
    /// before the first frame (asuka/splash).
    public func hold() {
        if !ready { held = true }
    }

    /// Takes the splash away (a short fade), now or once the first frame
    /// is in.
    public func release() {
        held = false
        if ready { hide() }
    }

    func methods() -> [String: AsukaModuleMethod] {
        [
            "hold": { [unowned self] _, reply in
                self.hold()
                reply(.success(Data()))
            },
            "hide": { [unowned self] _, reply in
                self.release()
                reply(.success(Data()))
            },
        ]
    }

    private func hide() {
        guard let c = cover else { return }
        cover = nil
        UIView.animate(withDuration: 0.2, animations: { c.alpha = 0 }) { _ in c.removeFromSuperview() }
    }
}
#endif
