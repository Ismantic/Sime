package com.isma.sime;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;

/**
 * Java wrapper for native SIME engine.
 * Handles asset extraction and JNI calls.
 */
public class SimeEngine {
    private static final String TAG = "SimeEngine";
    private static boolean sLoaded = false;

    static {
        try {
            System.loadLibrary("sime_jni");
            sLoaded = true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load sime_jni", e);
        }
    }

    // Native methods — both return triplets [text, units, cnt, ...]
    private static native boolean nativeLoadResources(String triePath, String modelPath);
    private static native boolean nativeLoadUserDict(String userDictPath);
    private static native String[] nativeDecodeSentence(String input, int num);
    private static native String[] nativeDecodeT9(String prefixLetters, String digits, int num);
    private static native boolean nativeIsReady();

    private boolean mReady = false;

    /** Decode result: text + segmented units + input consumed. */
    public static class Candidate {
        public final String text;
        public final String units;
        public final int matchedLen;

        Candidate(String text, String units, int matchedLen) {
            this.text = text;
            this.units = units;
            this.matchedLen = matchedLen;
        }
    }

    public boolean init(Context context) {
        if (!sLoaded) return false;

        File dataDir = new File(context.getFilesDir(), "sime");
        if (!dataDir.exists()) dataDir.mkdirs();

        String triePath = extractAsset(context, "sime.trie", dataDir);
        String modelPath = extractAsset(context, "sime.cnt", dataDir);

        if (triePath == null || modelPath == null) {
            Log.e(TAG, "Failed to extract assets");
            return false;
        }

        if (!nativeLoadResources(triePath, modelPath)) {
            Log.e(TAG, "Failed to load resources");
            return false;
        }

        mReady = true;
        Log.i(TAG, "Engine initialized");
        return true;
    }

    public boolean isReady() {
        return mReady && sLoaded && nativeIsReady();
    }

    /** Parse JNI triplet array into Candidate[]. */
    private static Candidate[] parseTriplets(String[] raw) {
        Candidate[] result = new Candidate[raw.length / 3];
        for (int i = 0; i < result.length; i++) {
            result[i] = new Candidate(
                raw[i * 3],
                raw[i * 3 + 1],
                Integer.parseInt(raw[i * 3 + 2])
            );
        }
        return result;
    }

    /** Decode unit input to candidates. */
    public Candidate[] decodeSentence(String input, int num) {
        if (!isReady()) return new Candidate[0];
        return parseTriplets(nativeDecodeSentence(input, num));
    }

    /** T9: decode digit string (with optional pinyin prefix) to candidates. */
    public Candidate[] decodeT9(String prefixLetters, String digits, int num) {
        if (!isReady()) return new Candidate[0];
        return parseTriplets(nativeDecodeT9(
                prefixLetters != null ? prefixLetters : "", digits, num));
    }

    private static String extractAsset(Context context, String assetName, File destDir) {
        File dest = new File(destDir, assetName);
        if (dest.exists() && dest.length() > 0) {
            return dest.getAbsolutePath();
        }
        try (InputStream is = context.getAssets().open(assetName);
             FileOutputStream os = new FileOutputStream(dest)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) {
                os.write(buf, 0, n);
            }
            Log.i(TAG, "Extracted " + assetName + " (" + dest.length() + " bytes)");
            return dest.getAbsolutePath();
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract " + assetName, e);
            return null;
        }
    }
}
