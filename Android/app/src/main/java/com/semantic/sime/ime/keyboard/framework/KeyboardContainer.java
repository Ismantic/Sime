package com.semantic.sime.ime.keyboard.framework;

import android.content.Context;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;

import com.semantic.sime.ime.keyboard.SimeKey;
import com.semantic.sime.ime.theme.SimeTheme;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * ViewGroup that owns a single {@link KeyboardLayout} and lays out one
 * {@link KeyView} per {@link KeyDef}. Replaces the old hand-rolled
 * nested-LinearLayout approach.
 *
 * <p>Layout strategy: each row's height = total_h * (rowWeight / sum)
 * minus row's vertical padding band; each key in the row gets width =
 * row_w * (keyWeight / row_sum). All margin/padding values come from
 * the {@link KeyboardLayout} constants in dp, converted here.
 *
 * <p>Use {@link #setLayout} to install or replace the layout (e.g. when
 * the SettingsKeyboardView navigates to a sub-menu). Use
 * {@link #findKeyById(String)} to look up keys with explicit ids for
 * runtime label updates (Qwerty shift, T9 双态键, settings selection).
 */
public class KeyboardContainer extends ViewGroup {

    public interface OnKeyEmitListener {
        void onKey(SimeKey key);
    }

    private final SimeTheme theme;
    private KeyboardLayout layout;
    private OnKeyEmitListener emitListener;

    private final Map<String, KeyView> idIndex = new HashMap<>();
    /**
     * Per-row list of KeyViews in row order — used by onLayout to compute
     * key bounds without re-walking the children list.
     */
    private final List<List<KeyView>> rowsKeys = new ArrayList<>();

    public KeyboardContainer(Context context, SimeTheme theme) {
        super(context);
        this.theme = theme;
    }

    public void setOnKeyEmitListener(OnKeyEmitListener l) {
        this.emitListener = l;
    }

    public void setLayout(KeyboardLayout newLayout) {
        this.layout = newLayout;
        rebuild();
    }

    public KeyView findKeyById(String id) {
        return idIndex.get(id);
    }

    private void rebuild() {
        removeAllViews();
        idIndex.clear();
        rowsKeys.clear();
        if (layout == null) return;

        setPadding(
                dp(layout.horizontalPaddingDp),
                dp(layout.verticalPaddingDp),
                dp(layout.horizontalPaddingDp),
                dp(layout.verticalPaddingDp));

        KeyView.OnKeyAction relay = (def, action) -> {
            if (emitListener == null) return;
            switch (action) {
                case CLICK:
                    if (def.clickAction != null) emitListener.onKey(def.clickAction);
                    break;
                case LONG_PRESS:
                    if (def.longPressAction != null) emitListener.onKey(def.longPressAction);
                    break;
            }
        };

        for (KeyRow row : layout.rows) {
            List<KeyView> rowViews = new ArrayList<>(row.keys.size());
            for (KeyDef def : row.keys) {
                KeyView kv = new KeyView(getContext(), theme, def, layout.keyMarginDp);
                kv.setListener(relay);
                addView(kv);
                rowViews.add(kv);
                if (def.id != null) idIndex.put(def.id, kv);
            }
            rowsKeys.add(rowViews);
        }
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int w = MeasureSpec.getSize(widthMeasureSpec);
        int h = MeasureSpec.getSize(heightMeasureSpec);

        // Measure each child to its share of width/height. We use
        // EXACTLY specs derived from row + column weights so children
        // know their exact size.
        if (layout != null) {
            float totalRowWeight = 0f;
            for (KeyRow r : layout.rows) totalRowWeight += r.heightWeight;

            int innerW = w - getPaddingLeft() - getPaddingRight();
            int innerH = h - getPaddingTop() - getPaddingBottom();

            for (int ri = 0; ri < layout.rows.size(); ri++) {
                KeyRow row = layout.rows.get(ri);
                int rowH = totalRowWeight > 0
                        ? Math.round(innerH * (row.heightWeight / totalRowWeight))
                        : 0;
                float colSum = 0f;
                for (KeyDef d : row.keys) colSum += d.widthWeight;

                List<KeyView> rowViews = rowsKeys.get(ri);
                for (int ci = 0; ci < rowViews.size(); ci++) {
                    KeyView kv = rowViews.get(ci);
                    int kw = colSum > 0
                            ? Math.round(innerW * (row.keys.get(ci).widthWeight / colSum))
                            : 0;
                    kv.measure(
                            MeasureSpec.makeMeasureSpec(kw, MeasureSpec.EXACTLY),
                            MeasureSpec.makeMeasureSpec(rowH, MeasureSpec.EXACTLY));
                }
            }
        }
        setMeasuredDimension(
                resolveSize(w, widthMeasureSpec),
                resolveSize(h, heightMeasureSpec));
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        if (layout == null) return;

        int innerL = getPaddingLeft();
        int innerT = getPaddingTop();
        int innerR = getWidth() - getPaddingRight();
        int innerB = getHeight() - getPaddingBottom();
        int innerW = innerR - innerL;
        int innerH = innerB - innerT;

        float totalRowWeight = 0f;
        for (KeyRow row : layout.rows) totalRowWeight += row.heightWeight;
        if (totalRowWeight <= 0) return;

        int y = innerT;
        for (int ri = 0; ri < layout.rows.size(); ri++) {
            KeyRow row = layout.rows.get(ri);
            int rowH = Math.round(innerH * (row.heightWeight / totalRowWeight));

            float colSum = 0f;
            for (KeyDef d : row.keys) colSum += d.widthWeight;

            List<KeyView> rowViews = rowsKeys.get(ri);
            int x = innerL;
            for (int ci = 0; ci < rowViews.size(); ci++) {
                KeyDef d = row.keys.get(ci);
                int kw = colSum > 0
                        ? Math.round(innerW * (d.widthWeight / colSum))
                        : 0;
                View kv = rowViews.get(ci);
                kv.layout(x, y, x + kw, y + rowH);
                x += kw;
            }
            y += rowH;
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }
}
