package com.shiyu.sime.ime.keyboard;

import com.shiyu.sime.ime.theme.Typography;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.shiyu.sime.QuickPhraseEditActivity;
import com.shiyu.sime.ime.data.ClipboardStore;
import com.shiyu.sime.ime.data.QuickPhraseStore;
import com.shiyu.sime.ime.feedback.InputFeedbacks;

import java.util.List;

/**
 * Unified picker panel with two tabs: 剪切板 (clipboard history) and
 * 常用语 (quick phrases). Mirrors the reference UI layout —
 * <pre>
 *   [tabs (剪切板 | 常用语)]                             [+]
 *   ┌───────────────────────────────────────┐  [✎]  [🗑]
 *   │  entry text                       ⋮   │
 *   └───────────────────────────────────────┘
 *   …more rows…
 * </pre>
 *
 * <p>Tap on the entry card → commit text + exit panel.
 * <p>"+" launches {@link QuickPhraseEditActivity} for adding a phrase
 * (only meaningful on the 常用语 tab).
 * <p>"✎" launches the same activity in edit mode (常用语 only).
 * <p>"🗑" removes the row from the active store.
 * <p>Switching tabs fires {@link Listener#onSwitchTab(Tab)} so the
 * host can swap the IME mode (which causes the panel to be rebuilt
 * with the new tab pre-selected).
 */
public class PickerPanelView extends KeyboardView {

    public enum Tab {
        QUICK_PHRASE,
        CLIPBOARD
    }

    public interface Listener {
        void onPick(String text);
        void onSwitchTab(Tab tab);
        /** "+" tapped on QUICK_PHRASE tab — open in-IME composer for new entry. */
        void onAddPhrase();
        /** "✎" tapped on a QUICK_PHRASE row — open composer in edit mode. */
        void onEditPhrase(int index, String currentText);
    }

    private final Tab activeTab;
    private final QuickPhraseStore phraseStore;
    private final ClipboardStore clipboardStore;
    private LinearLayout listContainer;
    private Listener listener;

    public PickerPanelView(Context context, Tab tab) {
        super(context);
        this.activeTab = tab;
        this.phraseStore = new QuickPhraseStore(context);
        this.clipboardStore = new ClipboardStore(context);
        build();
    }

    public void setListener(Listener l) {
        this.listener = l;
    }

    private void build() {
        // === Header row: tabs + "+" ===
        LinearLayout header = new LinearLayout(getContext());
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(8), dp(4), dp(8), dp(4));
        LayoutParams headerLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, dp(40));
        addView(header, headerLp);

        // Spacer left of tabs (balances the "+" on the right).
        View leftSpacer = new View(getContext());
        leftSpacer.setLayoutParams(new LinearLayout.LayoutParams(
                0, 0, 1f));
        header.addView(leftSpacer);

        // Tab pill: [剪切板 | 常用语]. Active tab uses accent color.
        LinearLayout tabPill = new LinearLayout(getContext());
        tabPill.setOrientation(LinearLayout.HORIZONTAL);
        tabPill.setBackground(roundedRect(theme.functionKeyBackground, dp(16)));
        tabPill.setPadding(dp(2), dp(2), dp(2), dp(2));
        header.addView(tabPill);

        tabPill.addView(buildTab("剪切板", Tab.CLIPBOARD));
        tabPill.addView(buildTab("常用语", Tab.QUICK_PHRASE));

        View rightSpacer = new View(getContext());
        rightSpacer.setLayoutParams(new LinearLayout.LayoutParams(
                0, 0, 1f));
        header.addView(rightSpacer);

        // Right-side action button:
        //   QUICK_PHRASE tab → "+"  (add a new phrase)
        //   CLIPBOARD    tab → "🗑" (clear all clipboard history)
        TextView actionBtn = new TextView(getContext());
        actionBtn.setGravity(Gravity.CENTER);
        actionBtn.setBackground(makeCircleBg(
                theme.functionKeyBackground, theme.functionKeyBackgroundPressed));
        if (activeTab == Tab.QUICK_PHRASE) {
            actionBtn.setText("+");
            actionBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.DISPLAY);
            actionBtn.setTextColor(theme.keyText);
            InputFeedbacks.wireClick(actionBtn, () -> {
                if (listener != null) listener.onAddPhrase();
            });
        } else {
            actionBtn.setText("🗑");
            actionBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.CALLOUT);
            actionBtn.setTextColor(0xFFE53935);  // red 600
            InputFeedbacks.wireClick(actionBtn, () -> {
                clipboardStore.clearAll();
                renderList();
            });
        }
        LinearLayout.LayoutParams actionLp = new LinearLayout.LayoutParams(
                dp(36), dp(36));
        actionBtn.setLayoutParams(actionLp);
        header.addView(actionBtn);

        // === Body: scrollable list ===
        ScrollView scroll = new ScrollView(getContext());
        LayoutParams scrollLp = new LayoutParams(
                LayoutParams.MATCH_PARENT, 0);
        scrollLp.weight = 1f;
        addView(scroll, scrollLp);

        listContainer = new LinearLayout(getContext());
        listContainer.setOrientation(LinearLayout.VERTICAL);
        listContainer.setPadding(dp(8), dp(4), dp(8), dp(8));
        scroll.addView(listContainer, new ScrollView.LayoutParams(
                LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));

        renderList();
    }

    private View buildTab(String label, Tab tab) {
        boolean active = tab == activeTab;
        TextView tv = new TextView(getContext());
        tv.setText(label);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.SMALL);
        tv.setGravity(Gravity.CENTER);
        tv.setPadding(dp(16), dp(4), dp(16), dp(4));
        tv.setTypeface(null, active ? Typeface.BOLD : Typeface.NORMAL);
        if (active) {
            tv.setTextColor(theme.keyText);
            tv.setBackground(roundedRect(theme.keyBackground, dp(14)));
        } else {
            tv.setTextColor(theme.hintLabelColor);
            tv.setBackgroundColor(Color.TRANSPARENT);
            InputFeedbacks.wireClick(tv, () -> {
                if (listener != null) listener.onSwitchTab(tab);
            });
        }
        return tv;
    }

    private void renderList() {
        listContainer.removeAllViews();
        List<String> items = (activeTab == Tab.QUICK_PHRASE)
                ? phraseStore.getAll()
                : clipboardStore.getAll();
        if (items.isEmpty()) {
            TextView empty = new TextView(getContext());
            empty.setText(activeTab == Tab.QUICK_PHRASE
                    ? "暂无常用语，点击 + 添加"
                    : "剪切板暂无记录\n复制文本后会自动保留最近 "
                            + ClipboardStore.MAX_ITEMS + " 条");
            empty.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.CAPTION);
            empty.setTextColor(theme.hintLabelColor);
            empty.setGravity(Gravity.CENTER);
            empty.setPadding(dp(12), dp(20), dp(12), dp(20));
            listContainer.addView(empty);
            return;
        }
        for (int i = 0; i < items.size(); i++) {
            listContainer.addView(buildRow(i, items.get(i)));
        }
    }

    private View buildRow(int idx, String text) {
        // Outer row holds: card (text) | edit button | delete button.
        LinearLayout row = new LinearLayout(getContext());
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                LayoutParams.MATCH_PARENT, dp(48));
        rowLp.bottomMargin = dp(6);
        row.setLayoutParams(rowLp);

        // Card body: tappable to commit.
        TextView body = new TextView(getContext());
        body.setText(text);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.BODY);
        body.setTextColor(theme.keyText);
        body.setMaxLines(1);
        body.setEllipsize(android.text.TextUtils.TruncateAt.END);
        body.setPadding(dp(12), 0, dp(12), 0);
        body.setGravity(Gravity.CENTER_VERTICAL);
        body.setBackground(makeRowBg(theme.keyBackground, theme.keyBackgroundPressed));
        LinearLayout.LayoutParams bodyLp = new LinearLayout.LayoutParams(
                0, LayoutParams.MATCH_PARENT, 1f);
        body.setLayoutParams(bodyLp);
        body.setClickable(true);
        body.setFocusable(true);
        InputFeedbacks.wireClick(body, () -> {
            if (listener != null) listener.onPick(text);
        });
        row.addView(body);

        // Edit button (常用语 only).
        if (activeTab == Tab.QUICK_PHRASE) {
            TextView edit = new TextView(getContext());
            edit.setText("✎");
            edit.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.TITLE);
            edit.setTextColor(theme.accentColor);
            edit.setGravity(Gravity.CENTER);
            edit.setBackground(makeCircleBg(
                    theme.functionKeyBackground, theme.functionKeyBackgroundPressed));
            LinearLayout.LayoutParams editLp = new LinearLayout.LayoutParams(
                    dp(36), dp(36));
            editLp.leftMargin = dp(6);
            edit.setLayoutParams(editLp);
            edit.setClickable(true);
            edit.setFocusable(true);
            InputFeedbacks.wireClick(edit, () -> {
                if (listener != null) listener.onEditPhrase(idx, text);
            });
            row.addView(edit);
        }

        // Delete button.
        TextView del = new TextView(getContext());
        del.setText("🗑");
        del.setTextSize(TypedValue.COMPLEX_UNIT_SP, Typography.CALLOUT);
        del.setTextColor(0xFFE53935);  // red 600 — matches the reference
        del.setGravity(Gravity.CENTER);
        del.setBackground(makeCircleBg(
                theme.functionKeyBackground, theme.functionKeyBackgroundPressed));
        LinearLayout.LayoutParams delLp = new LinearLayout.LayoutParams(
                dp(36), dp(36));
        delLp.leftMargin = dp(6);
        del.setLayoutParams(delLp);
        del.setClickable(true);
        del.setFocusable(true);
        InputFeedbacks.wireClick(del, () -> {
            if (activeTab == Tab.QUICK_PHRASE) {
                phraseStore.removeAt(idx);
            } else {
                clipboardStore.removeAt(idx);
            }
            renderList();
        });
        row.addView(del);

        return row;
    }

    private void launchAddActivity() {
        Intent intent = new Intent(getContext(), QuickPhraseEditActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        getContext().startActivity(intent);
    }

    private void launchEditActivity(int index, String currentText) {
        Intent intent = new Intent(getContext(), QuickPhraseEditActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(QuickPhraseEditActivity.EXTRA_EDIT_INDEX, index);
        intent.putExtra(QuickPhraseEditActivity.EXTRA_INITIAL_TEXT, currentText);
        getContext().startActivity(intent);
    }

    private StateListDrawable makeRowBg(int normal, int pressed) {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                roundedRect(pressed, dp(10)));
        sl.addState(new int[]{}, roundedRect(normal, dp(10)));
        return sl;
    }

    private StateListDrawable makeCircleBg(int normal, int pressed) {
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_pressed},
                circle(pressed));
        sl.addState(new int[]{}, circle(normal));
        return sl;
    }

    private GradientDrawable roundedRect(int color, int radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setCornerRadius(radiusPx);
        d.setColor(color);
        return d;
    }

    private GradientDrawable circle(int color) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(color);
        return d;
    }
}
