package com.shiyu.sime.ime.data;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;

/**
 * Persistent clipboard history store, backed by SharedPreferences.
 * Keeps the most recent {@link #MAX_ITEMS} clipboard entries.
 */
public class ClipboardStore {

    public static final int MAX_ITEMS = 20;

    private static final String PREFS_NAME = "sime_clipboard";
    private static final String KEY_COUNT = "count";
    private static final String KEY_PREFIX = "clip_";

    private final SharedPreferences prefs;

    public ClipboardStore(Context context) {
        prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    public List<String> getAll() {
        int count = prefs.getInt(KEY_COUNT, 0);
        List<String> list = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            String s = prefs.getString(KEY_PREFIX + i, null);
            if (s != null) list.add(s);
        }
        return list;
    }

    /** Add a new entry at the front, trimming to {@link #MAX_ITEMS}. */
    public void add(String text) {
        if (text == null || text.isEmpty()) return;
        List<String> all = getAll();
        // Remove duplicate if already present.
        all.remove(text);
        all.add(0, text);
        while (all.size() > MAX_ITEMS) {
            all.remove(all.size() - 1);
        }
        save(all);
    }

    public void removeAt(int index) {
        List<String> all = getAll();
        if (index >= 0 && index < all.size()) {
            all.remove(index);
            save(all);
        }
    }

    public void clearAll() {
        prefs.edit().clear().apply();
    }

    private void save(List<String> list) {
        SharedPreferences.Editor ed = prefs.edit();
        ed.clear();
        ed.putInt(KEY_COUNT, list.size());
        for (int i = 0; i < list.size(); i++) {
            ed.putString(KEY_PREFIX + i, list.get(i));
        }
        ed.apply();
    }
}
