package com.semantic.sime;

import com.semantic.sime.ime.theme.Typography;
import android.app.Activity;
import android.content.Intent;
import android.database.ContentObserver;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.text.InputType;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.List;

/**
 * Launcher / setup screen. Walks the user through the two-step Android
 * IME activation flow and shows the current status of each step:
 *
 * <ol>
 *   <li>Enable 是语 in <em>Languages &amp; Input → On-screen keyboard</em>.</li>
 *   <li>Select 是语 as the current input method (status-bar picker).</li>
 * </ol>
 *
 * A small "Try it" EditText at the bottom lets the user verify typing
 * works without leaving this activity.
 */
public class SettingsActivity extends Activity {

    private static final String SELF_PACKAGE = "com.semantic.sime";

    // Soft brand color (kept here rather than as a resource so the activity
    // is self-contained without requiring a colors.xml entry).
    private static final int ACCENT = 0xFF4A90E2;
    private static final int OK = 0xFF4CAF50;
    private static final int PENDING = 0xFFB0B0B0;

    private TextView step1Status;
    private TextView step2Status;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private ContentObserver imeObserver;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setBackgroundColor(0xFFF6F8FB);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        // Note: don't use fitsSystemWindows here — it overrides our
        // padding with the status-bar inset. We apply the offset manually.
        root.setPadding(dp(28), dp(96), dp(28), dp(28));
        scroll.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // === Title ===
        TextView title = new TextView(this);
        title.setText("是语输入法");
        title.setTextSize(Typography.HEADLINE);
        title.setTextColor(0xFF202124);
        title.setTypeface(null, Typeface.BOLD);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView subtitle = new TextView(this);
        subtitle.setText("两步启用，开箱即用");
        subtitle.setTextSize(Typography.SMALL);
        subtitle.setTextColor(0xFF606770);
        subtitle.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams subLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        subLp.topMargin = dp(8);
        subLp.bottomMargin = dp(32);
        root.addView(subtitle, subLp);

        // === Step 1 card ===
        step1Status = new TextView(this);
        root.addView(buildStepCard(
                "1",
                "在系统中启用",
                "在 系统设置 → 语言和输入法 → 屏幕键盘 中勾选 是语输入法。",
                "去启用",
                v -> startActivity(new Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)),
                step1Status));

        // === Step 2 card ===
        step2Status = new TextView(this);
        root.addView(buildStepCard(
                "2",
                "切换为当前输入法",
                "切换输入法（也可以下拉通知栏的“选择输入法”入口）。",
                "切换",
                v -> {
                    InputMethodManager imm = (InputMethodManager)
                            getSystemService(INPUT_METHOD_SERVICE);
                    if (imm != null) imm.showInputMethodPicker();
                },
                step2Status));

        // === Try-it card ===
        LinearLayout tryCard = makeCard();
        TextView tryTitle = new TextView(this);
        tryTitle.setText("3  试一试");
        tryTitle.setTextSize(Typography.CALLOUT);
        tryTitle.setTypeface(null, Typeface.BOLD);
        tryTitle.setTextColor(0xFF202124);
        tryCard.addView(tryTitle);

        TextView tryDesc = new TextView(this);
        tryDesc.setText("点击下方文本框，确认是语已激活并能正常输入。");
        tryDesc.setTextSize(Typography.CAPTION);
        tryDesc.setTextColor(0xFF606770);
        LinearLayout.LayoutParams descLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        descLp.topMargin = dp(6);
        tryCard.addView(tryDesc, descLp);

        EditText tryEdit = new EditText(this);
        tryEdit.setHint("在这里输入文字…");
        tryEdit.setTextSize(Typography.BODY);
        tryEdit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        tryEdit.setMinLines(2);
        tryEdit.setBackground(rounded(Color.WHITE, dp(6)));
        tryEdit.setPadding(dp(12), dp(10), dp(12), dp(10));
        LinearLayout.LayoutParams editLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        editLp.topMargin = dp(12);
        tryCard.addView(tryEdit, editLp);

        // Make sure the EditText is visible above the keyboard. Some
        // ROMs ignore adjustResize so we trigger the scroll explicitly
        // when focus arrives, after the IME has had a moment to appear.
        tryEdit.setOnFocusChangeListener((v, hasFocus) -> {
            if (!hasFocus) return;
            v.postDelayed(() -> {
                int[] loc = new int[2];
                v.getLocationInWindow(loc);
                scroll.smoothScrollTo(0, scroll.getChildAt(0).getHeight());
            }, 250);
        });

        root.addView(tryCard);

        // === Footer ===
        TextView footer = new TextView(this);
        footer.setText("输入法在本机离线运行，不上传任何输入或剪贴板内容。");
        footer.setTextSize(Typography.CAPTION);
        footer.setTextColor(0xFF888888);
        footer.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams footerLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        footerLp.topMargin = dp(28);
        root.addView(footer, footerLp);

        setContentView(scroll);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshStatus();
        // The system commits DEFAULT_INPUT_METHOD asynchronously after
        // the picker dialog dismisses, so the very first onResume reads
        // a stale value. Re-poll a few times to catch up, and also
        // register a ContentObserver for live updates.
        for (long delay : new long[]{200, 500, 1200}) {
            mainHandler.postDelayed(this::refreshStatus, delay);
        }
        if (imeObserver == null) {
            imeObserver = new ContentObserver(mainHandler) {
                @Override public void onChange(boolean selfChange, Uri uri) {
                    refreshStatus();
                }
            };
            getContentResolver().registerContentObserver(
                    Settings.Secure.getUriFor(Settings.Secure.DEFAULT_INPUT_METHOD),
                    false, imeObserver);
            getContentResolver().registerContentObserver(
                    Settings.Secure.getUriFor("enabled_input_methods"),
                    false, imeObserver);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (imeObserver != null) {
            getContentResolver().unregisterContentObserver(imeObserver);
            imeObserver = null;
        }
        mainHandler.removeCallbacksAndMessages(null);
    }

    private void refreshStatus() {
        boolean enabled = isImeEnabled();
        boolean selected = isImeSelected();
        applyStatus(step1Status, enabled, "已启用", "未启用");
        applyStatus(step2Status, selected, "当前正在使用", "尚未切换");
    }

    private static void applyStatus(TextView tv, boolean ok,
                                    String okText, String pendingText) {
        if (ok) {
            tv.setText("✓ " + okText);
            tv.setTextColor(OK);
        } else {
            tv.setText("○ " + pendingText);
            tv.setTextColor(PENDING);
        }
    }

    private boolean isImeEnabled() {
        InputMethodManager imm = (InputMethodManager)
                getSystemService(INPUT_METHOD_SERVICE);
        if (imm == null) return false;
        List<InputMethodInfo> list = imm.getEnabledInputMethodList();
        if (list == null) return false;
        for (InputMethodInfo info : list) {
            if (SELF_PACKAGE.equals(info.getPackageName())) return true;
        }
        return false;
    }

    private boolean isImeSelected() {
        String defaultIme = Settings.Secure.getString(
                getContentResolver(), Settings.Secure.DEFAULT_INPUT_METHOD);
        if (TextUtils.isEmpty(defaultIme)) return false;
        return defaultIme.startsWith(SELF_PACKAGE + "/");
    }

    // ===== UI helpers =====

    private LinearLayout buildStepCard(String num, String title, String desc,
                                       String btnLabel,
                                       View.OnClickListener onClick,
                                       TextView statusView) {
        LinearLayout card = makeCard();

        TextView head = new TextView(this);
        head.setText(num + "  " + title);
        head.setTextSize(Typography.CALLOUT);
        head.setTypeface(null, Typeface.BOLD);
        head.setTextColor(0xFF202124);
        card.addView(head);

        TextView body = new TextView(this);
        body.setText(desc);
        body.setTextSize(Typography.CAPTION);
        body.setTextColor(0xFF606770);
        LinearLayout.LayoutParams bodyLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        bodyLp.topMargin = dp(6);
        card.addView(body, bodyLp);

        // Bottom row: status (left) + action button (right).
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        rowLp.topMargin = dp(14);
        card.addView(row, rowLp);

        statusView.setTextSize(Typography.CAPTION);
        LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        statusView.setLayoutParams(statusLp);
        row.addView(statusView);

        TextView btn = new TextView(this);
        btn.setText(btnLabel);
        btn.setTextSize(Typography.SMALL);
        btn.setTextColor(Color.WHITE);
        btn.setTypeface(null, Typeface.BOLD);
        btn.setGravity(Gravity.CENTER);
        btn.setBackground(rounded(ACCENT, dp(20)));
        btn.setPadding(dp(20), dp(8), dp(20), dp(8));
        btn.setClickable(true);
        btn.setFocusable(true);
        btn.setOnClickListener(onClick);
        row.addView(btn);

        return card;
    }

    private LinearLayout makeCard() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(rounded(Color.WHITE, dp(12)));
        c.setPadding(dp(20), dp(18), dp(20), dp(18));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(14);
        c.setLayoutParams(lp);
        return c;
    }

    private GradientDrawable rounded(int color, int radius) {
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.RECTANGLE);
        d.setColor(color);
        d.setCornerRadius(radius);
        return d;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }
}
