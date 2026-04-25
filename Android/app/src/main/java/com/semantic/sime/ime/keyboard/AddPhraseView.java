package com.semantic.sime.ime.keyboard;

import com.semantic.sime.ime.theme.Typography;
import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.semantic.sime.SimeService;
import com.semantic.sime.ime.compose.ComposeSink;
import com.semantic.sime.ime.data.QuickPhraseStore;
import com.semantic.sime.ime.feedback.InputFeedbacks;
import com.semantic.sime.ime.theme.SimeTheme;

/**
 * Thin composer overlay for adding / editing a quick phrase. Designed
 * to sit BETWEEN the candidates bar and the keyboard so it doesn't eat
 * keyboard space — the regular Chinese keyboard renders below at full
 * size, and the user types into the in-IME buffer via {@link ComposeSink}.
 *
 * <p>Layout (~100dp tall):
 * <pre>
 *   [×]   添加常用语                  [完成]
 *   ┌───────────────────────────────────────┐
 *   │  buffer text + composing preedit       │
 *   └───────────────────────────────────────┘
 *   small hint line
 * </pre>
 */
public class AddPhraseView extends LinearLayout {

    public interface OnDismissListener {
        /** User tapped × or 完成 — host should remove the overlay. */
        void onDismiss();
    }

    /** Static one-shot register of next add/edit context.
     *  InputView writes here right before showing the overlay. */
    private static String pendingSeed = "";
    private static int pendingEditIndex = -1;

    public static void setSeed(String text, int editIndex) {
        pendingSeed = text == null ? "" : text;
        pendingEditIndex = editIndex;
    }

    private final SimeTheme theme;
    private final StringBuilder buffer = new StringBuilder();
    private String preedit = "";
    private final int editIndex;

    private TextView bufferDisplay;
    private OnDismissListener dismissListener;

    /** Cursor blink state — toggled every 500ms by {@link #cursorBlink}. */
    private boolean cursorVisible = true;
    private final Runnable cursorBlink = new Runnable() {
        @Override public void run() {
            cursorVisible = !cursorVisible;
            refreshDisplay();
            postDelayed(this, 500);
        }
    };

    public AddPhraseView(Context context) {
        super(context);
        this.theme = SimeTheme.fromContext(context);
        // Pull seed; one-shot.
        this.buffer.append(pendingSeed);
        this.editIndex = pendingEditIndex;
        pendingSeed = "";
        pendingEditIndex = -1;
        setOrientation(VERTICAL);
        setBackgroundColor(theme.keyboardBackground);
        setPadding(0, dp(2), 0, dp(8));
        build();
    }

    public void setOnDismissListener(OnDismissListener l) {
        this.dismissListener = l;
    }

    private void build() {
        // ===== Header =====
        LinearLayout header = new LinearLayout(getContext());
        header.setOrientation(HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(8), 0, dp(8), 0);
        LayoutParams hLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, dp(36));
        addView(header, hLp);

        TextView cancel = new TextView(getContext());
        cancel.setText("×");
        cancel.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.TITLE);
        cancel.setGravity(Gravity.CENTER);
        cancel.setTextColor(theme.keyText);
        cancel.setBackground(makeCircleBg(
                theme.functionKeyBackground, theme.functionKeyBackgroundPressed));
        cancel.setLayoutParams(new LinearLayout.LayoutParams(dp(30), dp(30)));
        InputFeedbacks.wireClick(cancel, this::onCancel);
        header.addView(cancel);

        TextView title = new TextView(getContext());
        title.setText(editIndex >= 0 ? "编辑常用语" : "添加常用语");
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.SMALL);
        title.setTextColor(theme.keyText);
        title.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(
                0, LayoutParams.WRAP_CONTENT, 1f);
        title.setLayoutParams(titleLp);
        header.addView(title);

        TextView done = new TextView(getContext());
        done.setText("完成");
        done.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.CAPTION);
        done.setGravity(Gravity.CENTER);
        done.setTypeface(null, Typeface.BOLD);
        done.setTextColor(0xFFFFFFFF);
        done.setBackground(roundedRect(0xFF4A90E2, dp(14)));
        done.setPadding(dp(14), dp(4), dp(14), dp(4));
        InputFeedbacks.wireClick(done, this::onDone);
        header.addView(done);

        // ===== Buffer card =====
        bufferDisplay = new TextView(getContext());
        bufferDisplay.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.CALLOUT);
        bufferDisplay.setTextColor(theme.keyText);
        bufferDisplay.setBackground(roundedRect(0xFFFFFFFF, dp(8)));
        bufferDisplay.setPadding(dp(14), dp(10), dp(14), dp(10));
        bufferDisplay.setMaxLines(2);
        bufferDisplay.setEllipsize(android.text.TextUtils.TruncateAt.END);
        bufferDisplay.setGravity(Gravity.TOP | Gravity.START);
        LayoutParams bufLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, dp(56));
        bufLp.leftMargin = dp(12);
        bufLp.rightMargin = dp(12);
        bufLp.topMargin = dp(2);
        addView(bufferDisplay, bufLp);

        refreshDisplay();
    }

    private void refreshDisplay() {
        if (bufferDisplay == null) return;

        android.text.SpannableStringBuilder sb =
                new android.text.SpannableStringBuilder();
        int caretColor = cursorVisible
                ? theme.accentColor
                : android.graphics.Color.TRANSPARENT;
        boolean empty = buffer.length() == 0 && preedit.isEmpty();
        if (empty) {
            int caretStart = sb.length();
            sb.append("\u200B");  // zero-width placeholder for the span
            sb.setSpan(new CaretSpan(caretColor),
                    caretStart, sb.length(),
                    android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            int placeholderStart = sb.length();
            sb.append("输入常用语");
            sb.setSpan(new android.text.style.ForegroundColorSpan(
                            theme.hintLabelColor),
                    placeholderStart, sb.length(),
                    android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            bufferDisplay.setText(sb);
        } else {
            sb.append(buffer.toString());
            if (!preedit.isEmpty()) {
                int start = sb.length();
                sb.append(preedit);
                sb.setSpan(new android.text.style.ForegroundColorSpan(
                                theme.hintLabelColor),
                        start, sb.length(),
                        android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
                sb.setSpan(new android.text.style.UnderlineSpan(),
                        start, sb.length(),
                        android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            }
            int caretStart = sb.length();
            sb.append("\u200B");
            sb.setSpan(new CaretSpan(caretColor),
                    caretStart, sb.length(),
                    android.text.Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            bufferDisplay.setText(sb);
            bufferDisplay.setTextColor(theme.keyText);
        }
    }

    /**
     * Custom span that draws a thin vertical line as the caret.
     * Replaces the placeholder ZWSP character so the caret has a
     * known small width (no font side-bearings making it look like
     * an extra space).
     */
    private final class CaretSpan
            extends android.text.style.ReplacementSpan {
        private final int color;
        private final float widthPx = dp(2);
        CaretSpan(int color) { this.color = color; }

        @Override
        public int getSize(android.graphics.Paint paint,
                           CharSequence text, int start, int end,
                           android.graphics.Paint.FontMetricsInt fm) {
            if (fm != null) {
                android.graphics.Paint.FontMetricsInt pm =
                        paint.getFontMetricsInt();
                fm.ascent = pm.ascent;
                fm.descent = pm.descent;
                fm.top = pm.top;
                fm.bottom = pm.bottom;
            }
            return Math.round(widthPx);
        }

        @Override
        public void draw(android.graphics.Canvas canvas,
                         CharSequence text, int start, int end,
                         float x, int top, int y, int bottom,
                         android.graphics.Paint paint) {
            android.graphics.Paint p = new android.graphics.Paint();
            p.setColor(color);
            p.setStyle(android.graphics.Paint.Style.FILL);
            p.setAntiAlias(true);
            android.graphics.Paint.FontMetricsInt fm =
                    paint.getFontMetricsInt();
            // Cap the caret to the text glyph height so it doesn't
            // overshoot above/below the line.
            float caretTop = y + fm.ascent + dp(1);
            float caretBottom = y + fm.descent - dp(1);
            canvas.drawRect(x, caretTop, x + widthPx, caretBottom, p);
        }
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        Context c = getContext();
        if (c instanceof SimeService) {
            ((SimeService) c).setComposeSink(sink);
        }
        // Start cursor blink.
        cursorVisible = true;
        postDelayed(cursorBlink, 500);
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        Context c = getContext();
        if (c instanceof SimeService) {
            ((SimeService) c).setComposeSink(null);
        }
        removeCallbacks(cursorBlink);
    }

    private final ComposeSink sink = new ComposeSink() {
        @Override public void onCommit(String text) {
            buffer.append(text);
            preedit = "";
            post(AddPhraseView.this::refreshDisplay);
        }
        @Override public void onDelete(int count) {
            int len = buffer.length();
            buffer.delete(Math.max(0, len - count), len);
            post(AddPhraseView.this::refreshDisplay);
        }
        @Override public void onPreedit(String pe) {
            preedit = pe == null ? "" : pe;
            post(AddPhraseView.this::refreshDisplay);
        }
    };

    private void onCancel() {
        if (dismissListener != null) dismissListener.onDismiss();
    }

    private void onDone() {
        String finalText = buffer.toString().trim();
        if (!finalText.isEmpty()) {
            QuickPhraseStore store = new QuickPhraseStore(getContext());
            if (editIndex >= 0) {
                store.updateAt(editIndex, finalText);
            } else {
                store.add(finalText);
            }
        }
        if (dismissListener != null) dismissListener.onDismiss();
    }

    private int dp(int v) {
        return (int) android.util.TypedValue.applyDimension(
                android.util.TypedValue.COMPLEX_UNIT_DIP,
                v, getResources().getDisplayMetrics());
    }

    private StateListDrawable makeCircleBg(int normal, int pressed) {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed}, circle(pressed));
        sl.addState(new int[]{}, circle(normal));
        return sl;
    }

    private GradientDrawable circle(int color) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(color);
        return d;
    }

    private GradientDrawable roundedRect(int color, int radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(radiusPx);
        d.setColor(color);
        return d;
    }
}
