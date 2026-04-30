package com.shiyu.sime.ime.keyboard;

import com.shiyu.sime.ime.theme.Typography;
import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.shiyu.sime.ime.feedback.InputFeedbacks;
import com.shiyu.sime.ime.theme.SimeTheme;

/**
 * Shared factory for pinyin-alt strip cells used by both
 * {@link T9KeyboardView} and
 * {@link com.shiyu.sime.ime.candidates.ExpandedCandidatesView}.
 */
public final class StripHelper {

    private StripHelper() {}

    public static TextView makeStripCell(Context ctx, SimeTheme theme,
                                         String label, Runnable onClick,
                                         int heightDp) {
        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setGravity(Gravity.CENTER);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.SMALL);
        tv.setTextColor(theme.keyTextFunction);
        tv.setBackground(makeFunctionSelector(ctx, theme));
        tv.setClickable(true);
        tv.setFocusable(true);
        tv.setSingleLine(true);
        int h = dp(ctx, heightDp);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, h);
        int m = dp(ctx, 2);
        lp.setMargins(m, m, m, m);
        tv.setLayoutParams(lp);
        InputFeedbacks.wireClick(tv, onClick);
        return tv;
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
