package com.semantic.sime.ime.data;

/**
 * Static symbol tables for the symbol keyboard. Each tab is exactly 24
 * entries so the grid stays a fixed 3×8 across all tabs — no trailing
 * empty cells, no variable height between tabs.
 */
public final class Symbols {

    private Symbols() {}

    // Single-char / single-glyph icons in the bottom navigation row.
    // Each is a representative symbol (or character) for its tab so the
    // row scans quickly and stays compact for 8 tabs.
    public static final String[] TAB_NAMES = {
            "中", "英", "①", "→", "π", "★", "¥", "©"
    };

    public static final String[][] TABS = {
        // 中文标点 (24)
        {
            "，", "。", "？", "！", "、", "：", "；", "·",
            "\u201C", "\u201D", "\u2018", "\u2019", "「", "」", "（", "）",
            "《", "》", "【", "】", "——", "……", "〔", "〕"
        },
        // 英文标点 (24)
        {
            ",", ".", "?", "!", ":", ";", "\"", "'",
            "(", ")", "[", "]", "-", "_", "/", "\\",
            "@", "#", "$", "%", "&", "*", "+", "="
        },
        // 数字符号 (24) — circled / Roman numerals + a few basic ops.
        {
            "①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧",
            "⑨", "⑩", "Ⅰ", "Ⅱ", "Ⅲ", "Ⅳ", "Ⅴ", "Ⅵ",
            "Ⅶ", "Ⅷ", "Ⅸ", "Ⅹ", "×", "÷", "±", "°"
        },
        // 箭头 (24)
        {
            "←", "→", "↑", "↓", "↖", "↗", "↘", "↙",
            "⇐", "⇒", "⇑", "⇓", "⇔", "⇕", "⇄", "⇆",
            "↩", "↪", "⤴", "⤵", "↺", "↻", "➤", "➔"
        },
        // 数学 (24)
        {
            "＋", "−", "×", "÷", "＝", "≠", "≈", "≡",
            "＜", "＞", "≤", "≥", "∞", "√", "∛", "∑",
            "∏", "∫", "π", "Δ", "Σ", "∂", "∇", "∝"
        },
        // 图形 (24)
        {
            "★", "☆", "♥", "♡", "♠", "♣", "♦", "✦",
            "○", "●", "□", "■", "△", "▲", "▽", "▼",
            "✓", "✗", "☑", "☒", "☞", "⚠", "§", "※"
        },
        // 货币 (24)
        {
            "¥", "$", "€", "£", "¢", "₹", "₩", "₽",
            "₣", "₤", "₥", "₦", "₧", "₨", "₪", "₫",
            "₮", "₱", "₴", "₵", "₸", "元", "圆", "角"
        },
        // 特殊 (24) — unit / typographic / control glyphs.
        {
            "©", "®", "™", "℠", "‰", "‱", "†", "‡",
            "°", "′", "″", "℃", "℉", "Ω", "µ", "Å",
            "§", "¶", "‼", "⁇", "⁈", "⁉", "✱", "❋"
        }
    };
}
