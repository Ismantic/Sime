package com.isma.sime.ime.keyboard;

import android.content.Context;
import android.graphics.Typeface;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.isma.sime.ime.ChineseLayout;
import com.isma.sime.ime.prefs.SimePrefs;

/**
 * Inline settings panel. Shown in place of the keyboard when the user taps
 * the ⚙ icon on the candidates bar. For now only the Chinese keyboard
 * layout (QWERTY vs T9) is configurable here. A back button ({@code ←})
 * emits {@link SimeKey#toBack()} which the kernel interprets as "return to
 * previous mode".
 */
public class SettingsKeyboardView extends KeyboardView {

    public interface OnLayoutChangedListener {
        void onLayoutChanged(ChineseLayout layout);
    }

    private OnLayoutChangedListener layoutListener;
    private final SimePrefs prefs;
    private LinearLayout qwertyCard;
    private LinearLayout t9Card;

    public SettingsKeyboardView(Context context) {
        super(context);
        this.prefs = new SimePrefs(context);
        build();
    }

    public void setOnLayoutChangedListener(OnLayoutChangedListener l) {
        this.layoutListener = l;
    }

    private void build() {
        // Header row with title and back button.
        LinearLayout header = new LinearLayout(getContext());
        header.setOrientation(HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        LayoutParams hLp = new LayoutParams(LayoutParams.MATCH_PARENT, dp(44));
        header.setLayoutParams(hLp);
        addView(header);

        TextView title = new TextView(getContext());
        title.setText("键盘布局");
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f);
        title.setTextColor(theme.keyText);
        title.setPadding(dp(16), 0, 0, 0);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        title.setLayoutParams(titleLp);
        header.addView(title);

        TextView back = makeKey("←", 1f, 18f, true,
                () -> emit(SimeKey.toBack()));
        LinearLayout.LayoutParams backLp = new LinearLayout.LayoutParams(
                dp(56), LinearLayout.LayoutParams.MATCH_PARENT);
        int m = dp(6);
        backLp.setMargins(m, m, m, m);
        back.setLayoutParams(backLp);
        header.addView(back);

        // Two side-by-side cards.
        LinearLayout cards = new LinearLayout(getContext());
        cards.setOrientation(HORIZONTAL);
        LayoutParams cLp = new LayoutParams(LayoutParams.MATCH_PARENT, 0, 1f);
        cards.setLayoutParams(cLp);
        cards.setPadding(dp(12), dp(8), dp(12), dp(12));
        addView(cards);

        ChineseLayout current = prefs.getChineseLayout();
        qwertyCard = buildCard("全键盘", "QWERTY",
                current == ChineseLayout.QWERTY,
                () -> pickLayout(ChineseLayout.QWERTY));
        cards.addView(qwertyCard);

        android.view.View gap = new android.view.View(getContext());
        cards.addView(gap, new LinearLayout.LayoutParams(dp(12), 1));

        t9Card = buildCard("九宫格", "T9",
                current == ChineseLayout.T9,
                () -> pickLayout(ChineseLayout.T9));
        cards.addView(t9Card);
    }

    private LinearLayout buildCard(String title, String subtitle,
                                    boolean selected, Runnable onClick) {
        LinearLayout card = new LinearLayout(getContext());
        card.setOrientation(VERTICAL);
        card.setGravity(Gravity.CENTER);
        card.setPadding(dp(8), dp(24), dp(8), dp(24));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
        card.setLayoutParams(lp);

        int bg = selected ? theme.accentColor : theme.keyBackground;
        card.setBackground(makeKeyBg(bg));
        card.setClickable(true);
        card.setFocusable(true);
        card.setOnClickListener(v -> onClick.run());

        TextView t = new TextView(getContext());
        t.setText(title);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f);
        t.setGravity(Gravity.CENTER);
        t.setTypeface(null, selected ? Typeface.BOLD : Typeface.NORMAL);
        t.setTextColor(selected ? theme.candidateHighlight : theme.keyText);
        card.addView(t);

        TextView s = new TextView(getContext());
        s.setText(subtitle);
        s.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f);
        s.setGravity(Gravity.CENTER);
        s.setPadding(0, dp(4), 0, 0);
        s.setTextColor(theme.keyTextFunction);
        card.addView(s);

        return card;
    }

    private void pickLayout(ChineseLayout layout) {
        prefs.setChineseLayout(layout);
        if (layoutListener != null) layoutListener.onLayoutChanged(layout);
        // After picking, return to main input so the user sees the effect.
        emit(SimeKey.toBack());
    }
}
