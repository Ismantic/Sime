// Candidate panel — floating NSPanel with blur background.
// Mirrors Squirrel's SquirrelPanel structure but significantly simplified.

import AppKit

final class SimePanel: NSPanel {
  static let shared = SimePanel()

  private let view: SimeView
  private let blur: NSVisualEffectView

  private init() {
    view = SimeView(frame: .zero)
    blur = NSVisualEffectView()

    super.init(contentRect: .zero,
               styleMask: .nonactivatingPanel,
               backing: .buffered,
               defer: true)

    level = NSWindow.Level(rawValue: Int(CGShieldingWindowLevel()) - 1)
    hasShadow = true
    isOpaque = false
    backgroundColor = .clear
    collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]

    // Blur background masked to the view's rounded shape
    blur.blendingMode = .behindWindow
    blur.material = .hudWindow
    blur.state = .active
    blur.wantsLayer = true
    blur.layer?.mask = view.shape

    let content = NSView()
    content.addSubview(blur)
    content.addSubview(view)
    contentView = content
  }

  // MARK: - Public API

  /// Update panel content and reposition near `position` (cursor rect).
  func update(preedit: String?, candidates: [String], highlighted: Int,
              position: NSRect) {
    guard !candidates.isEmpty else { hide(); return }

    view.preedit = preedit
    view.candidates = candidates
    view.highlighted = highlighted
    view.recompute()

    let size = view.contentSize
    view.frame = NSRect(origin: .zero, size: size)
    blur.frame = NSRect(origin: .zero, size: size)

    reposition(near: position, size: size)

    if !isVisible { orderFront(nil) }
    contentView?.setNeedsDisplay(contentView!.bounds)
  }

  func hide() {
    orderOut(nil)
    view.candidates = []
    view.preedit = nil
  }

  // MARK: - Mouse events (candidate click)

  override func sendEvent(_ event: NSEvent) {
    if event.type == .leftMouseUp {
      let loc = view.convert(mouseLocationOutsideOfEventStream, from: nil)
      if let idx = hitTest(at: loc) {
        // Notify controller via stored weak reference
        controller?.select(pageIndex: idx)
        return
      }
    }
    super.sendEvent(event)
  }

  // The active input controller sets this so clicks can trigger selection.
  weak var controller: SimeInputController?

  // MARK: - Private layout

  private func reposition(near cursorRect: NSRect, size: NSSize) {
    guard let screen = NSScreen.screens.first(where: { $0.frame.contains(cursorRect.origin) })
              ?? NSScreen.main else { return }
    let screenFrame = screen.visibleFrame

    // Default: just below the cursor
    var origin = NSPoint(x: cursorRect.minX,
                         y: cursorRect.minY - size.height - 4)

    // Flip above cursor if not enough room below
    if origin.y < screenFrame.minY {
      origin.y = cursorRect.maxY + 4
    }
    // Clamp horizontally
    if origin.x + size.width > screenFrame.maxX {
      origin.x = screenFrame.maxX - size.width
    }
    origin.x = max(origin.x, screenFrame.minX)

    setFrame(NSRect(origin: origin, size: size), display: false)
  }

  private func hitTest(at point: NSPoint) -> Int? {
    // Rough horizontal hit-test: divide the candidate row evenly.
    // A more precise approach would track per-candidate rects in SimeView.
    let candidates = view.candidates
    guard !candidates.isEmpty else { return nil }
    let candidateAreaY: CGFloat = SimeView.padding
    let totalH = view.bounds.height
    let rowY = totalH - candidateAreaY - 30  // approx candidate row y range
    guard point.y >= rowY else { return nil }

    // Walk candidate widths same as draw()
    var x: CGFloat = SimeView.padding
    for (i, text) in candidates.enumerated() {
      let label = "\(i + 1). "
      let lw = (label as NSString).size(withAttributes: [.font: SimeView.labelFont]).width
      let tw = (text as NSString).size(withAttributes: [.font: SimeView.candidateFont]).width
      let cellWidth = lw + tw + SimeView.candidateSpacing
      if point.x >= x - 4 && point.x < x + cellWidth {
        return i
      }
      x += cellWidth
    }
    return nil
  }
}
