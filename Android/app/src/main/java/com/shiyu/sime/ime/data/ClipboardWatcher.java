package com.shiyu.sime.ime.data;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;

/**
 * Watches the system clipboard and saves new text entries to
 * {@link ClipboardStore}. Pauses automatically in password fields.
 */
public class ClipboardWatcher {

    private final Context context;
    private final ClipboardStore store;
    private ClipboardManager clipboardManager;
    private boolean paused;

    private final ClipboardManager.OnPrimaryClipChangedListener clipListener =
            this::onClipChanged;

    public ClipboardWatcher(Context context) {
        this.context = context;
        this.store = new ClipboardStore(context);
    }

    public void start() {
        clipboardManager = (ClipboardManager)
                context.getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboardManager != null) {
            clipboardManager.addPrimaryClipChangedListener(clipListener);
        }
    }

    public void stop() {
        if (clipboardManager != null) {
            clipboardManager.removePrimaryClipChangedListener(clipListener);
        }
    }

    public void setPaused(boolean paused) {
        this.paused = paused;
    }

    private void onClipChanged() {
        if (paused || clipboardManager == null) return;
        ClipData clip = clipboardManager.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) return;
        CharSequence text = clip.getItemAt(0).getText();
        if (text != null && text.length() > 0) {
            store.add(text.toString());
        }
    }
}
