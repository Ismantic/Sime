package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.isma.sime.ime.KeyboardMode;

/**
 * 10-9-7 QWERTY layout plus a bottom row with {@code 123 ' ␣ 中/英 ⏎}.
 * Used for both CHINESE and ENGLISH modes — the caller sets the current
 * mode via {@link #setMode(KeyboardMode)}. Shift state is kept locally.
 */
public class QwertyKeyboardView extends KeyboardView {

    private static final String[] ROW1 = {"q","w","e","r","t","y","u","i","o","p"};
    private static final String[] ROW2 = {"a","s","d","f","g","h","j","k","l"};
    private static final String[] ROW3 = {"z","x","c","v","b","n","m"};

    private boolean shift = false;
    private KeyboardMode mode = KeyboardMode.CHINESE;

    private LinearLayout row1;
    private LinearLayout row2;
    private LinearLayout row3;
    private TextView shiftKey;
    private TextView toggleLangKey;

    public QwertyKeyboardView(Context context) {
        super(context);
        build();
    }

    public void setMode(KeyboardMode m) {
        this.mode = m;
        if (toggleLangKey != null) {
            toggleLangKey.setText(m == KeyboardMode.ENGLISH ? "EN" : "中");
        }
    }

    private void build() {
        row1 = makeRow();
        addLetterRow(row1, ROW1);
        addView(row1);

        row2 = makeRow();
        // Small left/right indent on the home row for the classic look.
        row2.addView(makeFiller(0.5f));
        addLetterRow(row2, ROW2);
        row2.addView(makeFiller(0.5f));
        addView(row2);

        row3 = makeRow();
        shiftKey = makeKey("⇧", 1.5f, 16f, true, () -> {
            shift = !shift;
            refreshLetterLabels();
        });
        row3.addView(shiftKey);
        addLetterRow(row3, ROW3);
        TextView bs = makeKey("⌫", 1.5f, 18f, true, () -> emit(SimeKey.backspace()));
        row3.addView(bs);
        addView(row3);

        LinearLayout row4 = makeRow();
        row4.addView(makeKey("123", 1.5f, 14f, true, () -> emit(SimeKey.toNumber())));
        row4.addView(makeKey("符", 1.0f, 14f, true, () -> emit(SimeKey.toSymbol())));
        row4.addView(makeKey("'", 1.0f, 16f, true, () -> emit(SimeKey.separator())));
        row4.addView(makeKey("空格", 3.5f, 14f, true, () -> emit(SimeKey.space())));
        toggleLangKey = makeKey("中", 1.0f, 14f, true, () -> emit(SimeKey.toggleLang()));
        row4.addView(toggleLangKey);
        row4.addView(makeKey("⏎", 1.5f, 18f, true, () -> emit(SimeKey.enter())));
        addView(row4);
    }

    private void addLetterRow(LinearLayout row, String[] letters) {
        for (String l : letters) {
            final String base = l;
            row.addView(makeKey(displayLetter(base), 1f, 18f, false,
                    () -> onLetter(base)));
        }
    }

    private String displayLetter(String base) {
        return shift ? base.toUpperCase() : base;
    }

    private void onLetter(String base) {
        char c = shift ? Character.toUpperCase(base.charAt(0)) : base.charAt(0);
        emit(SimeKey.letter(c));
        // iOS-style: shift auto-releases after one use (no caps lock).
        if (shift) {
            shift = false;
            refreshLetterLabels();
        }
    }

    private void refreshLetterLabels() {
        refreshRow(row1, ROW1, 0);
        // row2 has filler at position 0 and last.
        refreshRow(row2, ROW2, 1);
        // row3 has shift at position 0 then letters.
        refreshRow(row3, ROW3, 1);
    }

    private void refreshRow(LinearLayout row, String[] letters, int offset) {
        for (int i = 0; i < letters.length; i++) {
            android.view.View child = row.getChildAt(i + offset);
            if (child instanceof TextView) {
                ((TextView) child).setText(displayLetter(letters[i]));
            }
        }
    }
}
