// Sime macOS Input Method — entry point.
// Mirrors Squirrel's Main.swift structure.

import AppKit
import InputMethodKit

// Global IMKServer — must stay alive for the lifetime of the process.
var server: IMKServer!

@main
struct SimeApp {
  static func main() {
    // Initialize NSApplication first so NSApp is non-nil.
    let app = NSApplication.shared

    // The connection name must match Info.plist InputMethodConnectionName.
    let connectionName = Bundle.main.infoDictionary?["InputMethodConnectionName"] as? String
      ?? "Sime_Connection"

    server = IMKServer(name: connectionName, bundleIdentifier: Bundle.main.bundleIdentifier)

    // Load assets from the app bundle.
    let bundle = Bundle.main
    let dictPath = bundle.path(forResource: "sime", ofType: "dict")
    let cntPath = bundle.path(forResource: "sime", ofType: "cnt")

    if let d = dictPath, let c = cntPath {
      SimeEngine.shared.load(dictPath: d, cntPath: c)
      SimeEngine.shared.setUserSentenceEnabled(SimeSettings.shared.userSentence)
    } else {
      NSLog("Sime: WARNING — sime.dict / sime.cnt not found in app bundle.")
    }

    app.run()
  }
}

// ---------------------------------------------------------------------------
// Singleton wrapper around the C engine handle.
// ---------------------------------------------------------------------------

final class SimeEngine {
  static let shared = SimeEngine()
  private var handle: OpaquePointer?  // SimeHandle*

  // User-history LM persistence. Mirrors fcitx5-sime's threshold flush
  // and Android SimeEngine: accumulate learns in memory, flush every
  // `userSentenceFlushThreshold` accepts and on lifecycle hooks
  // (deactivate / commitComposition).
  private var userSentencePath: String?
  private var pendingUserSentenceSaves = 0
  private static let userSentenceFlushThreshold = 8
  private static let userSentenceFilename = "sentences.txt"

  private init() {}

  func load(dictPath: String, cntPath: String) {
    if let h = handle { sime_destroy(h) }
    handle = sime_create(dictPath, cntPath)
    if sime_ready(handle) {
      NSLog("Sime: engine loaded (context size = %d)", sime_context_size(handle))
      loadUserSentence()
    } else {
      NSLog("Sime: ERROR — failed to load engine")
    }
  }

  var ready: Bool { sime_ready(handle) }
  var contextSize: Int { Int(sime_context_size(handle)) }

  func decodeSentence(_ pinyin: String, extra: Int = 0) -> [Candidate] {
    guard let h = handle else { return [] }
    var r = sime_decode_sentence(h, pinyin, Int32(extra))
    defer { sime_free_results(&r) }
    return toSwift(r)
  }

  func decodeStr(_ pinyin: String, num: Int = 9) -> [Candidate] {
    guard let h = handle else { return [] }
    var r = sime_decode_str(h, pinyin, Int32(num))
    defer { sime_free_results(&r) }
    return toSwift(r)
  }

  func nextTokens(_ tokens: [UInt32], num: Int = 9) -> [Candidate] {
    guard let h = handle else { return [] }
    var r = tokens.withUnsafeBufferPointer { buf in
      sime_next_tokens(h, buf.baseAddress, Int32(tokens.count), Int32(num))
    }
    defer { sime_free_results(&r) }
    return toSwift(r)
  }

  // MARK: - User sentence

  func setUserSentenceEnabled(_ enabled: Bool) {
    guard let h = handle else { return }
    sime_set_user_sentence_enabled(h, enabled)
  }

  func learnUserSentence(context: [UInt32], tokens: [UInt32]) {
    guard let h = handle, !tokens.isEmpty,
          SimeSettings.shared.userSentence else { return }
    tokens.withUnsafeBufferPointer { tBuf in
      context.withUnsafeBufferPointer { cBuf in
        sime_learn_user_sentence(h,
          cBuf.baseAddress, Int32(context.count),
          tBuf.baseAddress, Int32(tokens.count))
      }
    }
    pendingUserSentenceSaves += 1
    if pendingUserSentenceSaves >= SimeEngine.userSentenceFlushThreshold {
      flushUserSentence()
    }
  }

  func flushUserSentence() {
    guard let h = handle, pendingUserSentenceSaves > 0,
          let path = userSentencePath else { return }
    if !sime_save_user_sentence(h, path) {
      NSLog("Sime: failed to save user sentence history at %@", path)
    }
    pendingUserSentenceSaves = 0
  }

  private func loadUserSentence() {
    guard let h = handle else { return }
    let fm = FileManager.default
    guard let appSupport = fm.urls(for: .applicationSupportDirectory,
                                   in: .userDomainMask).first else { return }
    let dir = appSupport.appendingPathComponent("Sime", isDirectory: true)
    do {
      try fm.createDirectory(at: dir, withIntermediateDirectories: true)
    } catch {
      NSLog("Sime: cannot create data dir %@ (%@)",
            dir.path, String(describing: error))
      return
    }
    let path = dir.appendingPathComponent(SimeEngine.userSentenceFilename).path
    userSentencePath = path
    // Vocab-signature mismatch (LM regen) silently rejects the old file;
    // a fresh one is written on the next flush.
    _ = sime_load_user_sentence(h, path)
  }

  private func toSwift(_ r: SimeResults) -> [Candidate] {
    guard r.count > 0, let items = r.items else { return [] }
    return (0..<Int(r.count)).map { i in
      let item = items[i]
      let text = item.text.map { String(cString: $0) } ?? ""
      let units = item.units.map { String(cString: $0) } ?? ""
      let tokens: [UInt32] = item.token_count > 0 && item.tokens != nil
        ? (0..<Int(item.token_count)).map { item.tokens![$0] }
        : []
      return Candidate(text: text, units: units, tokens: tokens,
                       consumed: Int(item.consumed))
    }
  }
}

// ---------------------------------------------------------------------------
// Value type returned by the engine.
// ---------------------------------------------------------------------------

struct Candidate {
  let text: String
  let units: String    // e.g. "ni'hao"
  let tokens: [UInt32]
  let consumed: Int    // bytes of pinyin consumed
}
