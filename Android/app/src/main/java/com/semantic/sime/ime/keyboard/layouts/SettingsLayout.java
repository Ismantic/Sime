package com.semantic.sime.ime.keyboard.layouts;

import com.semantic.sime.R;
import com.semantic.sime.ime.keyboard.framework.KeyDef;
import com.semantic.sime.ime.keyboard.framework.KeyRow;
import com.semantic.sime.ime.keyboard.framework.KeyboardLayout;
import com.semantic.sime.ime.theme.Typography;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

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

    /** Label → drawable mapping for the top-level settings panel. */
    private static final Map<String, Integer> ICONS = new HashMap<>();
    static {
        ICONS.put("键盘",   R.drawable.ic_settings_keyboard);
        ICONS.put("表情",   R.drawable.ic_settings_emoji);
        ICONS.put("剪切板", R.drawable.ic_settings_clipboard);
        ICONS.put("常用语", R.drawable.ic_settings_quote);
        ICONS.put("声音",   R.drawable.ic_settings_volume);
        ICONS.put("震动",   R.drawable.ic_settings_vibration);
        ICONS.put("繁体",   R.drawable.ic_settings_translate);
        ICONS.put("联想",   R.drawable.ic_settings_lightbulb);
        ICONS.put("全键盘", R.drawable.ic_settings_keyboard_alt);
        ICONS.put("九宫格", R.drawable.ic_settings_dialpad);
    }

    private SettingsLayout() {}

    public static KeyboardLayout build(SettingsNode current) {
        KeyboardLayout.Builder b = KeyboardLayout.builder()
                .horizontalPadding(20)
                .verticalPadding(20)
                .keyMargin(8);

        List<SettingsNode> kids = current.children;
        for (int r = 0; r < ROWS; r++) {
            KeyRow.Builder row = KeyRow.builder(1f);
            for (int c = 0; c < COLS; c++) {
                int idx = r * COLS + c;
                if (idx < kids.size()) {
                    SettingsNode k = kids.get(idx);
                    KeyDef.Builder kb = KeyDef.normal(k.label, null)
                            .id(ID_CHILD_PREFIX + idx)
                            .labelSize(Typography.CAPTION);
                    Integer iconRes = ICONS.get(k.label);
                    if (iconRes != null) kb.icon(iconRes);
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
