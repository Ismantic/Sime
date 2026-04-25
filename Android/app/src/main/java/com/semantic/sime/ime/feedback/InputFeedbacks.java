package com.semantic.sime.ime.feedback;

import android.content.Context;
import android.media.AudioManager;
import android.view.HapticFeedbackConstants;
import android.view.View;

/**
 * Process-global key-press feedback (click sound + haptic).
 *
 * <p>When sound is enabled we call {@link AudioManager#playSoundEffect}
 * with {@link AudioManager#FX_KEYPRESS_STANDARD}; that helper is itself
 * gated by {@code Settings.System.SOUND_EFFECTS_ENABLED}, so the IME
 * toggle effectively means "follow the system click-sound setting".
 *
 * <p>For haptic we call {@link View#performHapticFeedback} without any
 * override flags, so the system's {@code HAPTIC_FEEDBACK_ENABLED} setting
 * still applies — same "follow system" semantics.
 *
 * <p>State is held in static volatile fields so {@link com.semantic.sime.ime.keyboard.framework.KeyView}
 * can fire feedback without plumbing prefs through every constructor.
 * {@link com.semantic.sime.SimeService} pushes the current prefs at
 * startup and on settings changes.
 */
public final class InputFeedbacks {

    private static volatile boolean soundEnabled = true;
    private static volatile boolean vibrationEnabled = true;

    private InputFeedbacks() {}

    public static void setSoundEnabled(boolean enabled) {
        soundEnabled = enabled;
    }

    public static void setVibrationEnabled(boolean enabled) {
        vibrationEnabled = enabled;
    }

    /** Fire on key DOWN. Cheap when both toggles are off. */
    public static void onKeyPress(View view) {
        if (soundEnabled) {
            AudioManager am = (AudioManager) view.getContext()
                    .getSystemService(Context.AUDIO_SERVICE);
            if (am != null) {
                am.playSoundEffect(AudioManager.FX_KEYPRESS_STANDARD);
            }
        }
        if (vibrationEnabled) {
            view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        }
    }

    /**
     * Wrap a click listener so the system's default click sound is
     * suppressed and our gated {@link #onKeyPress} fires instead. Use
     * this for any clickable that isn't a {@link com.semantic.sime.ime.keyboard.framework.KeyView}
     * (candidates, toolbar buttons, etc.) — KeyView already fires
     * feedback in its own touch dispatch and bypasses {@code performClick}.
     */
    public static void wireClick(View view, Runnable action) {
        view.setSoundEffectsEnabled(false);
        view.setOnClickListener(v -> {
            onKeyPress(v);
            action.run();
        });
    }
}
