package com.semantic.sime.ime.prefs;

import android.content.Context;
import android.content.SharedPreferences;
import android.preference.PreferenceManager;

import com.semantic.sime.ime.ChineseLayout;

/**
 * Strongly-typed SharedPreferences wrapper. Currently exposes the
 * Chinese layout preference (QWERTY / T9). New settings are added by
 * extending this class — callers never touch string keys directly.
 */
public final class SimePrefs {

    private static final String KEY_CHINESE_LAYOUT = "chinese_layout";

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
}
