package com.generalsonline.stats.app;

import javax.swing.SwingUtilities;
import java.awt.Component;
import java.awt.Container;
import java.awt.Dimension;
import java.awt.Insets;
import java.awt.LayoutManager;

/**
 * Lays components out left to right and wraps to a new row when the container is too narrow, and reports the height of
 * the wrapped rows, so a toolbar grows when the window gets narrow instead of clipping. The first component starts at the
 * left edge exactly (a FlowLayout would indent it by the gap).
 */
final class WrapLayout implements LayoutManager {
    private final int hgap;
    private final int vgap;
    private final boolean centered;
    private int lastHeight = -1;

    /** @param centered whether the parts of a row sit on its middle (controls) or on its top edge (cards of different heights) */
    WrapLayout(int hgap, int vgap, boolean centered) {
        this.hgap = hgap;
        this.vgap = vgap;
        this.centered = centered;
    }

    @Override
    public void addLayoutComponent(String name, Component comp) {
    }

    @Override
    public void removeLayoutComponent(Component comp) {
    }

    @Override
    public Dimension preferredLayoutSize(Container target) {
        return size(target, true);
    }

    @Override
    public Dimension minimumLayoutSize(Container target) {
        return size(target, false);
    }

    /** The width the rows may use: the container's, or the nearest ancestor's while the container has none yet. */
    private static int availableWidth(Container target) {
        Container holder = target;
        while (holder.getWidth() == 0 && holder.getParent() != null) {
            holder = holder.getParent();
        }
        int width = holder.getWidth();
        Insets insets = target.getInsets();
        return width == 0 ? Integer.MAX_VALUE : width - insets.left - insets.right;
    }

    private Dimension size(Container target, boolean preferred) {
        synchronized (target.getTreeLock()) {
            int max = availableWidth(target);
            int width = 0;
            int height = 0;
            int rowWidth = 0;
            int rowHeight = 0;
            for (Component part : target.getComponents()) {
                if (!part.isVisible()) {
                    continue;
                }
                Dimension d = preferred ? part.getPreferredSize() : part.getMinimumSize();
                if (rowWidth > 0 && rowWidth + hgap + d.width > max) {
                    width = Math.max(width, rowWidth);
                    height += rowHeight + vgap;
                    rowWidth = 0;
                    rowHeight = 0;
                }
                rowWidth += (rowWidth > 0 ? hgap : 0) + d.width;
                rowHeight = Math.max(rowHeight, d.height);
            }
            width = Math.max(width, rowWidth);
            height += rowHeight;
            Insets insets = target.getInsets();
            return new Dimension(width + insets.left + insets.right, height + insets.top + insets.bottom);
        }
    }

    @Override
    public void layoutContainer(Container target) {
        synchronized (target.getTreeLock()) {
            Insets insets = target.getInsets();
            int max = target.getWidth() - insets.left - insets.right;
            int x = insets.left;
            int y = insets.top;
            int rowHeight = 0;
            int rowStart = 0;
            Component[] parts = target.getComponents();
            for (int i = 0; i < parts.length; i++) {
                Component part = parts[i];
                if (!part.isVisible()) {
                    continue;
                }
                Dimension d = part.getPreferredSize();
                if (x > insets.left && x - insets.left + d.width > max) {
                    centerRow(parts, rowStart, i, y, rowHeight);
                    y += rowHeight + vgap;
                    x = insets.left;
                    rowHeight = 0;
                    rowStart = i;
                }
                part.setSize(d.width, d.height);
                part.setLocation(x, y);
                x += d.width + hgap;
                rowHeight = Math.max(rowHeight, d.height);
            }
            centerRow(parts, rowStart, parts.length, y, rowHeight);
            // The height that was asked for before the width was known may be wrong; ask the parents again once.
            int needed = size(target, true).height;
            if (needed != lastHeight) {
                lastHeight = needed;
                SwingUtilities.invokeLater(target::revalidate);
            }
        }
    }

    /** Centers the parts of one row on its middle, so a label sits level with the field next to it. */
    private void centerRow(Component[] parts, int from, int to, int y, int rowHeight) {
        for (int i = from; i < to; i++) {
            if (!parts[i].isVisible()) {
                continue;
            }
            if (centered) {
                parts[i].setLocation(parts[i].getX(), y + (rowHeight - parts[i].getHeight()) / 2);
            } else {
                parts[i].setSize(parts[i].getWidth(), rowHeight);
            }
        }
    }
}
