package com.shiyu.sime.ime.data;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

/**
 * Simplified-to-traditional Chinese converter backed by a parallel vocab
 * file ({@code sime.ft.dict.txt}) where line {@code i} holds the traditional
 * form of token {@code i+1} (TokenID 1-based).
 */
public class TraditionalConverter {

    private volatile String[] table;

    public void load(File ftDict) {
        if (ftDict == null || !ftDict.exists()) return;
        try (BufferedReader br = new BufferedReader(
                new InputStreamReader(new FileInputStream(ftDict), "UTF-8"))) {
            List<String> lines = new ArrayList<>();
            String line;
            while ((line = br.readLine()) != null) {
                lines.add(line);
            }
            table = lines.toArray(new String[0]);
        } catch (Exception e) {
            // Silently fall back to no conversion.
        }
    }

    /**
     * Convert a decoded text string to traditional using per-token lookup.
     *
     * @param tokenIds token IDs from the decoder (1-based)
     * @param text     the simplified text to convert
     * @return traditional form, or the original text if no table is loaded
     */
    public String convert(int[] tokenIds, String text) {
        String[] t = table;
        if (t == null || tokenIds == null || tokenIds.length == 0) return text;
        StringBuilder sb = new StringBuilder();
        for (int id : tokenIds) {
            int idx = id - 1;
            if (idx >= 0 && idx < t.length) {
                sb.append(t[idx]);
            }
        }
        return sb.length() > 0 ? sb.toString() : text;
    }
}
