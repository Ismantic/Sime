package com.isma.sime.ime.keyboard.layouts;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.function.BooleanSupplier;

/**
 * Node in the settings menu tree. Either a category (has children, no
 * action) or a leaf (no children, has action). The {@code isSelected}
 * predicate is checked when the layer is rendered to mark the current
 * value of an option (e.g. "九宫格" is selected when the saved layout
 * is T9).
 *
 * <p>Tree is built by {@link SettingsTree}; rendered to a 4×2 keyboard
 * grid by {@link SettingsLayout}.
 */
public final class SettingsNode {

    public final String label;
    public final List<SettingsNode> children;
    public final Runnable action;
    public final BooleanSupplier isSelected;

    private SettingsNode(String label,
                         List<SettingsNode> children,
                         Runnable action,
                         BooleanSupplier isSelected) {
        this.label = label;
        this.children = children != null
                ? Collections.unmodifiableList(children)
                : Collections.<SettingsNode>emptyList();
        this.action = action;
        this.isSelected = isSelected;
    }

    public boolean isLeaf() {
        return action != null;
    }

    public static SettingsNode category(String label, SettingsNode... children) {
        List<SettingsNode> list = new ArrayList<>(children.length);
        for (SettingsNode c : children) list.add(c);
        return new SettingsNode(label, list, null, null);
    }

    public static SettingsNode leaf(String label, Runnable action,
                                    BooleanSupplier isSelected) {
        return new SettingsNode(label, null, action, isSelected);
    }

    public static SettingsNode leaf(String label, Runnable action) {
        return leaf(label, action, null);
    }
}
