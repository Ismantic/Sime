package com.semantic.sime;

import android.content.Context;
import android.util.Log;

import com.semantic.sime.ime.engine.DecodeResult;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;

/**
 * Java wrapper for the native SIME engine.
 *
 * <p>Asset extraction and {@code nativeLoadResources} run on a one-shot
 * background thread (started by {@link #start(Context)}); decode methods
 * are synchronous and called from whatever thread the IME is currently
 * on (in practice, the Android main thread). The decoder is fast enough
 * (single-digit milliseconds for typical inputs) that on-main-thread
 * execution does not cause perceptible jank.
 *
 * <p>The single shared bit of state between init and decode is the
 * {@code volatile boolean ready} flag: init flips it to {@code true}
 * after {@code nativeLoadResources} returns successfully, and decode
 * methods return an empty result while it's {@code false}.
 */
public class SimeEngine {
    private static final String TAG = "SimeEngine";

    /**
     * Bumped whenever the bundled {@code sime.dict} / {@code sime.cnt}
     * assets change. The deployed copy in app private storage carries
     * a marker file with the version it was extracted from; mismatches
     * trigger a re-extract on the next {@link #start(Context)}.
     */
    private static final int ASSET_VERSION = 3;
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
    private static native String[] nativeDecodeSentence(String input, int extra);
    private static native String[] nativeDecodeNumSentence(String prefixLetters, String digits, int extra);
    private static native boolean nativeIsReady();
    private static native String[] nativeNextTokens(int[] contextIds, int limit, boolean enOnly);
    private static native String[] nativeGetTokens(String prefix, int limit, boolean enOnly);

    /** Set true once init has loaded the native resources successfully. */
    private volatile boolean ready = false;

    public boolean isReady() {
        return ready;
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
        return parseQuads(nativeDecodeSentence(input, extra));
    }

    /**
     * Num-key (T9 / nine-key) decode: digit string with an optional
     * pinyin prefix. Mirrors {@code sime::Interpreter::DecodeNumSentence}.
     *
     * @param extra extra Layer 1 sentences beyond the top one (0 means
     *              just the single best sentence; Layer 2 word/char
     *              alternatives are always returned in full)
     */
    public DecodeResult[] decodeNumSentence(String prefixLetters, String digits, int extra) {
        if (!ready) return new DecodeResult[0];
        return parseQuads(nativeDecodeNumSentence(
                prefixLetters != null ? prefixLetters : "", digits, extra));
    }

    /**
     * Prediction: suggest next words based on context token IDs.
     *
     * @param enOnly when true, only English tokens are returned
     */
    public DecodeResult[] nextTokens(int[] contextIds, int limit, boolean enOnly) {
        if (!ready || contextIds == null || contextIds.length == 0)
            return new DecodeResult[0];
        return parseQuads(nativeNextTokens(contextIds, limit, enOnly));
    }

    /**
     * Prefix completion: return tokens starting with {@code prefix}.
     *
     * @param enOnly when true, only the English DAT is searched
     */
    public DecodeResult[] getTokens(String prefix, int limit, boolean enOnly) {
        if (!ready || prefix == null || prefix.isEmpty())
            return new DecodeResult[0];
        return parseQuads(nativeGetTokens(prefix, limit, enOnly));
    }

    // ===== Internals =====

    /** Parse JNI quad array into DecodeResult[]. */
    private static DecodeResult[] parseQuads(String[] raw) {
        DecodeResult[] result = new DecodeResult[raw.length / 4];
        for (int i = 0; i < result.length; i++) {
            result[i] = new DecodeResult(
                raw[i * 4],
                raw[i * 4 + 1],
                Integer.parseInt(raw[i * 4 + 2]),
                DecodeResult.parseTokenIds(raw[i * 4 + 3])
            );
        }
        return result;
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
            ready = true;
            Log.i(TAG, "engine ready");
        } catch (Throwable t) {
            Log.e(TAG, "engine start failed", t);
        }
    }

    /**
     * Compare the deployed asset version with {@link #ASSET_VERSION} and
     * delete the cached files if they don't match, so the subsequent
     * {@link #extractAsset} calls re-extract from the APK. The new
     * version is written back to the marker file after both extracts
     * succeed.
     */
    private void ensureAssetsFresh(Context ctx, File dataDir) {
        File marker = new File(dataDir, ASSET_VERSION_MARKER);
        int deployed = readMarkerVersion(marker);
        if (deployed != ASSET_VERSION) {
            Log.i(TAG, "asset version " + deployed + " → " + ASSET_VERSION
                    + ", re-extracting");
            new File(dataDir, "sime.dict").delete();
            new File(dataDir, "sime.cnt").delete();
        }
        boolean trieOk = extractAsset(ctx, "sime.dict", dataDir);
        boolean cntOk  = extractAsset(ctx, "sime.cnt",  dataDir);
        if (trieOk && cntOk && deployed != ASSET_VERSION) {
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

    /** Returns true on success (file exists and is non-empty after the call). */
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
