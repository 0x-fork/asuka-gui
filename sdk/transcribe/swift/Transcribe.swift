// platform/transcribe on iOS and macOS: the Speech framework, on the device
// only: it never falls back to Apple's servers.
import Asuka
import Foundation
import Speech

final class SpeechRecognizer: Recognizer {
    private static func recognizer(_ language: String) -> SFSpeechRecognizer? {
        language.isEmpty ? SFSpeechRecognizer() : SFSpeechRecognizer(locale: Locale(identifier: language))
    }

    func check(language: String) async throws -> Availability {
        let r = Self.recognizer(language)
        return Availability(onDevice: r?.supportsOnDeviceRecognition ?? false, language: r?.locale.identifier ?? language)
    }

    func file(path: String, language: String) async throws -> Result {
        let status = await withCheckedContinuation { c in SFSpeechRecognizer.requestAuthorization { c.resume(returning: $0) } }
        guard status == .authorized else { throw TranscribeError.notAllowed }
        guard let rec = Self.recognizer(language), rec.supportsOnDeviceRecognition else { throw TranscribeError.notAvailable }
        let request = SFSpeechURLRecognitionRequest(url: AsukaAssets.url(path) ?? URL(fileURLWithPath: path))
        request.requiresOnDeviceRecognition = true
        request.addsPunctuation = true
        request.shouldReportPartialResults = false
        let locale = rec.locale.identifier
        let collect = Collect()
        let holder = TaskHolder()
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (c: CheckedContinuation<Result, Error>) in
                holder.task = rec.recognitionTask(with: request) { result, error in
                    if let result { collect.add(result.bestTranscription) }
                    guard error != nil || result?.isFinal == true else { return }
                    guard holder.finish() else { return }
                    let err = error as NSError?
                    // No speech in it (kAFAssistantErrorDomain 1110, or 203) is an empty text.
                    let silence = err?.domain == "kAFAssistantErrorDomain" && (err?.code == 1110 || err?.code == 203)
                    if let err, !silence, !collect.hasText {
                        // The recognizer can't start here (kLSRErrorDomain): the
                        // simulator says it can, then has no models; a phone may
                        // not have them yet.
                        c.resume(throwing: err.domain == "kLSRErrorDomain" ? TranscribeError.notAvailable : err)
                    } else {
                        c.resume(returning: collect.answer(locale: locale))
                    }
                }
            }
        } onCancel: {
            holder.task?.cancel()
        }
    }

    /// The recognition's task, and whether it answered.
    private final class TaskHolder: @unchecked Sendable {
        var task: SFSpeechRecognitionTask?
        private var done = false
        private let lock = NSLock()
        func finish() -> Bool { lock.withLock { if done { return false }; done = true; return true } }
    }

    /// What's been recognized. A long file can come in several results, each
    /// on from the last, or a result can replace the one before.
    private final class Collect: @unchecked Sendable {
        private var done: [SFTranscription] = []
        private let lock = NSLock()

        var hasText: Bool { lock.withLock { done.contains { !$0.formattedString.isEmpty } } }

        func add(_ t: SFTranscription) {
            lock.withLock {
                if let last = done.last, let lastSeg = last.segments.last, let first = t.segments.first,
                   first.timestamp >= lastSeg.timestamp + lastSeg.duration - 0.01 {
                    done.append(t)
                } else {
                    if !done.isEmpty { done.removeLast() }
                    done.append(t)
                }
            }
        }

        func answer(locale: String) -> Result {
            lock.withLock {
                Result(
                    text: done.map(\.formattedString).filter { !$0.isEmpty }.joined(separator: " "),
                    segments: done.flatMap(\.segments).map { Segment(text: $0.substring, start: $0.timestamp, duration: $0.duration) },
                    language: locale
                )
            }
        }
    }
}

func makeRecognizer() throws -> any Recognizer { SpeechRecognizer() }
