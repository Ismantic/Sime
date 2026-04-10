package com.isma.sime.ime.data;

/**
 * Static symbol tables for the symbol keyboard. Three tabs only (see
 * refactor plan §6.4 simplified scheme B):
 * <ul>
 *   <li>中标点 — Chinese punctuation</li>
 *   <li>英标点 — English punctuation / ASCII symbols</li>
 *   <li>数字符号 — math and currency</li>
 * </ul>
 */
public final class Symbols {

    private Symbols() {}

    public static final String[] TAB_NAMES = {"中标点", "英标点", "数字符号"};

    public static final String[][] TABS = {
        // 中标点
        {
            "，", "。", "？", "！", "、", "：", "；",
            "\u201C", "\u201D", "\u2018", "\u2019",
            "（", "）", "《", "》", "【", "】",
            "——", "……", "·", "￥"
        },
        // 英标点
        {
            ",", ".", "?", "!", ":", ";",
            "\"", "'", "(", ")", "[", "]", "{", "}",
            "-", "_", "/", "\\", "@", "#",
            "$", "%", "&", "*", "+", "=",
            "<", ">", "|", "~", "^", "`"
        },
        // 数字符号
        {
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "+", "-", "×", "÷", "=", "%",
            ".", ",", "(", ")", "$", "¥", "€", "£"
        }
    };
}
