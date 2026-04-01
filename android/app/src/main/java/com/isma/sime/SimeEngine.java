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

    // Native methods
    private static native boolean nativeLoadResources(String triePath, String modelPath);
    private static native boolean nativeLoadT9(String pinyinModelPath);
    private static native boolean nativeLoadUserDict(String userDictPath);
    private static native String[] nativeDecodeSentence(String input, int num);
    private static native String nativeSegmentPinyin(String input);
    private static native String[] nativeDecodeT9(String digits, int num);
    private static native boolean nativeIsReady();

    private boolean mReady = false;

    /**
     * Initialize engine: extract assets and load models.
     */
    public boolean init(Context context) {
        if (!sLoaded) return false;

        File dataDir = new File(context.getFilesDir(), "sime");
        if (!dataDir.exists()) dataDir.mkdirs();

        String triePath = extractAsset(context, "trie.bin", dataDir);
        String modelPath = extractAsset(context, "model.bin", dataDir);
        String pinyinModelPath = extractAsset(context, "pinyin_model.bin", dataDir);

        if (triePath == null || modelPath == null) {
            Log.e(TAG, "Failed to extract assets");
            return false;
        }

        if (!nativeLoadResources(triePath, modelPath)) {
            Log.e(TAG, "Failed to load resources");
            return false;
        }

        if (pinyinModelPath != null) {
            nativeLoadT9(pinyinModelPath);
        }

        mReady = true;
        Log.i(TAG, "Engine initialized");
        return true;
    }

    public boolean isReady() {
        return mReady && sLoaded && nativeIsReady();
    }

    /** Candidate result: text + number of input bytes consumed. */
    public static class Candidate {
        public final String text;
        public final int matchedLen;

        Candidate(String text, int matchedLen) {
            this.text = text;
            this.matchedLen = matchedLen;
        }
    }

    /** Decode pinyin input to hanzi candidates. */
    public Candidate[] decodeSentence(String input, int num) {
        if (!isReady()) return new Candidate[0];
        String[] raw = nativeDecodeSentence(input, num);
        Candidate[] result = new Candidate[raw.length / 2];
        for (int i = 0; i < result.length; i++) {
            result[i] = new Candidate(
                raw[i * 2],
                Integer.parseInt(raw[i * 2 + 1])
            );
        }
        return result;
    }

    /** Segment raw pinyin into spaced syllables. */
    public String segmentPinyin(String input) {
        if (!sLoaded) return input;
        return nativeSegmentPinyin(input);
    }

    /** T9: decode digit string to hanzi candidates. */
    public Candidate[] decodeT9(String digits, int num) {
        if (!isReady()) return new Candidate[0];
        String[] raw = nativeDecodeT9(digits, num);
        Candidate[] result = new Candidate[raw.length / 2];
        for (int i = 0; i < result.length; i++) {
            result[i] = new Candidate(
                raw[i * 2],
                Integer.parseInt(raw[i * 2 + 1])
            );
        }
        return result;
    }

    /**
     * Extract an asset file to internal storage if not already present.
     * Returns the absolute path, or null on failure.
     */
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
