package com.shiyu.sime.ime.keyboard;

import android.content.Context;

import com.shiyu.sime.ime.KeyboardMode;
import com.shiyu.sime.ime.keyboard.framework.KeyView;
import com.shiyu.sime.ime.keyboard.framework.KeyboardContainer;
import com.shiyu.sime.ime.keyboard.layouts.QwertyLayout;

import java.util.HashMap;
import java.util.Map;

/**
 * QWERTY keyboard for both Chinese pinyin and English modes. The static
 * layout comes from {@link QwertyLayout}; this controller flips three
 * runtime states:
 *
 * <ul>
 *   <li><b>case</b>: Chinese mode shows uppercase letters. English mode
 *       follows the local {@link #shift} flag (default lowercase, shift
 *       toggles to uppercase for one tap).
 *   <li><b>shift / 分词 key</b> (left of row 3): English shows
 *       {@code ⇧} and toggles shift on click; Chinese shows
 *       {@code 分词} and emits {@link SimeKey#separator()}.
 *   <li><b>enter / 确定 key</b> (right of row 4): shows {@code 换行}
 *       normally, {@code 确定} when in Chinese mode with active input.
 * </ul>
 */
public class QwertyKeyboardView extends KeyboardView {

    /**
     * Half-width → full-width punctuation map for Chinese mode. Only
     * symbols that have a conventional Chinese form are listed; the rest
     * stay half-width (matches Sogou / 百度 / doubao behaviour).
     */
    private static final Map<String, String> CN_PUNCT = new HashMap<>();
    static {
        CN_PUNCT.put(",", "，");
        CN_PUNCT.put(".", "。");
        CN_PUNCT.put(":", "：");
        CN_PUNCT.put(";", "；");
        CN_PUNCT.put("(", "（");
        CN_PUNCT.put(")", "）");
        CN_PUNCT.put("\"", "\u201C");  // 用左双引号；右引号用得少先不智能配对
        CN_PUNCT.put("?", "？");
        CN_PUNCT.put("!", "！");
        // 半角保留: - / ~ ' @ _ # & …
    }

    private KeyboardContainer container;
    private KeyboardMode mode = KeyboardMode.CHINESE;
    private boolean active = false;
    private boolean shift = false;

    public QwertyKeyboardView(Context context) {
        super(context);
        build();
    }

    private void build() {
        container = new KeyboardContainer(getContext(), theme);
        LayoutParams lp = new LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
        container.setLayoutParams(lp);
        container.setOnKeyEmitListener(this::emit);
        container.setLayout(QwertyLayout.build());
        addView(container);

        installShiftHandler();
        refreshAll();
    }

    public void setMode(KeyboardMode m) {
        this.mode = m;
        if (m == KeyboardMode.ENGLISH) {
            // Reset shift when leaving Chinese — Chinese always shows
            // uppercase, English starts lowercase.
            shift = false;
        }
        refreshAll();
    }

    public void setActive(boolean active) {
        this.active = active;
        refreshEnterKey();
        refreshShiftKey();
    }

    /**
     * Override so that English-mode shift actually flips the emitted
     * char, not just the label. The KeyDef's clickAction is hard-wired
     * to lowercase letter('q'); intercepting at the emit boundary is
     * the cleanest place to apply case without rewriting per-key
     * listeners on every shift toggle.
     */
    @Override
    protected void emit(SimeKey key) {
        // Uppercase applies in both modes when shift is on. Chinese mode
        // accepts uppercase pinyin (decoder normalizes) and lets users
        // type proper-noun-style words like "iPhone" mid-buffer.
        if (shift && key.type == KeyType.LETTER) {
            char upper = Character.toUpperCase(key.ch);
            super.emit(SimeKey.letter(upper));
            // iOS-style: shift auto-releases after one letter so the
            // next tap returns to lowercase.
            shift = false;
            refreshLetters();
            return;
        }
        // Chinese mode: rewrite half-width punctuation to its full-width
        // equivalent for the symbols Chinese typing uses (, . : ; ( ) " ? !).
        // Other symbols (- / ~ ' @ _ # & …) stay half-width.
        if (mode == KeyboardMode.CHINESE && key.type == KeyType.PUNCTUATION
                && key.text != null) {
            String full = CN_PUNCT.get(key.text);
            if (full != null) {
                super.emit(SimeKey.punctuation(full));
                return;
            }
        }
        super.emit(key);
    }

    /**
     * Shift key:
     * <ul>
     *   <li>Click (any mode): toggle {@link #shift} → next letter is
     *       uppercase.</li>
     *   <li>Long-press in Chinese mode: emit a pinyin separator
     *       ({@code '}). English mode long-press is a no-op.</li>
     * </ul>
     * Visual is always {@code ⇧}; highlight reflects the shift state.
     */
    private void installShiftHandler() {
        KeyView shiftKey = container.findKeyById(QwertyLayout.ID_SHIFT);
        if (shiftKey == null) return;
        shiftKey.setListener((def, action) -> {
            if (action == KeyView.KeyAction.CLICK) {
                shift = !shift;
                refreshLetters();
            } else if (action == KeyView.KeyAction.LONG_PRESS
                    && mode == KeyboardMode.CHINESE) {
                emit(SimeKey.separator());
            }
        });
    }

    private void refreshAll() {
        refreshLetters();
        refreshShiftKey();
        refreshLangKey();
        refreshEnterKey();
        refreshPunctKeys();
    }

    /** Comma / period labels follow mode (display only — actual half-→
     *  full-width conversion happens in {@link #emit}). */
    private void refreshPunctKeys() {
        boolean cn = (mode == KeyboardMode.CHINESE);
        KeyView c = container.findKeyById(QwertyLayout.ID_COMMA);
        if (c != null) c.setLabel(cn ? "，" : ",");
        KeyView p = container.findKeyById(QwertyLayout.ID_PERIOD);
        if (p != null) p.setLabel(cn ? "。" : ".");
    }

    private void refreshLetters() {
        // Chinese mode: always uppercase. English mode: shift state.
        boolean upper = (mode == KeyboardMode.CHINESE) || shift;
        applyCase(QwertyLayout.ROW1, upper);
        applyCase(QwertyLayout.ROW2, upper);
        applyCase(QwertyLayout.ROW3, upper);
        // Letter case toggle in English mode also affects the shift
        // key highlight; keep it in sync.
        refreshShiftKey();
    }

    private void applyCase(String[] letters, boolean upper) {
        for (String l : letters) {
            KeyView kv = container.findKeyById(QwertyLayout.LETTER_ID_PREFIX + l);
            if (kv != null) kv.setLabel(upper ? l.toUpperCase() : l);
        }
    }

    private void refreshShiftKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_SHIFT);
        if (kv == null) return;
        kv.setLabel("⇧");
        kv.setHighlighted(shift);
        // CN mode: show "'" superscript so users discover that long-press
        // emits the pinyin separator. EN mode has no such fallback.
        kv.setHintLabel(mode == KeyboardMode.CHINESE ? "'" : null);
    }

    private void refreshLangKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_LANG);
        if (kv != null) {
            kv.setLabel(mode == KeyboardMode.CHINESE ? "中" : "EN");
        }
    }

    private void refreshEnterKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_ENTER);
        if (kv != null) {
            kv.setLabel(active ? "确定" : "换行");
        }
    }
}
