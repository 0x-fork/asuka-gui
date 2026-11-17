// platform/hotkey on a Mac: Carbon's hot keys (RegisterEventHotKey), which
// need no Accessibility permission, registered exclusively: another app's
// exclusive registration of the same keys fails one ("taken"), as do the
// system's own shortcuts (Spotlight's ⌘Space, Mission Control's), which
// CopySymbolicHotKeys lists. Presses come through the application's event
// target as AppKit's event loop runs. A hot key is the process's, not a
// window's: the registrations are the package's.
import AppKit
import Carbon.HIToolbox

@MainActor final class AsukaHotkeys {
    static let shared = AsukaHotkeys()

    /// EventHotKeyID's signature: the app's hot keys ('Asua'); another's
    /// aren't ours.
    static let signature: OSType = 0x4173_7561

    /// The registrations, by the package's ids.
    var refs: [UInt32: EventHotKeyRef] = [:]
    /// The presses "next" hasn't answered with yet, u32 ids.
    var pressed: [UInt8] = []
    var waiting: ((Result<Data, Error>) -> Void)?
    var handler: EventHandlerRef?

    /// "register" (u32 id, u8 mods, u32 key length, the key, the
    /// description, which a Mac doesn't show), "unregister" (u32 id),
    /// "next" (answered with the ids pressed since, u32 each, once there
    /// are some) and "stop" (fails the waiting "next" with "stopped").
    func methods() -> [String: AsukaModuleMethod] {
        [
            "register": { [unowned self] args, reply in
                var r = AsukaByteReader(args)
                let id = r.u32(), mods = r.u8()
                let key = r.string()
                if r.bad { return reply(.failure(AsukaModuleError("hotkey.register: bad arguments"))) }
                reply(self.register(id, key, mods))
            },
            "unregister": { [unowned self] args, reply in
                var r = AsukaByteReader(args)
                let id = r.u32()
                if !r.bad, let ref = self.refs.removeValue(forKey: id) { UnregisterEventHotKey(ref) }
                reply(.success(Data()))
            },
            "next": { [unowned self] _, reply in
                self.waiting?(.failure(AsukaModuleError("stopped"))) // one poll at a time
                self.waiting = reply
                self.flush()
            },
            "stop": { [unowned self] _, reply in
                let w = self.waiting
                self.waiting = nil
                self.pressed = []
                w?(.failure(AsukaModuleError("stopped")))
                reply(.success(Data()))
            },
        ]
    }

    func register(_ id: UInt32, _ key: String, _ mods: UInt8) -> Result<Data, Error> {
        guard let (code, cmods) = Self.carbonHotKey(key, mods) else { return .failure(AsukaModuleError("no key")) }
        if Self.systemHotKey(code, cmods) { return .failure(AsukaModuleError("taken")) }
        guard install() else { return .failure(AsukaModuleError("not available")) }
        if let old = refs.removeValue(forKey: id) { UnregisterEventHotKey(old) }
        var ref: EventHotKeyRef?
        let st = RegisterEventHotKey(code, cmods, EventHotKeyID(signature: Self.signature, id: id),
                                     GetApplicationEventTarget(), OptionBits(kEventHotKeyExclusive), &ref)
        if st == OSStatus(eventHotKeyExistsErr) { return .failure(AsukaModuleError("taken")) }
        guard st == noErr, let ref else { return .failure(AsukaModuleError("hotkey: RegisterEventHotKey failed (\(st))")) }
        refs[id] = ref
        return .success(Data())
    }

    /// The handler of the hot keys' events, on the application's target.
    func install() -> Bool {
        if handler != nil { return true }
        var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        let st = InstallEventHandler(GetApplicationEventTarget(), { _, event, _ -> OSStatus in
            var hk = EventHotKeyID()
            let st = GetEventParameter(event, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID), nil,
                                       MemoryLayout<EventHotKeyID>.size, nil, &hk)
            guard st == noErr, hk.signature == AsukaHotkeys.signature else { return OSStatus(eventNotHandledErr) }
            let id = hk.id
            MainActor.assumeIsolated { AsukaHotkeys.shared.pressed(id) }
            return noErr
        }, 1, &spec, nil, &handler)
        return st == noErr
    }

    /// Carbon's hot key event for id.
    func pressed(_ id: UInt32) {
        guard refs[id] != nil else { return }
        withUnsafeBytes(of: id.littleEndian) { pressed.append(contentsOf: $0) }
        flush()
    }

    /// Answers the waiting "next" with the presses.
    func flush() {
        guard let w = waiting, !pressed.isEmpty else { return }
        let b = pressed
        waiting = nil
        pressed = []
        w(.success(Data(b)))
    }

    /// Sends Carbon's hot key event for id to the application, as the
    /// system does when its keys are pressed (the automation's hotkey step).
    func sendEvent(_ id: UInt32) -> String? {
        var ev: EventRef?
        guard CreateEvent(nil, OSType(kEventClassKeyboard), UInt32(kEventHotKeyPressed), 0, EventAttributes(kEventAttributeNone), &ev) == noErr,
              let ev else { return "CreateEvent failed" }
        defer { ReleaseEvent(ev) }
        var hk = EventHotKeyID(signature: Self.signature, id: id)
        SetEventParameter(ev, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID), MemoryLayout<EventHotKeyID>.size, &hk)
        if SendEventToEventTarget(ev, GetApplicationEventTarget()) != noErr { return "SendEventToEventTarget failed" }
        return nil
    }

    /// The registrations for the automation's JSON.
    func json() -> [UInt32] { refs.keys.sorted() }

    /// The named keys' key codes (Events.h's kVK_), the same on every
    /// layout.
    static let named: [String: Int] = [
        "enter": 0x24, "tab": 0x30, "space": 0x31, "backspace": 0x33, "escape": 0x35,
        "delete": 0x75, "home": 0x73, "end": 0x77, "pageup": 0x74, "pagedown": 0x79,
        "left": 0x7B, "right": 0x7C, "down": 0x7D, "up": 0x7E,
        "f1": 0x7A, "f2": 0x78, "f3": 0x63, "f4": 0x76, "f5": 0x60, "f6": 0x61, "f7": 0x62, "f8": 0x64,
        "f9": 0x65, "f10": 0x6D, "f11": 0x67, "f12": 0x6F, "f13": 0x69, "f14": 0x6B, "f15": 0x71,
        "f16": 0x6A, "f17": 0x40, "f18": 0x4F, "f19": 0x50, "f20": 0x5A,
    ]

    /// A shortcut's key code and Carbon modifiers: a named key's, or the
    /// key that types the character on the keyboard's layout (with Shift if
    /// it takes it, as + on a US keyboard). Primary is ⌘.
    static func carbonHotKey(_ key: String, _ mods: UInt8) -> (UInt32, UInt32)? {
        let k = key.lowercased()
        var code: UInt32
        var cmods: UInt32 = 0
        if let c = named[k] {
            code = UInt32(c)
        } else if k.unicodeScalars.count == 1, let u = k.utf16.first, k.utf16.count == 1 {
            guard let (c, shift) = keyCode(for: u) else { return nil }
            code = UInt32(c)
            if shift { cmods |= UInt32(shiftKey) }
        } else {
            return nil
        }
        if mods & 1 != 0 { cmods |= UInt32(cmdKey) }
        if mods & 2 != 0 { cmods |= UInt32(shiftKey) }
        if mods & 4 != 0 { cmods |= UInt32(optionKey) }
        if mods & 8 != 0 { cmods |= UInt32(controlKey) }
        return (code, cmods)
    }

    /// The key code of the key that types ch on the current keyboard
    /// layout, or its ASCII-capable one (a Russian layout's Latin one), as
    /// menus' key equivalents are read: the key labelled K on an AZERTY
    /// keyboard for "k". shift: the key types ch only with Shift. The main
    /// keys come before the keypad's.
    static func keyCode(for ch: UInt16) -> (UInt16, Bool)? {
        var raw: UnsafeMutableRawPointer?
        var src: TISInputSource?
        for s in [TISCopyCurrentKeyboardLayoutInputSource(), TISCopyCurrentASCIICapableKeyboardLayoutInputSource()] {
            guard let x = s?.takeRetainedValue() else { continue }
            if let p = TISGetInputSourceProperty(x, kTISPropertyUnicodeKeyLayoutData) {
                raw = p
                src = x
                break
            }
        }
        guard let raw, src != nil else { return nil }
        let data = Unmanaged<CFData>.fromOpaque(raw).takeUnretainedValue()
        guard let bytes = CFDataGetBytePtr(data) else { return nil }
        let layout = UnsafeRawPointer(bytes).assumingMemoryBound(to: UCKeyboardLayout.self)
        let kbd = UInt32(LMGetKbdType())
        func keypad(_ c: UInt16) -> Bool { c >= 0x41 && c <= 0x5C && c != 0x48 && c != 0x49 && c != 0x4A && c != 0x50 && c != 0x5A }
        // UCKeyTranslate's modifiers: Carbon's, shifted down 8.
        for (state, pad) in [(UInt32(0), false), (UInt32(shiftKey >> 8), false), (UInt32(0), true)] {
            for c in UInt16(0)..<128 where keypad(c) == pad {
                var dead: UInt32 = 0
                var n = 0
                var buf = [UniChar](repeating: 0, count: 4)
                let st = UCKeyTranslate(layout, c, UInt16(kUCKeyActionDisplay), state, kbd, OptionBits(kUCKeyTranslateNoDeadKeysBit), &dead, 4, &n, &buf)
                if st == noErr && n == 1 && buf[0] == ch { return (c, state != 0) }
            }
        }
        return nil
    }

    /// Whether the system's own shortcuts (System Settings › Keyboard ›
    /// Keyboard Shortcuts: Spotlight's, Mission Control's, the input
    /// sources') have a key code with Carbon modifiers, enabled.
    static func systemHotKey(_ code: UInt32, _ mods: UInt32) -> Bool {
        var arr: Unmanaged<CFArray>?
        guard CopySymbolicHotKeys(&arr) == noErr, let list = arr?.takeRetainedValue() as? [[String: Any]] else { return false }
        let relevant = UInt32(cmdKey | shiftKey | optionKey | controlKey)
        for d in list {
            guard let c = (d[kHISymbolicHotKeyCode as String] as? NSNumber)?.uint32Value,
                  let m = (d[kHISymbolicHotKeyModifiers as String] as? NSNumber)?.uint32Value,
                  (d[kHISymbolicHotKeyEnabled as String] as? NSNumber)?.boolValue == true else { continue }
            if c == code && m & relevant == mods & relevant { return true }
        }
        return false
    }
}
