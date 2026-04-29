package com.shiyu.sime;

import android.content.Context;
import android.util.Log;

import com.shiyu.sime.ime.engine.DecodeResult;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;

/**
 * Java wrapper for the native SIME engine.
 *
 * <p>Asset extraction and {@code nativeLoadResources} run on a one-shot
 * background thread (started by {@link #start(Context)}). Decode methods
 * are called from the InputKernel's single-thread executor (off the main
 * thread) so that UI rendering is never blocked by JNI work.
 *
 * <p>Decode calls store results on the C++ side; Java reads them back
 * through typed accessors ({@code nativeResultText}, etc.) instead of
 * marshalling a {@code String[]} quad with CSV-encoded token IDs.
 */
public class SimeEngine {
    private static final String TAG = "SimeEngine";

    /**
     * Bumped whenever the bundled {@code sime.dict} / {@code sime.cnt}
     * assets change. The deployed copy in app private storage carries
     * a marker file with the version it was extracted from; mismatches
     * trigger a re-extract on the next {@link #start(Context)}.
     */
    private static final int ASSET_VERSION = 9;
    private static final String ASSET_VERSION_MARKER = ".deployed_version";

    private static boolean sLoaded = false;

    static {
        try {
            System.loadLibrary("sime_jni");
            sLoaded = true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load sime_jni", e);
        }
    }

    // ===== Native methods =====
    private static native boolean nativeLoadResources(String triePath, String modelPath);
    private static native boolean nativeIsReady();
    private static native int nativeContextSize();
    private static native void nativeResetCaches();

    // Decode methods store results in C++, return count.
    private static native int nativeDecodeSentence(String input, int extra);
    private static native int nativeDecodeNumSentence(String prefixLetters, String digits, int extra);
    private static native int nativeNextTokens(int[] contextIds, int limit, boolean enOnly);
    private static native int nativeGetTokens(String prefix, int limit, boolean enOnly);
    private static native String[] nativeT9PinyinSyllables(String digits, int limit);

    // Typed accessors for stored results.
    private static native String nativeResultText(int index);
    private static native String nativeResultUnits(int index);
    private static native int nativeResultConsumed(int index);
    private static native int[] nativeResultTokenIds(int index);

    // User-history LM (sentences.txt). Persists user picks across
    // sessions so repeated bigrams get a score boost on next decode.
    private static native boolean nativeLoadUserSentence(String path);
    private static native boolean nativeSaveUserSentence(String path);
    private static native void nativeSetUserSentenceEnabled(boolean enabled);
    private static native void nativeLearnUserSentence(int[] context, int[] tokens);

    /** Set true once init has loaded the native resources successfully. */
    private volatile boolean ready = false;

    /**
     * Absolute path to the on-disk user-sentence history file.
     * Set during {@link #doStart} once the data dir is known.
     */
    private volatile String userSentencePath = null;

    /**
     * Number of accepted learns waiting to be persisted. We flush at
     * threshold or on lifecycle hooks (onFinishInput, detach, onTrimMemory).
     */
    private int pendingUserSentenceSaves = 0;
    private static final int USER_SENTENCE_FLUSH_THRESHOLD = 8;
    private static final String USER_SENTENCE_FILENAME = "sentences.txt";

    public boolean isReady() {
        return ready;
    }

    /** Max context tokens the LM can use (n-gram order minus 1). */
    public int contextSize() {
        return ready ? nativeContextSize() : 2;
    }

    /** Hard cache release for memory-pressure callbacks (onTrimMemory). */
    public void resetCaches() {
        if (ready) nativeResetCaches();
    }

    /**
     * Kick off engine initialization on a one-shot background thread.
     * Returns immediately. Until init finishes, {@link #isReady()}
     * returns {@code false} and the decode methods return empty arrays.
     */
    public void start(Context context) {
        final Context appCtx = context.getApplicationContext();
        new Thread(() -> doStart(appCtx), "sime-init").start();
    }

    /**
     * Mark the engine as no longer ready. The native library is
     * process-wide and is not unloaded.
     */
    public void stop() {
        ready = false;
    }

    /** Decode unit input to candidates. */
    public DecodeResult[] decodeSentence(String input, int extra) {
        if (!ready) return new DecodeResult[0];
        int count = nativeDecodeSentence(input, extra);
        return readResults(count);
    }

    /**
     * Num-key (T9 / nine-key) decode.
     */
    public DecodeResult[] decodeNumSentence(String prefixLetters, String digits, int extra) {
        if (!ready) return new DecodeResult[0];
        int count = nativeDecodeNumSentence(
                prefixLetters != null ? prefixLetters : "", digits, extra);
        return readResults(count);
    }

    /**
     * Prediction: suggest next words based on context token IDs.
     */
    public DecodeResult[] nextTokens(int[] contextIds, int limit, boolean enOnly) {
        if (!ready || contextIds == null || contextIds.length == 0)
            return new DecodeResult[0];
        int count = nativeNextTokens(contextIds, limit, enOnly);
        return readResults(count);
    }

    /**
     * Prefix completion: return tokens starting with {@code prefix}.
     */
    public DecodeResult[] getTokens(String prefix, int limit, boolean enOnly) {
        if (!ready || prefix == null || prefix.isEmpty())
            return new DecodeResult[0];
        int count = nativeGetTokens(prefix, limit, enOnly);
        return readResults(count);
    }

    /**
     * Return legal pinyin syllables whose T9 spelling consumes a prefix of
     * {@code digits}. Used to fill the T9 left strip independently of hanzi
     * candidate recall.
     */
    public String[] t9PinyinSyllables(String digits, int limit) {
        if (!ready || digits == null || digits.isEmpty() || limit <= 0)
            return new String[0];
        return nativeT9PinyinSyllables(digits, limit);
    }

    /**
     * Record a single user pick into the in-memory history LM and bump the
     * dirty counter. Triggers a flush once {@link #USER_SENTENCE_FLUSH_THRESHOLD}
     * accumulated learns have happened. Cheap when engine isn't ready —
     * just a no-op.
     */
    public void learnUserSentence(int[] context, int[] tokens) {
        if (!ready || tokens == null || tokens.length == 0) return;
        nativeLearnUserSentence(context != null ? context : new int[0], tokens);
        if (++pendingUserSentenceSaves >= USER_SENTENCE_FLUSH_THRESHOLD) {
            flushUserSentence();
        }
    }

    /**
     * Persist the in-memory user-history to disk if there are pending
     * learns since the last flush. Called from lifecycle hooks
     * (onFinishInput, detach, onTrimMemory) and as the threshold safety
     * net inside {@link #learnUserSentence}.
     */
    public void flushUserSentence() {
        if (!ready || pendingUserSentenceSaves == 0
                || userSentencePath == null) return;
        if (!nativeSaveUserSentence(userSentencePath)) {
            Log.w(TAG, "save user sentence failed: " + userSentencePath);
        }
        pendingUserSentenceSaves = 0;
    }

    // ===== Internals =====

    /** Read stored results from C++ via typed accessors. */
    private static DecodeResult[] readResults(int count) {
        DecodeResult[] results = new DecodeResult[count];
        for (int i = 0; i < count; i++) {
            results[i] = new DecodeResult(
                nativeResultText(i),
                nativeResultUnits(i),
                nativeResultConsumed(i),
                nativeResultTokenIds(i)
            );
        }
        return results;
    }

    private void doStart(Context appCtx) {
        if (!sLoaded) {
            Log.e(TAG, "native library not loaded");
            return;
        }
        try {
            File dataDir = new File(appCtx.getFilesDir(), "sime");
            if (!dataDir.exists() && !dataDir.mkdirs()) {
                Log.e(TAG, "cannot create data dir: " + dataDir);
                return;
            }
            ensureAssetsFresh(appCtx, dataDir);
            String triePath = new File(dataDir, "sime.dict").getAbsolutePath();
            String modelPath = new File(dataDir, "sime.cnt").getAbsolutePath();
            if (!nativeLoadResources(triePath, modelPath)) {
                Log.e(TAG, "nativeLoadResources failed: trie=" + triePath
                        + " model=" + modelPath);
                return;
            }
            // Load any persisted user-sentence history; vocab-signature
            // mismatch (LM regen) silently rejects the old file and a
            // fresh one is written on the next flush.
            userSentencePath =
                new File(dataDir, USER_SENTENCE_FILENAME).getAbsolutePath();
            nativeLoadUserSentence(userSentencePath);
            nativeSetUserSentenceEnabled(true);
            ready = true;
            Log.i(TAG, "engine ready");
        } catch (Throwable t) {
            Log.e(TAG, "engine start failed", t);
        }
    }

    private void ensureAssetsFresh(Context ctx, File dataDir) {
        File marker = new File(dataDir, ASSET_VERSION_MARKER);
        int deployed = readMarkerVersion(marker);
        if (deployed != ASSET_VERSION) {
            Log.i(TAG, "asset version " + deployed + " → " + ASSET_VERSION
                    + ", re-extracting");
            new File(dataDir, "sime.dict").delete();
            new File(dataDir, "sime.cnt").delete();
            new File(dataDir, "sime.ft.dict.txt").delete();
            new File(dataDir, "emoji.txt").delete();
        }
        boolean trieOk    = extractAsset(ctx, "sime.dict", dataDir);
        boolean cntOk     = extractAsset(ctx, "sime.cnt",  dataDir);
        boolean ftOk      = extractAsset(ctx, "sime.ft.dict.txt", dataDir);
        boolean emojiOk   = extractAsset(ctx, "emoji.txt", dataDir);
        if (trieOk && cntOk && ftOk && emojiOk && deployed != ASSET_VERSION) {
            writeMarkerVersion(marker, ASSET_VERSION);
        }
    }

    private static int readMarkerVersion(File marker) {
        if (!marker.exists()) return -1;
        try (FileInputStream fis = new FileInputStream(marker)) {
            byte[] buf = new byte[16];
            int n = fis.read(buf);
            if (n <= 0) return -1;
            return Integer.parseInt(new String(buf, 0, n).trim());
        } catch (IOException | NumberFormatException e) {
            Log.w(TAG, "marker read failed, treating as stale", e);
            return -1;
        }
    }

    private static void writeMarkerVersion(File marker, int version) {
        try (FileOutputStream fos = new FileOutputStream(marker)) {
            fos.write(Integer.toString(version).getBytes());
        } catch (IOException e) {
            Log.w(TAG, "marker write failed", e);
        }
    }

    private static boolean extractAsset(Context context, String assetName, File destDir) {
        File dest = new File(destDir, assetName);
        if (dest.exists() && dest.length() > 0) return true;
        try (InputStream is = context.getAssets().open(assetName);
             FileOutputStream os = new FileOutputStream(dest)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) {
                os.write(buf, 0, n);
            }
            Log.i(TAG, "Extracted " + assetName + " (" + dest.length() + " bytes)");
            return true;
        } catch (IOException e) {
            Log.e(TAG, "Failed to extract " + assetName, e);
            return false;
        }
    }
}
