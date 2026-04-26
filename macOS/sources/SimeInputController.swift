// Sime IMKInputController — handles key events and manages composition state.
// Mirrors the structure of Squirrel's SquirrelInputController and ports the
// state/key-handling logic from Linux/fcitx5/src/sime-ime.cc.

import InputMethodKit
import AppKit
import Carbon.HIToolbox

// ---------------------------------------------------------------------------
// Per-session composition state (port of SimeState from sime-state.h)
// ---------------------------------------------------------------------------

private struct Selection {
  let text: String
  let units: String
  let tokens: [UInt32]
  let consumed: Int  // bytes of pinyin consumed
}

private final class SimeState {
  var buffer: String = ""     // raw pinyin typed so far (ASCII only)
  var selections: [Selection] = []
  var contextIds: [UInt32] = []  // LM context across sessions
  var predicting: Bool = false

  // Paging
  var page: Int = 0
  var candidates: [Candidate] = []
  var highlightedIndex: Int = 0

  static let pageSize = 9

  var selectedLength: Int { selections.reduce(0) { $0 + $1.consumed } }

  var committedText: String { selections.map(\.text).joined() }

  var remaining: String {
    let sel = selectedLength
    guard sel < buffer.count else { return "" }
    return String(buffer.dropFirst(sel))
  }

  var isEmpty: Bool { buffer.isEmpty && !predicting }

  var fullySelected: Bool {
    !buffer.isEmpty && selectedLength >= buffer.count
  }

  var pageStart: Int { page * SimeState.pageSize }

  var pageCandidates: [Candidate] {
    let start = pageStart
    let end = min(start + SimeState.pageSize, candidates.count)
    guard start < end else { return [] }
    return Array(candidates[start..<end])
  }

  var hasNextPage: Bool {
    (page + 1) * SimeState.pageSize < candidates.count
  }

  func reset() {
    buffer = ""
    selections = []
    candidates = []
    highlightedIndex = 0
    page = 0
    predicting = false
  }

  func pushContext(_ text: String, _ tokens: [UInt32], maxSize: Int) {
    for t in tokens { contextIds.append(t) }
    // Trim context to maxSize (keep most recent)
    if contextIds.count > maxSize * 4 {
      contextIds.removeFirst(contextIds.count - maxSize * 4)
    }
  }

  func clearContext() {
    contextIds = []
    predicting = false
  }
}

// ---------------------------------------------------------------------------
// User-facing toggles persisted across sessions. Mirrors the fcitx5
// "fullwidth" + "punctuation" addons that sime-ime.cc registers in the
// status area.
// ---------------------------------------------------------------------------

final class SimeSettings {
  static let shared = SimeSettings()
  private let defaults = UserDefaults.standard
  private enum Key {
    static let fullwidthPunctuation = "SimeFullwidthPunctuation"
    static let fullwidthCharacter   = "SimeFullwidthCharacter"
    static let prediction           = "SimePrediction"
  }
  // Defaults: Chinese punctuation ON, fullwidth characters OFF, prediction ON.
  var fullwidthPunctuation: Bool {
    get { defaults.object(forKey: Key.fullwidthPunctuation) as? Bool ?? true }
    set { defaults.set(newValue, forKey: Key.fullwidthPunctuation) }
  }
  var fullwidthCharacter: Bool {
    get { defaults.object(forKey: Key.fullwidthCharacter) as? Bool ?? false }
    set { defaults.set(newValue, forKey: Key.fullwidthCharacter) }
  }
  var prediction: Bool {
    get { defaults.object(forKey: Key.prediction) as? Bool ?? true }
    set { defaults.set(newValue, forKey: Key.prediction) }
  }
}

// ---------------------------------------------------------------------------
// IMKInputController subclass
// ---------------------------------------------------------------------------

final class SimeInputController: IMKInputController {
  private weak var currentClient: IMKTextInput?
  private let state = SimeState()

  // Curly-quote toggle state (true = next press emits the opening glyph).
  private var doubleQuoteOpen = true
  private var singleQuoteOpen = true

  // English mode (toggled by tapping the left Shift key alone).
  private var englishMode = false
  private var leftShiftDown = false
  private var keyPressedWhileShift = false

  private static let punctuationMap: [Character: String] = [
    ",": "，", ".": "。", "?": "？", "!": "！",
    ";": "；", ":": "：", "\\": "、",
    "(": "（", ")": "）", "<": "《", ">": "》",
    "[": "【", "]": "】", "{": "「", "}": "」",
    "$": "￥", "^": "……", "_": "——",
    "@": "＠", "#": "＃", "&": "＆", "~": "～",
  ]

  // MARK: - Lifecycle

  override init!(server: IMKServer!, delegate: Any!, client: Any!) {
    currentClient = client as? IMKTextInput
    super.init(server: server, delegate: delegate, client: client)
  }

  override func activateServer(_ sender: Any!) {
    currentClient = sender as? IMKTextInput
    state.reset()
  }

  override func deactivateServer(_ sender: Any!) {
    commitOrClear(sender)
    hidePalettes()
    currentClient = nil
  }

  override func commitComposition(_ sender: Any!) {
    currentClient = sender as? IMKTextInput
    commitOrClear(sender)
  }

  override func hidePalettes() {
    SimePanel.shared.hide()
    super.hidePalettes()
  }

  // MARK: - Key handling

  override func recognizedEvents(_ sender: Any!) -> Int {
    Int(NSEvent.EventTypeMask.keyDown.rawValue
        | NSEvent.EventTypeMask.flagsChanged.rawValue)
  }

  // swiftlint:disable:next cyclomatic_complexity
  override func handle(_ event: NSEvent!, client sender: Any!) -> Bool {
    guard let event = event else { return false }
    currentClient = sender as? IMKTextInput

    // --- Left-Shift tap toggles English mode (no other key in between) ---
    if event.type == .flagsChanged {
      if Int(event.keyCode) == kVK_Shift {  // left shift only
        let pressed = event.modifierFlags.contains(.shift)
        if pressed {
          leftShiftDown = true
          keyPressedWhileShift = false
        } else if leftShiftDown {
          if !keyPressedWhileShift {
            toggleEnglishMode()
          }
          leftShiftDown = false
        }
      }
      return false
    }

    guard event.type == .keyDown else { return false }

    // A real key was pressed during the shift press — disqualify the tap.
    if leftShiftDown { keyPressedWhileShift = true }

    // Ignore Command-modified keys
    if event.modifierFlags.contains(.command) { return false }

    // --- English mode: pass printable ASCII straight through ---
    if englishMode, !event.modifierFlags.contains(.control),
       let c = (event.characters ?? "").first,
       let v = c.asciiValue, v >= 0x20, v <= 0x7E {
      if !state.buffer.isEmpty { commitOrClear(sender) }
      commit(string: String(c))
      return true
    }

    let keyCode = Int(event.keyCode)
    // Use `characters` (with Shift applied) so Shift+A → "A". IMK's
    // charactersIgnoringModifiers returns "a" for Shift+A on letter keys,
    // which would otherwise fall into the lowercase pinyin branch below.
    let chars = event.characters ?? ""

    // --- Prediction mode: try prediction-specific keys first ---
    if state.predicting {
      if let handled = handlePredictionKey(keyCode: keyCode, chars: chars) {
        return handled
      }
      // Not consumed by prediction — exit prediction mode and fall through
      // to normal handling so the key (e.g. a pinyin letter) isn't dropped.
      state.predicting = false
      state.candidates = []
      state.highlightedIndex = 0
      state.page = 0
      SimePanel.shared.hide()
    }

    // --- Candidate navigation when buffer is active ---
    if !state.buffer.isEmpty {
      if let handled = handleNavigationKey(keyCode: keyCode, chars: chars) {
        return handled
      }
    }

    // --- Letter input: lowercase a-z and uppercase A-Z both go into the
    // buffer (matches fcitx5-sime: case is preserved so the engine's English
    // DAT can pick up mixed-case input). Apostrophe is a syllable separator,
    // only meaningful mid-buffer.
    if let c = chars.first {
      if (c >= "a" && c <= "z")
          || (c >= "A" && c <= "Z")
          || (c == "'" && !state.buffer.isEmpty) {
        appendChar(c)
        return true
      }
    }

    // --- Backspace ---
    if keyCode == kVK_Delete {
      return handleBackspace()
    }

    // --- Escape ---
    if keyCode == kVK_Escape {
      if !state.isEmpty {
        clearAll()
        return true
      }
      return false
    }

    // --- Enter: commit raw input ---
    if keyCode == kVK_Return || keyCode == kVK_ANSI_KeypadEnter {
      if !state.buffer.isEmpty {
        commit(string: state.committedText + state.remaining)
        state.reset()
        return true
      }
      return false
    }

    // --- Punctuation (Chinese or ASCII based on toggle) ---
    if !event.modifierFlags.contains(.control), let c = chars.first,
       let mapped = punctuationOutput(c) {
      if !state.buffer.isEmpty { commitOrClear(sender) }
      commit(string: mapped)
      if mapped == "。" || mapped == "？" || mapped == "！" {
        state.clearContext()
      }
      return true
    }

    // --- Fullwidth digits / space when fullwidth-character toggle is on ---
    if SimeSettings.shared.fullwidthCharacter, state.buffer.isEmpty,
       let c = chars.first, (c >= "0" && c <= "9") || c == " " {
      commit(string: toFullwidth(c))
      return true
    }

    return false
  }

  // Returns the character to commit for a recognized punctuation key,
  // honoring the fullwidth-punctuation toggle. nil means "not punctuation
  // we manage" — caller should fall through.
  private func punctuationOutput(_ c: Character) -> String? {
    let fullwidth = SimeSettings.shared.fullwidthPunctuation
    if let mapped = SimeInputController.punctuationMap[c] {
      return fullwidth ? mapped : String(c)
    }
    if c == "\"" {
      guard fullwidth else { return "\"" }
      defer { doubleQuoteOpen.toggle() }
      return doubleQuoteOpen ? "\u{201C}" : "\u{201D}"
    }
    if c == "'" {
      guard fullwidth else { return "'" }
      defer { singleQuoteOpen.toggle() }
      return singleQuoteOpen ? "\u{2018}" : "\u{2019}"
    }
    return nil
  }

  private func toFullwidth(_ c: Character) -> String {
    guard let ascii = c.asciiValue else { return String(c) }
    if ascii == 0x20 { return "\u{3000}" }
    if ascii >= 0x21 && ascii <= 0x7E {
      return String(UnicodeScalar(UInt32(ascii) + 0xFEE0)!)
    }
    return String(c)
  }

  // MARK: - Menu bar dropdown (mirrors fcitx5 status-area actions)

  override func menu() -> NSMenu! {
    let menu = NSMenu()

    func add(_ title: String, _ on: Bool, _ action: Selector) {
      let item = NSMenuItem(title: title, action: action, keyEquivalent: "")
      item.state = on ? .on : .off
      item.target = self
      menu.addItem(item)
    }
    add("英文模式", englishMode, #selector(toggleEnglish))
    menu.addItem(.separator())
    add("中文标点", SimeSettings.shared.fullwidthPunctuation,
        #selector(toggleFullwidthPunctuation))
    add("全角字符", SimeSettings.shared.fullwidthCharacter,
        #selector(toggleFullwidthCharacter))
    add("启用联想", SimeSettings.shared.prediction,
        #selector(togglePrediction))
    return menu
  }

  @objc private func toggleFullwidthPunctuation() {
    SimeSettings.shared.fullwidthPunctuation.toggle()
  }
  @objc private func toggleFullwidthCharacter() {
    SimeSettings.shared.fullwidthCharacter.toggle()
  }
  @objc private func togglePrediction() {
    SimeSettings.shared.prediction.toggle()
    if !SimeSettings.shared.prediction {
      state.predicting = false
      SimePanel.shared.hide()
    }
  }
  @objc private func toggleEnglish() { toggleEnglishMode() }

  private func toggleEnglishMode() {
    if !state.buffer.isEmpty { commitOrClear(currentClient) }
    state.predicting = false
    SimePanel.shared.hide()
    englishMode.toggle()
  }

  // MARK: - Private key helpers

  private func appendChar(_ c: Character) {
    state.buffer.append(c)
    state.page = 0
    decode()
    updateUI()
  }

  private func handleBackspace() -> Bool {
    if !state.selections.isEmpty && state.remaining.isEmpty {
      // Undo last selection
      state.selections.removeLast()
      state.page = 0
      decode()
      updateUI()
      return true
    }
    if !state.buffer.isEmpty {
      state.buffer.removeLast()
      state.page = 0
      if state.buffer.isEmpty {
        clearAll()
      } else {
        decode()
        updateUI()
      }
      return true
    }
    return false
  }

  // Returns nil if key not handled as navigation.
  private func handleNavigationKey(keyCode: Int, chars: String) -> Bool? {
    let page = state.pageCandidates

    // Number keys 1–9: direct selection
    if let c = chars.first, c >= "1" && c <= "9" {
      let idx = Int(c.asciiValue! - Character("1").asciiValue!)
      if idx < page.count {
        select(pageIndex: idx)
        return true
      }
    }

    // Space: select highlighted
    if keyCode == kVK_Space {
      let idx = state.highlightedIndex - state.pageStart
      if idx >= 0 && idx < page.count {
        select(pageIndex: idx)
      } else if !page.isEmpty {
        select(pageIndex: 0)
      }
      return true
    }

    // Tab / Down: next candidate
    if keyCode == kVK_Tab || keyCode == kVK_DownArrow {
      let next = state.highlightedIndex + 1
      if next < state.candidates.count {
        state.highlightedIndex = next
        if next >= state.pageStart + SimeState.pageSize {
          state.page += 1
        }
        updateUI()
      }
      return true
    }

    // Shift+Tab / Up: previous candidate
    if (keyCode == kVK_Tab && NSEvent.modifierFlags.contains(.shift))
        || keyCode == kVK_UpArrow {
      let prev = state.highlightedIndex - 1
      if prev >= 0 {
        state.highlightedIndex = prev
        if prev < state.pageStart {
          state.page = max(0, state.page - 1)
        }
        updateUI()
      }
      return true
    }

    // Page down / =
    if keyCode == kVK_PageDown || (chars == "=" && state.hasNextPage) {
      if state.hasNextPage {
        state.page += 1
        state.highlightedIndex = state.pageStart
        updateUI()
      }
      return true
    }

    // Page up / -
    if keyCode == kVK_PageUp || chars == "-" {
      if state.page > 0 {
        state.page -= 1
        state.highlightedIndex = state.pageStart
        updateUI()
      }
      return true
    }

    return nil
  }

  // Returns nil if the key isn't a prediction-mode key (caller should exit
  // prediction mode and continue normal processing).
  private func handlePredictionKey(keyCode: Int, chars: String) -> Bool? {
    let page = state.pageCandidates

    // Number keys: select prediction
    if let c = chars.first, c >= "1" && c <= "9" {
      let idx = Int(c.asciiValue! - Character("1").asciiValue!)
      if idx < page.count {
        selectPrediction(pageIndex: idx)
        return true
      }
    }
    // Space: select highlighted prediction
    if keyCode == kVK_Space {
      let idx = state.highlightedIndex - state.pageStart
      if idx >= 0 && idx < page.count {
        selectPrediction(pageIndex: idx)
      } else if !page.isEmpty {
        selectPrediction(pageIndex: 0)
      }
      return true
    }
    // Escape: dismiss predictions
    if keyCode == kVK_Escape {
      state.clearContext()
      SimePanel.shared.hide()
      clearMarkedText()
      return true
    }
    return nil
  }

  // MARK: - Engine calls

  private func decode() {
    let rem = state.remaining
    guard !rem.isEmpty else {
      state.candidates = []
      state.highlightedIndex = 0
      return
    }
    // DecodeSentence returns N-best full sentences; decodeStr adds word-level
    // candidates anchored at the start. Merge: sentence results first, then
    // unique word-level additions, matching Fcitx5 updateUI() approach.
    let sentence = SimeEngine.shared.decodeSentence(rem, extra: 2)
    var seen = Set(sentence.map(\.text))
    let words = SimeEngine.shared.decodeStr(rem, num: 9).filter { seen.insert($0.text).inserted }
    state.candidates = sentence + words
    // Keep highlighted index valid
    let maxIdx = max(0, state.candidates.count - 1)
    if state.highlightedIndex > maxIdx { state.highlightedIndex = 0 }
  }

  // MARK: - Candidate selection

  func select(pageIndex: Int) {
    let start = state.pageStart
    let absIdx = start + pageIndex
    guard absIdx < state.candidates.count else { return }
    let c = state.candidates[absIdx]
    state.selections.append(Selection(text: c.text, units: c.units,
                                      tokens: c.tokens, consumed: c.consumed))
    state.page = 0
    state.highlightedIndex = 0

    if state.fullySelected {
      // Commit everything and show predictions
      let text = state.committedText
      for sel in state.selections {
        state.pushContext(sel.text, sel.tokens, maxSize: SimeEngine.shared.contextSize)
      }
      commit(string: text)
      state.reset()
      showPredictions()
    } else {
      decode()
      updateUI()
    }
  }

  func selectPrediction(pageIndex: Int) {
    let absIdx = state.pageStart + pageIndex
    guard absIdx < state.candidates.count else { return }
    let c = state.candidates[absIdx]
    state.pushContext(c.text, c.tokens, maxSize: SimeEngine.shared.contextSize)
    commit(string: c.text)
    state.reset()
    showPredictions()
  }

  private func showPredictions() {
    guard SimeSettings.shared.prediction, !state.contextIds.isEmpty else {
      SimePanel.shared.hide()
      return
    }
    let results = SimeEngine.shared.nextTokens(state.contextIds, num: SimeState.pageSize)
    guard !results.isEmpty else {
      SimePanel.shared.hide()
      return
    }
    state.candidates = results
    state.highlightedIndex = 0
    state.page = 0
    state.predicting = true

    let inputPos = cursorRect()
    SimePanel.shared.controller = self
    SimePanel.shared.update(
      preedit: nil,
      candidates: results.map(\.text),
      highlighted: 0,
      position: inputPos
    )
  }

  // MARK: - UI updates

  private func updateUI() {
    guard let client = currentClient else { return }

    // Build preedit attributed string:
    //   [committed hanzi — highlighted] + [remaining pinyin — underlined]
    let committed = state.committedText
    let rem = state.remaining

    // Insert spaces at syllable boundaries using results[0].units
    var remDisplay = rem
    if let top = state.candidates.first, !top.units.isEmpty && top.consumed > 0 {
      remDisplay = syllabify(rem, units: top.units, consumed: top.consumed)
    }

    let preeditStr = committed + remDisplay
    let attrStr = NSMutableAttributedString(string: preeditStr)

    if !committed.isEmpty {
      let committedAttrs = mark(forStyle: kTSMHiliteConvertedText,
                                at: NSRange(location: 0, length: committed.utf16.count))
        as! [NSAttributedString.Key: Any]
      attrStr.setAttributes(committedAttrs,
                             range: NSRange(location: 0, length: committed.utf16.count))
    }
    if !remDisplay.isEmpty {
      let remRange = NSRange(location: committed.utf16.count,
                             length: remDisplay.utf16.count)
      let remAttrs = mark(forStyle: kTSMHiliteSelectedRawText, at: remRange)
        as! [NSAttributedString.Key: Any]
      attrStr.setAttributes(remAttrs, range: remRange)
    }

    let caretPos = preeditStr.utf16.count
    client.setMarkedText(attrStr,
                         selectionRange: NSRange(location: caretPos, length: 0),
                         replacementRange: NSRange(location: NSNotFound, length: 0))

    // Show candidate panel
    if !state.pageCandidates.isEmpty {
      let inputPos = cursorRect()
      SimePanel.shared.controller = self
      SimePanel.shared.update(
        preedit: nil,
        candidates: state.pageCandidates.map(\.text),
        highlighted: state.highlightedIndex - state.pageStart,
        position: inputPos
      )
    } else {
      SimePanel.shared.hide()
    }
  }

  // MARK: - Helpers

  private func commit(string: String) {
    guard !string.isEmpty, let client = currentClient else { return }
    client.insertText(string, replacementRange: NSRange(location: NSNotFound, length: 0))
    clearMarkedText()
    SimePanel.shared.hide()
  }

  private func clearMarkedText() {
    currentClient?.setMarkedText("",
                                 selectionRange: NSRange(location: 0, length: 0),
                                 replacementRange: NSRange(location: NSNotFound, length: 0))
  }

  private func clearAll() {
    state.reset()
    clearMarkedText()
    SimePanel.shared.hide()
  }

  private func commitOrClear(_ sender: Any?) {
    if !state.buffer.isEmpty {
      // Commit: selected text + DecodeSentence top result for remaining
      var text = state.committedText
      let rem = state.remaining
      if !rem.isEmpty {
        if let top = SimeEngine.shared.decodeSentence(rem, extra: 0).first {
          text += top.text
        } else {
          text += rem
        }
      }
      commit(string: text)
    }
    state.reset()
  }

  private func cursorRect() -> NSRect {
    var rect = NSRect.zero
    currentClient?.attributes(forCharacterIndex: 0, lineHeightRectangle: &rect)
    return rect
  }

  /// Inserts spaces at syllable boundaries indicated by units apostrophes,
  /// mirroring updateUI() in sime-ime.cc.
  private func syllabify(_ rem: String, units: String, consumed: Int) -> String {
    var result = ""
    var ri = rem.startIndex
    var ui = units.startIndex
    let remEnd = rem.index(rem.startIndex, offsetBy: min(consumed, rem.count))
    while ri < remEnd && ui < units.endIndex {
      if units[ui] == "'" {
        result += " "
        ui = units.index(after: ui)
      } else {
        result.append(rem[ri])
        ri = rem.index(after: ri)
        ui = units.index(after: ui)
      }
    }
    if ri < rem.endIndex {
      result += " " + rem[ri...]
    }
    return result
  }
}

