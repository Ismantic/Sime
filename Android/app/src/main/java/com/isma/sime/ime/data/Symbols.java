package com.isma.sime.ime.data;

/**
 * Static symbol tables for the symbol keyboard. Three tabs:
 * <ul>
 *   <li>中标点 — Chinese punctuation</li>
 *   <li>英标点 — English punctuation / ASCII symbols</li>
 *   <li>数字符号 — math and currency</li>
 * </ul>
 *
 * <p>Each tab has exactly 24 entries so the symbol keyboard's grid
 * stays a fixed 3×8 across all tabs — no trailing empty cells, no
 * variable height between tabs.
 */
public final class Symbols {

    private Symbols() {}

    public static final String[] TAB_NAMES = {"中文标点", "英文标点", "数字符号"};

    public static final String[][] TABS = {
        // 中文标点 (24)
        {
            "，", "。", "？", "！", "、", "：", "；", "·",
            "\u201C", "\u201D", "\u2018", "\u2019", "「", "」", "（", "）",
            "《", "》", "【", "】", "——", "……", "〔", "〕"
        },
        // 英文标点 (24) — dropped {, }, <, >, |, ~, ^, ` from the original
        // 32 to fit a 3x8 grid; the rest are the most common ASCII puncs.
        {
            ",", ".", "?", "!", ":", ";", "\"", "'",
            "(", ")", "[", "]", "-", "_", "/", "\\",
            "@", "#", "$", "%", "&", "*", "+", "="
        },
        // 数字符号 (24) — hard-to-input number symbols. Plain 0-9
        // are reachable from the number keyboard already, so this tab
        // focuses on full circled / Roman numerals plus a few common
        // math operators that aren't on the standard keyboard.
        {
            "①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧",
            "⑨", "⑩", "Ⅰ", "Ⅱ", "Ⅲ", "Ⅳ", "Ⅴ", "Ⅵ",
            "Ⅶ", "Ⅷ", "Ⅸ", "Ⅹ", "×", "÷", "±", "°"
        }
    };
}
