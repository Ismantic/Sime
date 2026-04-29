package com.shiyu.sime;

import android.inputmethodservice.InputMethodService;
import android.text.InputType;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

import com.shiyu.sime.ime.InputKernel;
import com.shiyu.sime.ime.InputState;
import com.shiyu.sime.ime.InputView;
import com.shiyu.sime.ime.KeyboardMode;
import com.shiyu.sime.ime.data.ClipboardWatcher;
import com.shiyu.sime.ime.data.EmojiStore;
import com.shiyu.sime.ime.data.TraditionalConverter;
import com.shiyu.sime.ime.engine.SimeEngineDecoder;
import com.shiyu.sime.ime.prefs.SimePrefs;

import java.io.File;
import java.text.BreakIterator;

/**
 * Thin IME host. Life-cycle + {@link InputConnection} bridging only —
 * all input semantics live in {@link InputKernel}.
 */
public class SimeService extends InputMethodService
        implements InputKernel.Listener {

    private static final String TAG = "SimeIME";

    private SimeEngine engine;
    private InputKernel kernel;
    private InputView inputView;
    private ClipboardWatcher clipboardWatcher;
    private TraditionalConverter tradConverter;
    private EmojiStore emojiStore;

    public EmojiStore getEmojiStore() { return emojiStore; }

    /**
     * When non-null, commits / deletes / preedit are routed here
     * instead of to the host app's {@link InputConnection}. Used by
     * the in-IME phrase composer (AddPhraseView) so the user can type
     * a quick phrase using Sime's own keyboard.
     */
    private com.shiyu.sime.ime.compose.ComposeSink composeSink;

    public void setComposeSink(com.shiyu.sime.ime.compose.ComposeSink sink) {
        this.composeSink = sink;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        engine = new SimeEngine();
        engine.start(getApplicationContext());
        kernel = new InputKernel(new SimeEngineDecoder(engine));
        clipboardWatcher = new ClipboardWatcher(this);
        clipboardWatcher.start();
        // Load the trad table on a background thread so we don't stall
        // IME startup on the ~2MB file read. Engine.start() already runs
        // its own background extraction; we piggyback after a short delay
        // so the assets are likely on disk by the time we read.
        tradConverter = new TraditionalConverter();
        kernel.setTraditionalConverter(tradConverter);
        emojiStore = new EmojiStore();
        new Thread(() -> {
            File simeDir = new File(getApplicationContext().getFilesDir(), "sime");
            File ftDict = new File(simeDir, "sime.ft.dict.txt");
            File emojiTxt = new File(simeDir, "emoji.txt");
            // Spin briefly until the asset extraction has materialized
            // the files. Bounded to ~3s.
            for (int i = 0; i < 30; i++) {
                if (ftDict.exists() && emojiTxt.exists()) break;
                try { Thread.sleep(100); } catch (InterruptedException e) { break; }
            }
            tradConverter.load(ftDict);
            emojiStore.load(emojiTxt);
        }, "sime-asset-load").start();
        applyPrefs();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "onDestroy");
        if (clipboardWatcher != null) clipboardWatcher.stop();
        if (kernel != null) kernel.detach();
        if (engine != null) engine.stop();
        super.onDestroy();
    }

    @Override
    public void onTrimMemory(int level) {
        super.onTrimMemory(level);
        // System is asking us to release memory. Drop the trie sep_cache
        // (rebuilds lazily on next decode at small one-shot cost) and
        // flush user-sentence history so unsaved learns aren't lost if
        // the process is reaped under pressure.
        if (level >= TRIM_MEMORY_RUNNING_MODERATE && engine != null) {
            engine.resetCaches();
            engine.flushUserSentence();
        }
    }

    private void applyPrefs() {
        SimePrefs prefs = new SimePrefs(this);
        kernel.setChineseLayout(prefs.getChineseLayout());
        kernel.setPredictionEnabled(prefs.getPredictionEnabled());
        kernel.setTraditionalEnabled(prefs.getTraditionalEnabled());
        com.shiyu.sime.ime.feedback.InputFeedbacks.setSoundEnabled(
                prefs.getSoundEnabled());
        com.shiyu.sime.ime.feedback.InputFeedbacks.setVibrationEnabled(
                prefs.getVibrationEnabled());
    }

    @Override
    public View onCreateInputView() {
        inputView = new InputView(this);
        InputKernel.Snapshot initialSnapshot = new InputKernel.Snapshot(
                new InputState(), java.util.Collections.emptyList(),
                java.util.Collections.emptyList(), "",
                KeyboardMode.CHINESE, kernel.getInitialChineseLayout(), "");
        inputView.attach(kernel, initialSnapshot);
        kernel.attach(this, inputView);
        inputView.setOnHideRequest(() -> requestHideSelf(0));
        return inputView;
    }

    @Override
    public void onStartInput(EditorInfo attribute, boolean restarting) {
        super.onStartInput(attribute, restarting);
        // Pick up any layout change made in SettingsActivity since we last ran.
        applyPrefs();
        // Detect password / OTP-style fields. While focused there:
        //   - clipboard watcher pauses (don't capture pasted secrets)
        //   - prediction is suppressed (avoid leaking previous-context
        //     suggestions into a sensitive field)
        boolean privateField = isPrivateField(attribute);
        if (clipboardWatcher != null) clipboardWatcher.setPaused(privateField);
        if (kernel != null) kernel.setPrivateField(privateField);
        if (kernel != null) kernel.onStartInput();
    }

    @Override
    public void onFinishInput() {
        super.onFinishInput();
        if (clipboardWatcher != null) clipboardWatcher.setPaused(false);
        if (kernel != null) kernel.setPrivateField(false);
        if (kernel != null) kernel.onFinishInput();
    }

    private static boolean isPrivateField(EditorInfo info) {
        if (info == null) return false;
        // Apps / autofill providers can opt out of personalized learning
        // explicitly. Honor that flag wherever Android sets it.
        if ((info.imeOptions & EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING) != 0) {
            return true;
        }
        int type = info.inputType;
        int cls = type & InputType.TYPE_MASK_CLASS;
        int variation = type & InputType.TYPE_MASK_VARIATION;
        if (cls == InputType.TYPE_CLASS_TEXT) {
            if (variation == InputType.TYPE_TEXT_VARIATION_PASSWORD
                    || variation == InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
                    || variation == InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD) {
                return true;
            }
        }
        if (cls == InputType.TYPE_CLASS_NUMBER
                && variation == InputType.TYPE_NUMBER_VARIATION_PASSWORD) {
            return true;
        }
        return false;
    }

    // ===== InputKernel.Listener =====

    @Override
    public void onCommitText(String text) {
        if (composeSink != null) {
            composeSink.onCommit(text);
            return;
        }
        InputConnection ic = getCurrentInputConnection();
        if (ic != null) ic.commitText(text, 1);
    }

    @Override
    public void onDeleteBefore(int count) {
        if (composeSink != null) {
            composeSink.onDelete(count);
            return;
        }
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        // Walk back `count` grapheme clusters and delete that many UTF-16
        // chars in one call. Without this, emoji (surrogate pairs) and
        // ZWJ sequences (家庭表情等) take multiple presses to disappear.
        ic.deleteSurroundingText(graphemeUnitsBefore(ic, count), 0);
    }

    private static int graphemeUnitsBefore(InputConnection ic, int graphemes) {
        // Pull a generous window before the cursor — enough to cover the
        // longest plausible ZWJ emoji sequence times `graphemes`.
        CharSequence before = ic.getTextBeforeCursor(64 * graphemes, 0);
        if (before == null || before.length() == 0) return graphemes;
        BreakIterator it = BreakIterator.getCharacterInstance();
        it.setText(before.toString());
        int boundary = before.length();
        for (int i = 0; i < graphemes; i++) {
            int prev = it.preceding(boundary);
            if (prev == BreakIterator.DONE) break;
            boundary = prev;
        }
        int deleted = before.length() - boundary;
        // Fallback to char-count if the buffer was empty / iterator did
        // nothing — never return 0 when the caller asked for a deletion.
        return deleted > 0 ? deleted : graphemes;
    }

    @Override
    public void onSendEnter() {
        if (composeSink != null) {
            // Enter inside the composer: ignore (user uses 完成 button).
            return;
        }
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
        ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
    }

    @Override
    public void onSetComposingText(String preedit) {
        if (composeSink != null) {
            composeSink.onPreedit(preedit == null ? "" : preedit);
            return;
        }
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        if (preedit == null || preedit.isEmpty()) {
            ic.finishComposingText();
        } else {
            ic.setComposingText(preedit, 1);
        }
    }

}
