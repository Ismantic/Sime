package com.isma.sime.ime.keyboard;

import android.content.Context;

import com.isma.sime.ime.KeyboardMode;
import com.isma.sime.ime.keyboard.framework.KeyView;
import com.isma.sime.ime.keyboard.framework.KeyboardContainer;
import com.isma.sime.ime.keyboard.layouts.QwertyLayout;

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

    private void refreshAll() {
        refreshLetters();
        refreshShiftKey();
        refreshLangKey();
        refreshEnterKey();
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
