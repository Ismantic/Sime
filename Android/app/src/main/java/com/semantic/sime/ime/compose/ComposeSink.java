package com.semantic.sime.ime.compose;

/**
 * Receives commit / delete / preedit events when the IME is composing
 * text inside its own UI (e.g. the quick-phrase add panel) instead of
 * sending them to the host app's {@code InputConnection}.
 *
 * <p>Installed on {@link com.semantic.sime.SimeService} via
 * {@code setComposeSink}; cleared (back to {@code null}) when the
 * composer is dismissed.
 */
public interface ComposeSink {
    /** Decoded hanzi (or whatever the kernel committed) — append to buffer. */
    void onCommit(String text);

    /** Backspace — drop last {@code count} chars from buffer. */
    void onDelete(int count);

    /** Composing preview (raw pinyin / digits the user is currently typing). */
    void onPreedit(String preedit);
}
