package com.shiyu.sime.ime.data;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;

/**
 * Persistent store for user quick phrases, backed by SharedPreferences.
 * Phrases are stored as a simple indexed list.
 */
public class QuickPhraseStore {

    private static final String PREFS_NAME = "sime_quick_phrases";
    private static final String KEY_COUNT = "count";
    private static final String KEY_PREFIX = "phrase_";

    private final SharedPreferences prefs;

    public QuickPhraseStore(Context context) {
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

    public void add(String text) {
        List<String> all = getAll();
        all.add(text);
        save(all);
    }

    public void updateAt(int index, String text) {
        List<String> all = getAll();
        if (index >= 0 && index < all.size()) {
            all.set(index, text);
            save(all);
        }
    }

    public void removeAt(int index) {
        List<String> all = getAll();
        if (index >= 0 && index < all.size()) {
            all.remove(index);
            save(all);
        }
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
