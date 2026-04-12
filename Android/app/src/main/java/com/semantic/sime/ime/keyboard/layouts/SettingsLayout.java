package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;

import java.util.List;

/**
 * Render a {@link SettingsNode}'s children into a 4×2 keyboard grid.
 * Each child becomes one cell with id {@link #ID_CHILD_PREFIX}{@code i}
 * (i = index in the children list). Empty cells are filled with
 * {@link KeyDef#empty(float)}.
 *
 * <p>The "back" affordance is <b>not</b> in the grid — it lives on the
 * candidates bar (see {@code CandidatesBar.setSettingsMode}).
 */
public final class SettingsLayout {

    public static final int COLS = 4;
    public static final int ROWS = 2;
    public static final int CAPACITY = COLS * ROWS;

    public static final String ID_CHILD_PREFIX = "settings.child.";

    private SettingsLayout() {}

    public static KeyboardLayout build(SettingsNode current) {
        KeyboardLayout.Builder b = KeyboardLayout.builder()
                .horizontalPadding(8)
                .verticalPadding(8)
                .keyMargin(4);

        List<SettingsNode> kids = current.children;
        for (int r = 0; r < ROWS; r++) {
            KeyRow.Builder row = KeyRow.builder(1f);
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                if (idx < kids.size()) {
                    SettingsNode k = kids.get(idx);
                    boolean selected = k.isSelected != null && k.isSelected.getAsBoolean();
                    KeyDef.Builder kb = (selected
                            ? KeyDef.accent(k.label, null)
                            : KeyDef.normal(k.label, null))
                            .id(ID_CHILD_PREFIX + idx)
                            .width(1f)
                            .labelSize(16f);
                    row.key(kb);
                } else {
                    row.key(KeyDef.empty(1f));
                }
            }
            b.row(row);
        }
        return b.build();
    }
}
