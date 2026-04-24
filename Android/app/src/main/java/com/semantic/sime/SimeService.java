package com.semantic.sime;

import android.inputmethodservice.InputMethodService;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

import com.semantic.sime.ime.InputKernel;
import com.semantic.sime.ime.InputState;
import com.semantic.sime.ime.InputView;
import com.semantic.sime.ime.KeyboardMode;
import com.semantic.sime.ime.engine.SimeEngineDecoder;
import com.semantic.sime.ime.prefs.SimePrefs;

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

    @Override
    public void onCreate() {
        super.onCreate();
        engine = new SimeEngine();
        engine.start(getApplicationContext());
        kernel = new InputKernel(new SimeEngineDecoder(engine));
        applyPrefs();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "onDestroy");
        if (kernel != null) kernel.detach();
        if (engine != null) engine.stop();
        super.onDestroy();
    }

    private void applyPrefs() {
        SimePrefs prefs = new SimePrefs(this);
        kernel.setChineseLayout(prefs.getChineseLayout());
        kernel.setPredictionEnabled(prefs.getPredictionEnabled());
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
        inputView.getCandidatesBar().setOnCandidatePickListener(idx -> {
            Log.d(TAG, "candidate pick: " + idx);
            kernel.onCandidatePick(idx);
        });
        inputView.getCandidatesBar().setOnSettingsListener(() -> {
            Log.d(TAG, "settings gear tapped");
            kernel.switchMode(KeyboardMode.SETTINGS);
        });
        inputView.getCandidatesBar().setOnHideListener(() -> requestHideSelf(0));
        return inputView;
    }

    @Override
    public void onStartInput(EditorInfo attribute, boolean restarting) {
        super.onStartInput(attribute, restarting);
        // Pick up any layout change made in SettingsActivity since we last ran.
        applyPrefs();
        if (kernel != null) kernel.onStartInput();
    }

    @Override
    public void onFinishInput() {
        super.onFinishInput();
        if (kernel != null) kernel.onFinishInput();
    }

    // ===== InputKernel.Listener =====

    @Override
    public void onCommitText(String text) {
        InputConnection ic = getCurrentInputConnection();
        if (ic != null) ic.commitText(text, 1);
    }

    @Override
    public void onDeleteBefore(int count) {
        InputConnection ic = getCurrentInputConnection();
        if (ic != null) ic.deleteSurroundingText(count, 0);
    }

    @Override
    public void onSendEnter() {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
        ic.sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
    }

    @Override
    public void onSetComposingText(String preedit) {
        InputConnection ic = getCurrentInputConnection();
        if (ic == null) return;
        if (preedit == null || preedit.isEmpty()) {
            ic.finishComposingText();
        } else {
            ic.setComposingText(preedit, 1);
        }
    }

}
