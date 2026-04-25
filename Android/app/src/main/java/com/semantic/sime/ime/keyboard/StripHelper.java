package com.semantic.sime.ime.keyboard;

import com.semantic.sime.ime.theme.Typography;
import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.semantic.sime.ime.feedback.InputFeedbacks;
import com.semantic.sime.ime.theme.SimeTheme;

/**
 * Shared factory for pinyin-alt strip cells used by both
 * {@link T9KeyboardView} and
 * {@link com.semantic.sime.ime.candidates.ExpandedCandidatesView}.
 */
public final class StripHelper {

    private StripHelper() {}

    /**
     * Create a single strip cell.
     *
     * @param ctx         Android context (for resources)
     * @param theme       active theme palette
     * @param label       text shown in the cell
     * @param functionBg  {@code true} → function-key colors
     *                    ({@link SimeTheme#functionKeyBackground} /
     *                    {@link SimeTheme#keyTextFunction});
     *                    {@code false} → normal key colors
     *                    ({@link SimeTheme#keyBackground} /
     *                    {@link SimeTheme#keyText})
     * @param onClick     click action
     * @param heightDp    fixed cell height in dp
     */
    public static TextView makeStripCell(Context ctx, SimeTheme theme,
                                         String label, boolean functionBg,
                                         Runnable onClick, int heightDp) {
        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.SMALL);
        tv.setTextColor(functionBg ? theme.keyTextFunction : theme.keyText);
        tv.setBackground(functionBg
                ? makeFunctionSelector(ctx, theme)
                : makeNormalSelector(ctx, theme));
        tv.setClickable(true);
        tv.setFocusable(true);
        tv.setSingleLine(true);
        int h = dp(ctx, heightDp);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, h);
        int m = dp(ctx, 3);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        InputFeedbacks.wireClick(tv, onClick);
        return tv;
    }

    private static StateListDrawable makeNormalSelector(Context ctx, SimeTheme theme) {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                roundedRect(ctx, theme.keyBackgroundPressed));
        sl.addState(new int[]{}, roundedRect(ctx, theme.keyBackground));
        return sl;
    }

    private static StateListDrawable makeFunctionSelector(Context ctx, SimeTheme theme) {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                roundedRect(ctx, theme.functionKeyBackgroundPressed));
        sl.addState(new int[]{}, roundedRect(ctx, theme.functionKeyBackground));
        return sl;
    }

    private static GradientDrawable roundedRect(Context ctx, int color) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(dp(ctx, 8));
        d.setColor(color);
        return d;
    }

    private static int dp(Context ctx, int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, ctx.getResources().getDisplayMetrics());
    }
}
