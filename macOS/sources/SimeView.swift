// Custom candidate list drawing — simplified from Squirrel's SquirrelView.
// Draws a preedit line (optional) followed by a row of numbered candidates.

import AppKit

final class SimeView: NSView {
  // Content to render
  var preedit: String? = nil
  var candidates: [String] = []
  var highlighted: Int = 0

  // Layout constants
  static let padding: CGFloat = 10
  static let candidateSpacing: CGFloat = 8
  static let cornerRadius: CGFloat = 10

  // Font sizes
  static let preeditFont = NSFont.systemFont(ofSize: 14)
  static let candidateFont = NSFont.systemFont(ofSize: 16)
  static let labelFont = NSFont.monospacedDigitSystemFont(ofSize: 13, weight: .medium)

  // Computed size for current content
  private(set) var contentSize: NSSize = .zero

  // CAShapeLayer used by the parent panel to mask the blur view
  let shape = CAShapeLayer()

  func recompute() {
    contentSize = computeSize()
    shape.path = CGPath(
      roundedRect: CGRect(origin: .zero, size: contentSize),
      cornerWidth: SimeView.cornerRadius, cornerHeight: SimeView.cornerRadius,
      transform: nil)
    setNeedsDisplay(bounds)
  }

  // MARK: - Drawing

  override func draw(_ dirtyRect: NSRect) {
    var y: CGFloat = SimeView.padding

    // Preedit
    if let pre = preedit, !pre.isEmpty {
      let attrs: [NSAttributedString.Key: Any] = [
        .font: SimeView.preeditFont,
        .foregroundColor: NSColor.secondaryLabelColor,
      ]
      let str = NSAttributedString(string: pre, attributes: attrs)
      let size = str.size()
      str.draw(at: NSPoint(x: SimeView.padding, y: bounds.height - y - size.height))
      y += size.height + 6
    }

    // Candidates
    var x: CGFloat = SimeView.padding
    for (i, text) in candidates.enumerated() {
      let isHighlighted = i == highlighted
      let label = "\(i + 1). "

      let labelAttrs: [NSAttributedString.Key: Any] = [
        .font: SimeView.labelFont,
        .foregroundColor: isHighlighted ? NSColor.white : NSColor.tertiaryLabelColor,
      ]
      let textAttrs: [NSAttributedString.Key: Any] = [
        .font: SimeView.candidateFont,
        .foregroundColor: isHighlighted ? NSColor.white : NSColor.labelColor,
      ]

      let labelStr = NSAttributedString(string: label, attributes: labelAttrs)
      let textStr = NSAttributedString(string: text, attributes: textAttrs)

      let full = NSMutableAttributedString(attributedString: labelStr)
      full.append(textStr)

      let size = full.size()
      let cellRect = NSRect(x: x - 4,
                            y: bounds.height - y - (size.height + 8),
                            width: size.width + 8,
                            height: size.height + 8)

      if isHighlighted {
        let path = NSBezierPath(roundedRect: cellRect,
                                xRadius: 6, yRadius: 6)
        NSColor.controlAccentColor.setFill()
        path.fill()
      }

      full.draw(at: NSPoint(x: x, y: cellRect.origin.y + 4))
      x += size.width + SimeView.candidateSpacing
    }
  }

  // MARK: - Size computation

  private func computeSize() -> NSSize {
    var width: CGFloat = 0
    var height: CGFloat = SimeView.padding * 2

    if let pre = preedit, !pre.isEmpty {
      let attrs: [NSAttributedString.Key: Any] = [.font: SimeView.preeditFont]
      let sz = (pre as NSString).size(withAttributes: attrs)
      width = max(width, sz.width + SimeView.padding * 2)
      height += sz.height + 6
    }

    var candidateWidth: CGFloat = SimeView.padding
    var maxCandHeight: CGFloat = 0
    for (i, text) in candidates.enumerated() {
      let label = "\(i + 1). "
      let lw = (label as NSString).size(withAttributes: [.font: SimeView.labelFont]).width
      let tw = (text as NSString).size(withAttributes: [.font: SimeView.candidateFont]).width
      let th = (text as NSString).size(withAttributes: [.font: SimeView.candidateFont]).height
      candidateWidth += lw + tw + SimeView.candidateSpacing
      maxCandHeight = max(maxCandHeight, th)
    }
    candidateWidth += SimeView.padding

    width = max(width, max(candidateWidth, 120))
    height += maxCandHeight + 8  // cell padding

    return NSSize(width: width, height: height)
  }
}
