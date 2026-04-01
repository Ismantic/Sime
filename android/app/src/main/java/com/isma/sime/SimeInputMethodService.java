package com.isma.sime;

import android.graphics.Color;
import android.graphics.Typeface;
import android.inputmethodservice.InputMethodService;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.UnderlineSpan;
import android.util.Log;
import android.util.TypedValue;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public class SimeInputMethodService extends InputMethodService {
    private static final String TAG = "SimeIME";
    private static final int MAX_CANDIDATES = 36;
    private static final int PAGE_SIZE = 9;

    private SimeEngine mEngine;
    private StringBuilder mPreedit = new StringBuilder();
    private int mCursor = 0;
    private boolean mChineseMode = true;
    private boolean mT9Mode = false;

    // Candidate state
    private List<SimeEngine.Candidate> mCandidates = new ArrayList<>();
    private int mCandidatePage = 0;

    // UI refs (re-bound on keyboard switch)
    private View mInputView;
    private LinearLayout mCandidateContainer;
    private HorizontalScrollView mCandidateScroll;
    private TextView mPreeditView;
    private TextView mToggleKey;

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
    }

    @Override
    public View onCreateInputView() {
        return inflateKeyboard();
    }

    private View inflateKeyboard() {
        int layoutRes = mT9Mode ? R.layout.input_view_t9 : R.layout.input_view;
        mInputView = getLayoutInflater().inflate(layoutRes, null);
        mPreeditView = mInputView.findViewById(R.id.preedit);
        mCandidateContainer = mInputView.findViewById(R.id.candidate_container);
        mCandidateScroll = mInputView.findViewById(R.id.candidate_scroll);
        mToggleKey = mInputView.findViewById(R.id.key_toggle);
        updateToggleLabel();
        return mInputView;
    }

    private void switchKeyboard() {
        // Commit any pending preedit
        if (mPreedit.length() > 0) {
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(mPreedit.toString(), 1);
            resetComposition();
        }
        mT9Mode = !mT9Mode;
        setInputView(inflateKeyboard());
    }

    @Override
    public void onStartInput(EditorInfo attribute, boolean restarting) {
        super.onStartInput(attribute, restarting);
        resetComposition();
    }

    @Override
    public void onFinishInput() {
        super.onFinishInput();
        resetComposition();
    }

    public void onKeyPress(View v) {
        String tag = (String) v.getTag();
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
            case "switch_kb":
                switchKeyboard();
                break;
            case "comma":
            case "period":
                handlePunctuation(tag);
                break;
            default:
                if (tag.startsWith("t9_")) {
                    handleT9Digit(tag.substring(3));
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
            // English mode: just type the digit
            InputConnection ic = getCurrentInputConnection();
            if (ic != null) ic.commitText(digit, 1);
            return;
        }
        // Only 2-9 are valid T9 keys; 0 = space, 1 = reserved
        if ("0".equals(digit)) {
            handleSpace();
            return;
        }
        if ("1".equals(digit)) {
            // Could be used for punctuation later; for now ignore in Chinese mode
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
        updateToggleLabel();
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
        if (mT9Mode) {
            // T9: matchedLen = digits consumed (the whole input)
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

        if (mT9Mode) {
            updateT9UI();
        } else {
            updatePinyinUI();
        }
    }

    private void updatePinyinUI() {
        // Preedit with underline
        String segmented = mEngine.segmentPinyin(mPreedit.toString());
        SpannableString ss = new SpannableString(segmented);
        ss.setSpan(new UnderlineSpan(), 0, segmented.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        mPreeditView.setText(ss);
        mPreeditView.setVisibility(View.VISIBLE);

        // Decode candidates
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

        // Preedit: show digits + pinyin interpretation
        String preeditText = digits;
        if (mEngine.isReady()) {
            // Get top pinyin parse to show in preedit
            SimeEngine.Candidate[] t9Results =
                mEngine.decodeT9(digits, MAX_CANDIDATES);
            mCandidates.clear();
            for (SimeEngine.Candidate c : t9Results) {
                mCandidates.add(c);
            }

            // Show digits → pinyin in preedit
            // Use segmentPinyin on the first candidate's matched pinyin
            // Actually we need T9 pinyin decode - let's just show digits for now
            // and the hanzi candidates below
            if (!mCandidates.isEmpty()) {
                // The first candidate text is the best hanzi match
                preeditText = digits;
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
            String label = (indexOnPage + 1) + "." + c.text;
            TextView tv = makeCandidateView(label, indexOnPage);

            final int idx = indexOnPage;
            tv.setOnClickListener(view -> selectCandidate(idx));
            mCandidateContainer.addView(tv);
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
        tv.setTextColor(indexOnPage == 0 ? Color.parseColor("#1A73E8") : Color.parseColor("#333333"));
        if (indexOnPage == 0) {
            tv.setTypeface(null, Typeface.BOLD);
        }
        return tv;
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }
}
