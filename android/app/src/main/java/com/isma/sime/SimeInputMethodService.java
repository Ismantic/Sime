package com.isma.sime;

import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.inputmethodservice.InputMethodService;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.UnderlineSpan;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.TextView;

import android.content.SharedPreferences;
import android.util.Log;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class SimeInputMethodService extends InputMethodService {
    private static final String TAG = "SimeIME";
    private static final int MAX_CANDIDATES = 36;
    private static final int PAGE_SIZE = 9;

    // Backspace repeat
    private static final long REPEAT_START_DELAY = 400L;
    private static final long REPEAT_INTERVAL = 50L;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mBackspaceRepeating = false;
    private boolean mLongPressHandled = false;
    private Runnable mPendingLongPress = null;
    private final Runnable mRepeatBackspace = new Runnable() {
        @Override
        public void run() {
            handleBackspace();
            mHandler.postDelayed(this, REPEAT_INTERVAL);
        }
    };

    private SimeEngine mEngine;
    private StringBuilder mPreedit = new StringBuilder();
    private int mCursor = 0;
    private boolean mChineseMode = true;

    // Layout mode: true = T9 (九宫格), false = QWERTY (全键盘)
    // Stored in SharedPreferences
    private boolean mT9LayoutMode = false;

    private enum KeyboardMode { QWERTY, T9, NUMBER, SYMBOL }
    private KeyboardMode mKeyboardMode = KeyboardMode.QWERTY;

    // Candidate state
    private List<SimeEngine.Candidate> mCandidates = new ArrayList<>();
    private int mCandidatePage = 0;

    // UI refs
    private View mInputView;
    private LinearLayout mCandidateContainer;
    private HorizontalScrollView mCandidateScroll;
    private TextView mPreeditView;
    private TextView mToggleKey;

    // Key popup
    private PopupWindow mPopup;
    private TextView mPopupText;

    // Symbol keyboard state
    private int mSymbolCategory = 0;
    private int mSymbolPage = 0;
    private static final int SYMBOLS_PER_PAGE = 24; // 4 rows x 6 cols
    private static final int SYMBOL_COLS = 6;
    private static final int SYMBOL_ROWS = 4;

    // Symbol data
    private static final LinkedHashMap<String, String[]> SYMBOL_CATEGORIES = new LinkedHashMap<>();
    static {
        SYMBOL_CATEGORIES.put("常用", new String[]{
            "，", "。", "？", "！", "、", "：", "；",
            "\u201C", "\u201D", "\u2018", "\u2019", "……",
            "——", "（", "）", "【", "】",
            "《", "》", "「", "」", "～", "·", "…",
            "@", "#", "￥", "%", "&", "*", "+", "-", "=", "/", "\\", "|",
            "^", "_", "<", ">", "{", "}", "[", "]", ".", ",", "?", "!",
            ":", ";", "\"", "'", "`", "~", "$", "€", "£", "¥"
        });
        SYMBOL_CATEGORIES.put("标点", new String[]{
            "。", "，", "、", "；", "：", "？", "！", ".", ",", ";", ":", "?",
            "!", "…", "……", "—", "——", "～", "~", "-", "–", "―",
            "·", "•", "‧", "﹒", "°", "′", "″", "‰", "‱", "※"
        });
        SYMBOL_CATEGORIES.put("括号", new String[]{
            "（", "）", "【", "】", "《", "》", "「", "」", "『", "』", "〔", "〕",
            "〈", "〉", "｛", "｝", "〖", "〗", "〘", "〙", "〚", "〛",
            "(", ")", "[", "]", "{", "}", "<", ">", "⟨", "⟩"
        });
        SYMBOL_CATEGORIES.put("数学", new String[]{
            "+", "-", "×", "÷", "=", "≠", "≈", "≤", "≥", "<", ">", "±",
            "∞", "√", "∑", "∏", "∫", "∂", "∆", "∇", "π", "μ", "α", "β",
            "γ", "δ", "θ", "λ", "σ", "φ", "ω", "Ω", "°", "%", "‰", "‱"
        });
        SYMBOL_CATEGORIES.put("序号", new String[]{
            "①", "②", "③", "④", "⑤", "⑥", "⑦", "⑧", "⑨", "⑩",
            "⑪", "⑫", "⑬", "⑭", "⑮", "⑯", "⑰", "⑱", "⑲", "⑳",
            "㈠", "㈡", "㈢", "㈣", "㈤", "㈥", "㈦", "㈧", "㈨", "㈩",
            "Ⅰ", "Ⅱ", "Ⅲ", "Ⅳ", "Ⅴ", "Ⅵ", "Ⅶ", "Ⅷ", "Ⅸ", "Ⅹ",
            "ⅰ", "ⅱ", "ⅲ", "ⅳ", "ⅴ", "ⅵ", "ⅶ", "ⅷ", "ⅸ", "ⅹ"
        });
        SYMBOL_CATEGORIES.put("特殊", new String[]{
            "♠", "♣", "♥", "♦", "★", "☆", "■", "□", "▲", "△", "●", "○",
            "◆", "◇", "▶", "◀", "↑", "↓", "←", "→", "↔", "↕",
            "✓", "✗", "✦", "✧", "❤", "☺", "☻", "♪", "♫", "☀",
            "☁", "☂", "⚡", "❄", "⭐", "✨", "🔥", "💯", "✅", "❌"
        });
    }

    private static String chinesePunc(String key) {
        switch (key) {
            case "comma":  return "，";
            case "period": return "。";
            default:       return null;
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mEngine = new SimeEngine();
        new Thread(() -> {
            boolean ok = mEngine.init(getApplicationContext());
            Log.i(TAG, "Engine init: " + ok);
        }).start();
        loadLayoutPref();
        initPopup();
    }

    private void loadLayoutPref() {
        SharedPreferences prefs = getSharedPreferences(SettingsActivity.PREFS_NAME, MODE_PRIVATE);
        mT9LayoutMode = prefs.getInt(SettingsActivity.KEY_LAYOUT_MODE, SettingsActivity.MODE_QWERTY) == SettingsActivity.MODE_T9;
        // Set initial keyboard based on preference and language
        mKeyboardMode = getMainKeyboardMode();
    }

    private void saveLayoutPref() {
        SharedPreferences prefs = getSharedPreferences(SettingsActivity.PREFS_NAME, MODE_PRIVATE);
        prefs.edit().putInt(SettingsActivity.KEY_LAYOUT_MODE,
            mT9LayoutMode ? SettingsActivity.MODE_T9 : SettingsActivity.MODE_QWERTY).apply();
    }

    /** Returns the correct main keyboard based on layout mode + language */
    private KeyboardMode getMainKeyboardMode() {
        if (mChineseMode && mT9LayoutMode) {
            return KeyboardMode.T9;
        }
        return KeyboardMode.QWERTY; // English is always QWERTY
    }

    private void initPopup() {
        View popupView = LayoutInflater.from(this).inflate(R.layout.key_popup, null);
        mPopupText = popupView.findViewById(R.id.popup_text);
        mPopup = new PopupWindow(popupView, dp(56), dp(70));
        mPopup.setClippingEnabled(false);
        mPopup.setAnimationStyle(0);
    }

    @Override
    public View onCreateInputView() {
        return inflateKeyboard();
    }

    private View inflateKeyboard() {
        int layoutRes;
        switch (mKeyboardMode) {
            case T9:     layoutRes = R.layout.input_view_t9; break;
            case NUMBER:  layoutRes = R.layout.input_view_number; break;
            case SYMBOL: layoutRes = R.layout.input_view_symbol; break;
            default:     layoutRes = R.layout.input_view; break;
        }
        mInputView = getLayoutInflater().inflate(layoutRes, null);

        if (mKeyboardMode == KeyboardMode.SYMBOL) {
            setupSymbolKeyboard();
        } else if (mKeyboardMode == KeyboardMode.NUMBER) {
            // Number keyboard has no preedit/candidates
            mPreeditView = null;
            mCandidateContainer = null;
            mCandidateScroll = null;
            mToggleKey = null;
        } else {
            mPreeditView = mInputView.findViewById(R.id.preedit);
            mCandidateContainer = mInputView.findViewById(R.id.candidate_container);
            mCandidateScroll = mInputView.findViewById(R.id.candidate_scroll);
            mToggleKey = mInputView.findViewById(R.id.key_toggle);
            updateToggleLabel();
            // Update layout toggle button label
            TextView layoutKey = mInputView.findViewById(R.id.key_layout);
            if (layoutKey != null) {
                layoutKey.setText(mT9LayoutMode ? "26" : "9");
            }
        }

        // Set up touch listeners for all tagged views
        setupTouchListeners(mInputView);
        return mInputView;
    }

    /**
     * Recursively find all views with tags and set up touch listeners.
     * This replaces the old onClick approach with proper touch handling
     * for haptic feedback, key popup, and long press backspace.
     */
    private void setupTouchListeners(View view) {
        if (view.getTag() instanceof String) {
            String tag = (String) view.getTag();
            view.setClickable(true);
            view.setOnTouchListener(new KeyTouchListener(tag, view));
        }
        if (view instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) view;
            for (int i = 0; i < vg.getChildCount(); i++) {
                setupTouchListeners(vg.getChildAt(i));
            }
        }
    }

    private class KeyTouchListener implements View.OnTouchListener {
        private final String tag;
        private final View keyView;
        private boolean moved = false;
        private float downX, downY;

        KeyTouchListener(String tag, View keyView) {
            this.tag = tag;
            this.keyView = keyView;
        }

        @Override
        public boolean onTouch(View v, MotionEvent event) {
            switch (event.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    moved = false;
                    downX = event.getRawX();
                    downY = event.getRawY();
                    v.setPressed(true);
                    // Haptic feedback
                    v.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP,
                        HapticFeedbackConstants.FLAG_IGNORE_GLOBAL_SETTING);
                    // Show popup for letter keys
                    showKeyPopup(v, tag);
                    // Long press handling
                    mLongPressHandled = false;
                    if (mPendingLongPress != null) {
                        mHandler.removeCallbacks(mPendingLongPress);
                    }
                    if ("backspace".equals(tag)) {
                        mBackspaceRepeating = false;
                        mPendingLongPress = () -> {
                            mBackspaceRepeating = true;
                            mLongPressHandled = true;
                            mRepeatBackspace.run();
                        };
                        mHandler.postDelayed(mPendingLongPress, REPEAT_START_DELAY);
                    } else if ("comma".equals(tag)) {
                        mPendingLongPress = () -> {
                            mLongPressHandled = true;
                            v.setPressed(false);
                            switchKeyboard(KeyboardMode.SYMBOL);
                        };
                        mHandler.postDelayed(mPendingLongPress, REPEAT_START_DELAY);
                    } else if ("period".equals(tag)) {
                        mPendingLongPress = () -> {
                            mLongPressHandled = true;
                            v.setPressed(false);
                            switchKeyboard(KeyboardMode.SYMBOL);
                        };
                        mHandler.postDelayed(mPendingLongPress, REPEAT_START_DELAY);
                    }
                    return true;

                case MotionEvent.ACTION_MOVE:
                    float dx = event.getRawX() - downX;
                    float dy = event.getRawY() - downY;
                    if (Math.abs(dx) > dp(25) || Math.abs(dy) > dp(25)) {
                        moved = true;
                        dismissPopup();
                    }
                    return true;

                case MotionEvent.ACTION_UP:
                    v.setPressed(false);
                    dismissPopup();
                    if (mPendingLongPress != null) {
                        mHandler.removeCallbacks(mPendingLongPress);
                        mPendingLongPress = null;
                    }
                    if ("backspace".equals(tag)) {
                        mHandler.removeCallbacks(mRepeatBackspace);
                        if (mBackspaceRepeating) {
                            mBackspaceRepeating = false;
                            return true;
                        }
                    }
                    if (!moved && !mLongPressHandled) {
                        handleKeyPress(tag);
                    }
                    return true;

                case MotionEvent.ACTION_CANCEL:
                    v.setPressed(false);
                    dismissPopup();
                    if (mPendingLongPress != null) {
                        mHandler.removeCallbacks(mPendingLongPress);
                        mPendingLongPress = null;
                    }
                    mHandler.removeCallbacks(mRepeatBackspace);
                    mBackspaceRepeating = false;
                    return true;
            }
            return false;
        }
    }

    private void showKeyPopup(View anchor, String tag) {
        // Only show popup for single character keys (letters, digits)
        if (tag.length() == 1 && Character.isLetter(tag.charAt(0))) {
            mPopupText.setText(tag.toUpperCase());
            mPopup.dismiss();
            // Position popup above the key
            int[] loc = new int[2];
            anchor.getLocationInWindow(loc);
            int x = loc[0] + (anchor.getWidth() - dp(56)) / 2;
            int y = loc[1] - dp(70);
            mPopup.showAtLocation(anchor, Gravity.NO_GRAVITY, x, y);
        }
    }

    private void dismissPopup() {
        if (mPopup != null && mPopup.isShowing()) {
            mPopup.dismiss();
        }
    }

    private void switchKeyboard(KeyboardMode newMode) {
        // Commit any pending preedit
        if (mPreedit.length() > 0) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(mPreedit.toString(), 1);
            resetComposition();
        }
        mKeyboardMode = newMode;
        setInputView(inflateKeyboard());
    }

    @Override
    public void onStartInput(EditorInfo attribute, boolean restarting) {
        super.onStartInput(attribute, restarting);
        resetComposition();
        // Reload preference in case user changed it in settings
        boolean wasT9 = mT9LayoutMode;
        loadLayoutPref();
        if (wasT9 != mT9LayoutMode) {
            mKeyboardMode = getMainKeyboardMode();
            setInputView(inflateKeyboard());
        }
    }

    @Override
    public void onFinishInput() {
        super.onFinishInput();
        resetComposition();
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        mHandler.removeCallbacksAndMessages(null);
        dismissPopup();
    }

    private void handleKeyPress(String tag) {
        if (tag == null) return;

        switch (tag) {
            case "backspace":
                handleBackspace();
                break;
            case "space":
                handleSpace();
                break;
            case "enter":
                handleEnter();
                break;
            case "toggle":
                handleToggle();
                break;
            case "layout_toggle":
                handleLayoutToggle();
                break;
            case "num_open":
                switchKeyboard(KeyboardMode.NUMBER);
                break;
            case "num_back":
                switchKeyboard(getMainKeyboardMode());
                break;
            case "sym_open":
                // #+= button: go to full symbol keyboard
                switchKeyboard(KeyboardMode.SYMBOL);
                break;
            case "sym_back":
                // Back to main keyboard
                switchKeyboard(getMainKeyboardMode());
                break;
            case "comma":
            case "period":
                handlePunctuation(tag);
                break;
            default:
                if (tag.startsWith("t9_")) {
                    handleT9Digit(tag.substring(3));
                } else if (tag.startsWith("num_")) {
                    // Number key: num_0 through num_9
                    handleSymbolInput(tag.substring(4));
                } else if (tag.startsWith("sym_")) {
                    // Symbol key: sym_<symbol text>
                    handleSymbolInput(tag.substring(4));
                } else if (tag.length() == 1 && tag.charAt(0) >= 'a' && tag.charAt(0) <= 'z') {
                    handleLetter(tag);
                }
                break;
        }
    }

    // === Key handlers ===

    private void handleLetter(String letter) {
        if (!mChineseMode) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(letter, 1);
            return;
        }
        mPreedit.insert(mCursor, letter);
        mCursor += letter.length();
        mCandidatePage = 0;
        updateUI();
    }

    private void handleT9Digit(String digit) {
        if (!mChineseMode) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(digit, 1);
            return;
        }
        if ("0".equals(digit)) {
            handleSpace();
            return;
        }
        if ("1".equals(digit)) {
            return;
        }
        mPreedit.append(digit);
        mCursor = mPreedit.length();
        mCandidatePage = 0;
        updateUI();
    }

    private void handleBackspace() {
        if (mPreedit.length() > 0 && mCursor > 0) {
            mPreedit.deleteCharAt(mCursor - 1);
            mCursor--;
            mCandidatePage = 0;
            if (mPreedit.length() == 0) {
                resetComposition();
            } else {
                updateUI();
            }
        } else {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.deleteSurroundingText(1, 0);
        }
    }

    private void handleSpace() {
        if (mPreedit.length() > 0 && !mCandidates.isEmpty()) {
            selectCandidate(0);
        } else {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(" ", 1);
        }
    }

    private void handleEnter() {
        if (mPreedit.length() > 0) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(mPreedit.toString(), 1);
            resetComposition();
        } else {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) {
                ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
                ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
            }
        }
    }

    private void handleToggle() {
        if (mPreedit.length() > 0) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(mPreedit.toString(), 1);
            resetComposition();
        }
        mChineseMode = !mChineseMode;
        // Determine correct keyboard for new language
        KeyboardMode target = getMainKeyboardMode();
        if (target != mKeyboardMode) {
            // Need to switch keyboard (e.g. T9→QWERTY or QWERTY→T9)
            mKeyboardMode = target;
            setInputView(inflateKeyboard());
        } else {
            updateToggleLabel();
        }
    }

    private void handleLayoutToggle() {
        if (mPreedit.length() > 0) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(mPreedit.toString(), 1);
            resetComposition();
        }
        // Toggle layout mode and save preference
        mT9LayoutMode = !mT9LayoutMode;
        saveLayoutPref();
        // Switch to the correct keyboard
        mKeyboardMode = getMainKeyboardMode();
        setInputView(inflateKeyboard());
    }

    private void handlePunctuation(String key) {
        if (mChineseMode) {
            if (mPreedit.length() > 0 && !mCandidates.isEmpty()) {
                selectCandidate(0);
            }
            String punc = chinesePunc(key);
            if (punc != null) {
                InputConnection ic = getCurrentInputConnection();
                if (ic != null) ic.commitText(punc, 1);
            }
        } else {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) {
                String c = "comma".equals(key) ? "," : ".";
                ic.commitText(c, 1);
            }
        }
    }

    private void handleSymbolInput(String symbol) {
        InputConnection ic = getCurrentInputConnection();
        if (ic != null) ic.commitText(symbol, 1);
    }

    // === Candidate selection ===

    private void selectCandidate(int indexOnPage) {
        int globalIndex = mCandidatePage * PAGE_SIZE + indexOnPage;
        if (globalIndex >= mCandidates.size()) return;

        SimeEngine.Candidate c = mCandidates.get(globalIndex);
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        ic.commitText(c.text, 1);
        consumePreedit(c.matchedLen);
    }

    private void consumePreedit(int matchedLen) {
        if (mKeyboardMode == KeyboardMode.T9) {
            resetComposition();
        } else {
            if (matchedLen >= mPreedit.length()) {
                resetComposition();
            } else {
                mPreedit.delete(0, matchedLen);
                mCursor = mPreedit.length();
                mCandidatePage = 0;
                updateUI();
            }
        }
    }

    // === UI ===

    private void resetComposition() {
        mPreedit.setLength(0);
        mCursor = 0;
        mCandidates.clear();
        mCandidatePage = 0;
        if (mPreeditView != null) {
            mPreeditView.setText("");
            mPreeditView.setVisibility(View.GONE);
        }
        if (mCandidateContainer != null) {
            mCandidateContainer.removeAllViews();
        }
    }

    private void updateToggleLabel() {
        if (mToggleKey != null) {
            mToggleKey.setText(mChineseMode ? "中" : "英");
        }
    }

    private void updateUI() {
        if (mPreeditView == null || mCandidateContainer == null) return;

        if (mKeyboardMode == KeyboardMode.T9) {
            updateT9UI();
        } else {
            updatePinyinUI();
        }
    }

    private void updatePinyinUI() {
        String segmented = mEngine.segmentPinyin(mPreedit.toString());
        SpannableString ss = new SpannableString(segmented);
        ss.setSpan(new UnderlineSpan(), 0, segmented.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        mPreeditView.setText(ss);
        mPreeditView.setVisibility(View.VISIBLE);

        mCandidates.clear();
        if (mEngine.isReady()) {
            SimeEngine.Candidate[] raw =
                mEngine.decodeSentence(mPreedit.toString(), MAX_CANDIDATES);
            for (SimeEngine.Candidate c : raw) {
                mCandidates.add(c);
            }
        }
        showCandidatePage();
    }

    private void updateT9UI() {
        String digits = mPreedit.toString();
        String preeditText = digits;
        if (mEngine.isReady()) {
            SimeEngine.Candidate[] t9Results =
                mEngine.decodeT9(digits, MAX_CANDIDATES);
            mCandidates.clear();
            for (SimeEngine.Candidate c : t9Results) {
                mCandidates.add(c);
            }
            // Show pinyin instead of raw digits
            String pinyin = mEngine.decodeT9Pinyin(digits);
            if (pinyin != null && !pinyin.isEmpty()) {
                preeditText = pinyin;
            }
        }

        SpannableString ss = new SpannableString(preeditText);
        ss.setSpan(new UnderlineSpan(), 0, preeditText.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        mPreeditView.setText(ss);
        mPreeditView.setVisibility(View.VISIBLE);

        showCandidatePage();
    }

    private void showCandidatePage() {
        mCandidateContainer.removeAllViews();

        int start = mCandidatePage * PAGE_SIZE;
        int end = Math.min(start + PAGE_SIZE, mCandidates.size());

        if (mCandidatePage > 0) {
            TextView arrow = makeCandidateView("◀", -1);
            arrow.setOnClickListener(v -> {
                mCandidatePage--;
                showCandidatePage();
            });
            mCandidateContainer.addView(arrow);
        }

        for (int i = start; i < end; i++) {
            SimeEngine.Candidate c = mCandidates.get(i);
            int indexOnPage = i - start;
            TextView tv = makeCandidateView(c.text, indexOnPage);

            final int idx = indexOnPage;
            tv.setOnClickListener(view -> selectCandidate(idx));
            mCandidateContainer.addView(tv);

            // Add a subtle divider between candidates
            if (i < end - 1) {
                View divider = new View(this);
                divider.setBackgroundColor(Color.parseColor("#E0E0E0"));
                LinearLayout.LayoutParams divParams = new LinearLayout.LayoutParams(dp(1), dp(20));
                divParams.gravity = Gravity.CENTER_VERTICAL;
                mCandidateContainer.addView(divider, divParams);
            }
        }

        if (end < mCandidates.size()) {
            TextView arrow = makeCandidateView("▶", -1);
            arrow.setOnClickListener(v -> {
                mCandidatePage++;
                showCandidatePage();
            });
            mCandidateContainer.addView(arrow);
        }

        if (mCandidateScroll != null) {
            mCandidateScroll.scrollTo(0, 0);
        }
    }

    private TextView makeCandidateView(String text, int indexOnPage) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        tv.setPadding(dp(14), dp(8), dp(14), dp(8));
        tv.setClickable(true);

        int textColor;
        if (indexOnPage == 0) {
            textColor = getColor(R.color.candidate_first);
            tv.setTypeface(null, Typeface.BOLD);
        } else if (indexOnPage < 0) {
            textColor = getColor(R.color.key_text_secondary);
        } else {
            textColor = getColor(R.color.candidate_text);
        }
        tv.setTextColor(textColor);

        // Rounded ripple background for candidate items
        if (indexOnPage >= 0) {
            GradientDrawable bg = new GradientDrawable();
            bg.setCornerRadius(dp(6));
            bg.setColor(Color.TRANSPARENT);
            tv.setBackground(bg);
        }

        return tv;
    }

    // === Symbol keyboard ===

    private void setupSymbolKeyboard() {
        LinearLayout tabsContainer = mInputView.findViewById(R.id.symbol_tabs);
        tabsContainer.removeAllViews();
        List<String> categories = new ArrayList<>(SYMBOL_CATEGORIES.keySet());

        for (int i = 0; i < categories.size(); i++) {
            final int catIndex = i;
            TextView tab = new TextView(this);
            tab.setText(categories.get(i));
            tab.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            tab.setPadding(dp(16), dp(8), dp(16), dp(8));
            tab.setGravity(Gravity.CENTER);
            tab.setClickable(true);

            if (i == mSymbolCategory) {
                tab.setTextColor(getColor(R.color.candidate_first));
                tab.setTypeface(null, Typeface.BOLD);
            } else {
                tab.setTextColor(getColor(R.color.key_text_secondary));
                tab.setTypeface(null, Typeface.NORMAL);
            }

            tab.setOnClickListener(v -> {
                mSymbolCategory = catIndex;
                mSymbolPage = 0;
                setupSymbolKeyboard(); // Refresh
            });
            tabsContainer.addView(tab);
        }

        // Populate symbol grid
        String categoryName = categories.get(mSymbolCategory);
        String[] symbols = SYMBOL_CATEGORIES.get(categoryName);
        int totalPages = (symbols.length + SYMBOLS_PER_PAGE - 1) / SYMBOLS_PER_PAGE;
        int start = mSymbolPage * SYMBOLS_PER_PAGE;
        int end = Math.min(start + SYMBOLS_PER_PAGE, symbols.length);

        LinearLayout grid = mInputView.findViewById(R.id.symbol_grid);
        grid.removeAllViews();

        for (int row = 0; row < SYMBOL_ROWS; row++) {
            LinearLayout rowLayout = new LinearLayout(this);
            rowLayout.setOrientation(LinearLayout.HORIZONTAL);
            rowLayout.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(52)));

            for (int col = 0; col < SYMBOL_COLS; col++) {
                int idx = start + row * SYMBOL_COLS + col;
                TextView cell = new TextView(this);
                LinearLayout.LayoutParams cellParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1);
                cellParams.setMargins(dp(2), dp(2), dp(2), dp(2));
                cell.setLayoutParams(cellParams);
                cell.setGravity(Gravity.CENTER);
                cell.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
                cell.setBackground(getDrawable(R.drawable.key_bg));

                if (idx < end) {
                    String sym = symbols[idx];
                    cell.setText(sym);
                    cell.setTextColor(getColor(R.color.key_text));
                    cell.setTag("sym_" + sym);
                } else {
                    cell.setText("");
                    cell.setTextColor(Color.TRANSPARENT);
                }
                rowLayout.addView(cell);
            }
            grid.addView(rowLayout);
        }

        // Page indicator with left/right navigation
        TextView pageIndicator = mInputView.findViewById(R.id.sym_page_indicator);
        if (totalPages > 1) {
            String prevArrow = mSymbolPage > 0 ? "◀  " : "    ";
            String nextArrow = mSymbolPage < totalPages - 1 ? "  ▶" : "    ";
            pageIndicator.setText(prevArrow + (mSymbolPage + 1) + "/" + totalPages + nextArrow);
            pageIndicator.setOnClickListener(v -> {
                // Tap left half = prev page, right half = next page
                // Since we can't easily detect tap position on click, just cycle forward
                // Use a GestureDetector-free approach: left half vs right half
            });
            pageIndicator.setOnTouchListener((v, event) -> {
                if (event.getAction() == MotionEvent.ACTION_UP) {
                    float x = event.getX();
                    float w = v.getWidth();
                    if (x < w / 2 && mSymbolPage > 0) {
                        mSymbolPage--;
                        setupSymbolKeyboard();
                    } else if (x >= w / 2 && mSymbolPage < totalPages - 1) {
                        mSymbolPage++;
                        setupSymbolKeyboard();
                    }
                    return true;
                }
                return event.getAction() == MotionEvent.ACTION_DOWN;
            });
        } else {
            pageIndicator.setText("");
            pageIndicator.setOnTouchListener(null);
        }

        // Set up touch listeners (for backspace, space, sym_back, and symbol keys)
        setupTouchListeners(mInputView);
    }

    // === Utility ===

    private int dp(int value) {
        return (int) TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }

    private int dp(float value) {
        return (int) TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }
}
