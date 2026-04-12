package com.semantic.sime.ime.keyboard;

import android.content.Context;

import com.semantic.sime.ime.KeyboardMode;
import com.semantic.sime.ime.keyboard.framework.KeyView;
import com.semantic.sime.ime.keyboard.framework.KeyboardContainer;
import com.semantic.sime.ime.keyboard.layouts.QwertyLayout;

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
        installCommaHandler();
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
        if (mode == KeyboardMode.ENGLISH && shift
                && key.type == KeyType.LETTER) {
            char upper = Character.toUpperCase(key.ch);
            super.emit(SimeKey.letter(upper));
            // iOS-style: shift auto-releases after one letter so the
            // next tap returns to lowercase. Hold shift = caps lock
            // would need a long-press toggle, not implemented here.
            shift = false;
            refreshLetters();
            return;
        }
        super.emit(key);
    }

    /**
     * The shift key has dual semantics. We override its per-key listener
     * once: in English mode it toggles shift; in Chinese mode it emits
     * a pinyin separator. The listener checks {@link #mode} at click
     * time so we don't need to re-install on every mode change.
     */
    private void installShiftHandler() {
        KeyView shiftKey = container.findKeyById(QwertyLayout.ID_SHIFT);
        if (shiftKey == null) return;
        shiftKey.setListener((def, action) -> {
            if (action != KeyView.KeyAction.CLICK) return;
            if (mode == KeyboardMode.CHINESE) {
                emit(SimeKey.separator());
            } else {
                shift = !shift;
                refreshLetters();
            }
        });
    }

    /**
     * Comma key emits {@code ,} (English) or {@code ，} (Chinese) on
     * tap, and {@code .} / {@code 。} on long-press. The KeyDef hard-
     * codes ASCII so the listener checks the live mode and overrides
     * the emit. The placeholder longPress in the def just turns the
     * long-press timer on.
     */
    private void installCommaHandler() {
        KeyView commaKey = container.findKeyById(QwertyLayout.ID_COMMA);
        if (commaKey == null) return;
        commaKey.setListener((def, action) -> {
            boolean cn = (mode == KeyboardMode.CHINESE);
            String comma  = cn ? "，" : ",";
            String period = cn ? "。" : ".";
            if (action == KeyView.KeyAction.CLICK) {
                emit(SimeKey.punctuation(comma));
            } else if (action == KeyView.KeyAction.LONG_PRESS) {
                emit(SimeKey.punctuation(period));
            }
        });
    }

    private void refreshAll() {
        refreshLetters();
        refreshShiftKey();
        refreshLangKey();
        refreshEnterKey();
        refreshCommaKey();
    }

    private void refreshCommaKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_COMMA);
        if (kv != null) {
            boolean cn = (mode == KeyboardMode.CHINESE);
            kv.setLabel(cn ? "，" : ",");
            kv.setTopLabel(cn ? "。" : ".");
        }
    }

    private void refreshLetters() {
        // Chinese mode: always uppercase. English mode: shift state.
        boolean upper = (mode == KeyboardMode.CHINESE) || shift;
        applyCase(QwertyLayout.ROW1, upper);
        applyCase(QwertyLayout.ROW2, upper);
        applyCase(QwertyLayout.ROW3, upper);
    }

    private void applyCase(String[] letters, boolean upper) {
        for (String l : letters) {
            KeyView kv = container.findKeyById(QwertyLayout.LETTER_ID_PREFIX + l);
            if (kv != null) kv.setLabel(upper ? l.toUpperCase() : l);
        }
    }

    private void refreshShiftKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_SHIFT);
        if (kv != null) {
            kv.setLabel(mode == KeyboardMode.CHINESE ? "分词" : "⇧");
        }
    }

    private void refreshLangKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_LANG);
        if (kv != null) {
            kv.setLabel(mode == KeyboardMode.CHINESE ? "中\n英" : "EN");
        }
    }

    private void refreshEnterKey() {
        KeyView kv = container.findKeyById(QwertyLayout.ID_ENTER);
        if (kv != null) {
            boolean confirm = (mode == KeyboardMode.CHINESE) && active;
            kv.setLabel(confirm ? "确定" : "换行");
        }
    }
}
