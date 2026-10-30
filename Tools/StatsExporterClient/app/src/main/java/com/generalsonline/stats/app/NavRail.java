package com.generalsonline.stats.app;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JToggleButton;
import javax.swing.SwingConstants;
import java.awt.CardLayout;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.FontMetrics;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.geom.RoundRectangle2D;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The navigation of the window: a rail on the left with the sections in groups, and the page of the chosen section beside it.
 * Twelve flat tabs say nothing about how they belong together; the groups do (what the game shows, the logs, the match,
 * what you can do to the game, the code).
 */
@SuppressWarnings("serial")
final class NavRail extends JPanel {
    private static final int WIDTH = 190;

    private final JPanel rail = new JPanel();
    private final JPanel pages = new JPanel(new CardLayout());
    private final Map<String, Item> items = new LinkedHashMap<>();
    private final Map<String, JComponent> components = new LinkedHashMap<>();
    private String selected;
    private Runnable onChange = () -> {
    };

    NavRail() {
        super(new java.awt.BorderLayout());
        rail.setLayout(new BoxLayout(rail, BoxLayout.Y_AXIS));
        rail.setBackground(Theme.BG);
        rail.setBorder(BorderFactory.createEmptyBorder(8, 10, 10, 10));
        JPanel railHolder = new JPanel(new java.awt.BorderLayout());
        railHolder.setBackground(Theme.BG);
        railHolder.setBorder(BorderFactory.createMatteBorder(0, 0, 0, 1, Theme.HAIRLINE));
        javax.swing.JScrollPane railScroll = new javax.swing.JScrollPane(rail, javax.swing.JScrollPane.VERTICAL_SCROLLBAR_AS_NEEDED, javax.swing.JScrollPane.HORIZONTAL_SCROLLBAR_NEVER);
        railScroll.setBorder(BorderFactory.createEmptyBorder());
        railScroll.getViewport().setBackground(Theme.BG);
        railHolder.add(railScroll, java.awt.BorderLayout.CENTER);
        railHolder.setPreferredSize(new Dimension(WIDTH, 100));
        pages.setBackground(Theme.SURFACE);
        add(railHolder, java.awt.BorderLayout.WEST);
        add(pages, java.awt.BorderLayout.CENTER);
    }

    /** Starts a group of sections under a heading. */
    void group(String title) {
        JLabel label = new JLabel(title);
        label.setFont(Theme.semibold(11));
        label.setForeground(Theme.DIM);
        label.setBorder(BorderFactory.createEmptyBorder(rail.getComponentCount() == 0 ? 6 : 16, 10, 5, 0));
        label.setAlignmentX(Component.LEFT_ALIGNMENT);
        rail.add(label);
    }

    void add(String title, JComponent page) {
        Item item = new Item(title);
        item.setAlignmentX(Component.LEFT_ALIGNMENT);
        item.addActionListener(e -> select(title));
        rail.add(item);
        rail.add(Box.createVerticalStrut(2));
        items.put(title, item);
        components.put(title, page);
        pages.add(page, title);
        if (selected == null) {
            select(title);
        }
    }

    /** Shows the page of a section. */
    void select(String title) {
        if (!items.containsKey(title)) {
            return;
        }
        selected = title;
        for (Map.Entry<String, Item> entry : items.entrySet()) {
            entry.getValue().setSelected(entry.getKey().equals(title));
        }
        ((CardLayout) pages.getLayout()).show(pages, title);
        onChange.run();
    }

    void select(Component page) {
        for (Map.Entry<String, JComponent> entry : components.entrySet()) {
            if (entry.getValue() == page) {
                select(entry.getKey());
            }
        }
    }

    String selected() {
        return selected;
    }

    List<String> titles() {
        return new ArrayList<>(items.keySet());
    }

    JComponent page(String title) {
        return components.get(title);
    }

    /** A small count at the right of a section, such as the number of events; an empty text hides it. */
    void badge(String title, String text) {
        Item item = items.get(title);
        if (item != null && !text.equals(item.badge)) {
            item.badge = text;
            item.repaint();
        }
    }

    void onChange(Runnable listener) {
        this.onChange = listener;
    }

    /** One section in the rail: a label, an optional count, and a bar in the accent when it is the open one. */
    private static final class Item extends JToggleButton {
        private String badge = "";
        private boolean hover;

        Item(String title) {
            super(title);
            setHorizontalAlignment(SwingConstants.LEFT);
            setFont(Theme.ui(java.awt.Font.PLAIN, 13));
            setForeground(Theme.MUTED);
            setFocusPainted(false);
            setContentAreaFilled(false);
            setBorderPainted(false);
            setOpaque(false);
            setBorder(BorderFactory.createEmptyBorder(7, 14, 7, 12));
            setMaximumSize(new Dimension(Integer.MAX_VALUE, 34));
            setCursor(java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.HAND_CURSOR));
            addMouseListener(new MouseAdapter() {
                @Override
                public void mouseEntered(MouseEvent e) {
                    hover = true;
                    repaint();
                }

                @Override
                public void mouseExited(MouseEvent e) {
                    hover = false;
                    repaint();
                }
            });
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
                boolean on = isSelected();
                if (on || hover) {
                    g.setColor(on ? Theme.RAISED : Theme.alpha(Theme.RAISED, 150));
                    g.fill(new RoundRectangle2D.Double(0, 0, getWidth(), getHeight(), 8, 8));
                }
                if (on) {
                    g.setColor(Theme.ACCENT);
                    g.fill(new RoundRectangle2D.Double(0, 7, 3, getHeight() - 14, 3, 3));
                }
                g.setFont(on ? Theme.semibold(13) : getFont());
                g.setColor(on ? Theme.TEXT : hover ? Theme.TEXT : Theme.MUTED);
                FontMetrics metrics = g.getFontMetrics();
                int baseline = (getHeight() - metrics.getHeight()) / 2 + metrics.getAscent();
                g.drawString(getText(), 14, baseline);
                if (!badge.isEmpty()) {
                    g.setFont(Theme.ui(java.awt.Font.PLAIN, 11));
                    FontMetrics small = g.getFontMetrics();
                    g.setColor(on ? Theme.ACCENT : Theme.DIM);
                    g.drawString(badge, getWidth() - 12 - small.stringWidth(badge), (getHeight() - small.getHeight()) / 2 + small.getAscent());
                }
            } finally {
                g.dispose();
            }
        }
    }
}
