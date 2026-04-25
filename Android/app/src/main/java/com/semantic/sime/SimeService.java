package com.semantic.sime;

import android.inputmethodservice.InputMethodService;
import android.text.InputType;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

import com.semantic.sime.ime.InputKernel;
import com.semantic.sime.ime.InputState;
import com.semantic.sime.ime.InputView;
import com.semantic.sime.ime.KeyboardMode;
import com.semantic.sime.ime.data.ClipboardWatcher;
import com.semantic.sime.ime.data.EmojiStore;
import com.semantic.sime.ime.data.TraditionalConverter;
import com.semantic.sime.ime.engine.SimeEngineDecoder;
import com.semantic.sime.ime.prefs.SimePrefs;

import java.io.File;

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
    private com.semantic.sime.ime.compose.ComposeSink composeSink;

    public void setComposeSink(com.semantic.sime.ime.compose.ComposeSink sink) {
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

    private void applyPrefs() {
        SimePrefs prefs = new SimePrefs(this);
        kernel.setChineseLayout(prefs.getChineseLayout());
        kernel.setPredictionEnabled(prefs.getPredictionEnabled());
        kernel.setTraditionalEnabled(prefs.getTraditionalEnabled());
        com.semantic.sime.ime.feedback.InputFeedbacks.setSoundEnabled(
                prefs.getSoundEnabled());
        com.semantic.sime.ime.feedback.InputFeedbacks.setVibrationEnabled(
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
        if (ic != null) ic.deleteSurroundingText(count, 0);
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
