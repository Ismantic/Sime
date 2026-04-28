package com.shiyu.sime.ime.prefs;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

import com.shiyu.sime.ime.ChineseLayout;

/**
 * Strongly-typed SharedPreferences wrapper. Currently exposes the
 * Chinese layout preference (QWERTY / T9). New settings are added by
 * extending this class — callers never touch string keys directly.
 */
public final class SimePrefs {

    private static final String KEY_CHINESE_LAYOUT = "chinese_layout";
    private static final String KEY_PREDICTION_ENABLED = "prediction_enabled";
    private static final String KEY_SOUND_ENABLED = "sound_enabled";
    private static final String KEY_VIBRATION_ENABLED = "vibration_enabled";
    private static final String KEY_TRADITIONAL_ENABLED = "traditional_enabled";

    private final SharedPreferences sp;

    public SimePrefs(Context ctx) {
        this.sp = PreferenceManager.getDefaultSharedPreferences(ctx);
    }

    public ChineseLayout getChineseLayout() {
        String v = sp.getString(KEY_CHINESE_LAYOUT, ChineseLayout.QWERTY.name());
        try {
            return ChineseLayout.valueOf(v);
        } catch (IllegalArgumentException e) {
            return ChineseLayout.QWERTY;
        }
    }

    public void setChineseLayout(ChineseLayout l) {
        sp.edit().putString(KEY_CHINESE_LAYOUT, l.name()).apply();
    }

    public boolean getPredictionEnabled() {
        return sp.getBoolean(KEY_PREDICTION_ENABLED, true);
    }

    public void setPredictionEnabled(boolean enabled) {
        sp.edit().putBoolean(KEY_PREDICTION_ENABLED, enabled).apply();
    }

    /** Sound on key press. When true, follows system sound-effects setting. */
    public boolean getSoundEnabled() {
        return sp.getBoolean(KEY_SOUND_ENABLED, true);
    }

    public void setSoundEnabled(boolean enabled) {
        sp.edit().putBoolean(KEY_SOUND_ENABLED, enabled).apply();
    }

    /** Haptic feedback on key press. When true, follows system haptic setting. */
    public boolean getVibrationEnabled() {
        return sp.getBoolean(KEY_VIBRATION_ENABLED, true);
    }

    public void setVibrationEnabled(boolean enabled) {
        sp.edit().putBoolean(KEY_VIBRATION_ENABLED, enabled).apply();
    }

    /** Output traditional Chinese (placeholder; no conversion logic yet). */
    public boolean getTraditionalEnabled() {
        return sp.getBoolean(KEY_TRADITIONAL_ENABLED, false);
    }

    public void setTraditionalEnabled(boolean enabled) {
        sp.edit().putBoolean(KEY_TRADITIONAL_ENABLED, enabled).apply();
    }
}
