package com.generalsonline.stats.app;

import com.generalsonline.stats.app.Theme.Tone;

import javax.swing.BorderFactory;
import javax.swing.Box;
import javax.swing.BoxLayout;
import javax.swing.Icon;
import javax.swing.JButton;
import javax.swing.JComponent;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTextArea;
import javax.swing.JViewport;
import javax.swing.Scrollable;
import javax.swing.border.Border;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.FlowLayout;
import java.awt.Font;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.GridBagConstraints;
import java.awt.GridBagLayout;
import java.awt.Insets;
import java.awt.Rectangle;
import java.awt.RenderingHints;
import java.awt.geom.Ellipse2D;
import java.awt.geom.RoundRectangle2D;

/** The shared pieces of the window, built from {@link Theme}: cards, forms, toolbars, buttons, pills and scroll panes. */
final class Ui {
    static final int PAGE_PADDING = 14;
    static final int GAP = 12;

    private Ui() {
    }

    // -- Text.

    static JLabel caption(String text) {
        JLabel label = new JLabel(text);
        label.setForeground(Theme.MUTED);
        return label;
    }

    static JLabel heading(String text) {
        JLabel label = new JLabel(text);
        label.setFont(Theme.semibold(13));
        label.setForeground(Theme.TEXT);
        return label;
    }

    /** A small muted label in a row of controls. */
    static void mute(Component part) {
        if (part instanceof JLabel && ((JLabel) part).getClientProperty("keepColor") == null) {
            part.setForeground(Theme.MUTED);
        }
    }

    // -- Surfaces.

    /** A panel with a filled, rounded, outlined face. */
    @SuppressWarnings("serial")
    static class Surface extends JPanel {
        private final Color fill;
        private final Color line;
        private final int arc;
        private int fixedWidth;

        Surface(Color fill, Color line, int arc) {
            this.fill = fill;
            this.line = line;
            this.arc = arc;
            setOpaque(false);
        }

        /** Gives the card a width of its own (its height stays what the content needs), for cards that wrap in a flow. */
        Surface width(int width) {
            this.fixedWidth = width;
            return this;
        }

        @Override
        public Dimension getPreferredSize() {
            Dimension size = super.getPreferredSize();
            return fixedWidth > 0 ? new Dimension(fixedWidth, size.height) : size;
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setColor(fill);
                g.fill(new RoundRectangle2D.Double(0.5, 0.5, getWidth() - 1, getHeight() - 1, arc, arc));
                if (line != null) {
                    g.setColor(line);
                    g.draw(new RoundRectangle2D.Double(0.5, 0.5, getWidth() - 1, getHeight() - 1, arc, arc));
                }
            } finally {
                g.dispose();
            }
            super.paintComponent(graphics);
        }
    }

    /** A titled card. The body fills it; the optional hint sits under the title in muted type. */
    static Surface card(String title, String hint, JComponent body) {
        Surface card = new Surface(Theme.RAISED, Theme.BORDER, 12);
        card.setLayout(new BorderLayout(0, 10));
        card.setBorder(BorderFactory.createEmptyBorder(12, 14, 14, 14));
        JPanel head = new JPanel();
        head.setOpaque(false);
        head.setLayout(new BoxLayout(head, BoxLayout.Y_AXIS));
        JLabel titleLabel = heading(title);
        titleLabel.setAlignmentX(Component.LEFT_ALIGNMENT);
        head.add(titleLabel);
        if (hint != null && !hint.isEmpty()) {
            JLabel hintLabel = new WrapLabel();
            hintLabel.setText("<html>" + PanelKit.escape(hint) + "</html>");
            hintLabel.setFont(Theme.ui(Font.PLAIN, Theme.TEXT_SIZE));
            hintLabel.setForeground(Theme.MUTED);
            hintLabel.setAlignmentX(Component.LEFT_ALIGNMENT);
            hintLabel.setBorder(BorderFactory.createEmptyBorder(2, 0, 0, 0));
            head.add(hintLabel);
        }
        card.add(head, BorderLayout.NORTH);
        card.add(body, BorderLayout.CENTER);
        return card;
    }

    /** A titled region around a table or text, flat on the page: a small heading over a bordered scroll pane. */
    static JPanel section(String title, JComponent body) {
        JPanel panel = new JPanel(new BorderLayout(0, 6));
        panel.setOpaque(false);
        JLabel label = heading(title);
        label.setBorder(BorderFactory.createEmptyBorder(2, 2, 0, 0));
        panel.add(label, BorderLayout.NORTH);
        panel.add(body, BorderLayout.CENTER);
        return panel;
    }

    /** The padding every page of the window uses. */
    static <T extends JComponent> T pad(T component) {
        component.setBorder(BorderFactory.createEmptyBorder(PAGE_PADDING, PAGE_PADDING, PAGE_PADDING, PAGE_PADDING));
        return component;
    }

    static JScrollPane scroll(Component view) {
        JScrollPane pane = new JScrollPane(view);
        pane.setBorder(BorderFactory.createLineBorder(Theme.HAIRLINE));
        pane.getViewport().setBackground(Theme.FIELD);
        pane.getVerticalScrollBar().setUnitIncrement(24);
        pane.getHorizontalScrollBar().setUnitIncrement(24);
        return pane;
    }

    /** A page that scrolls as a whole: it follows the width of the window and grows taller than it only when it must. */
    static JScrollPane pageScroll(Component view) {
        ScrollPage page = new ScrollPage();
        page.add(view, BorderLayout.CENTER);
        JScrollPane pane = scroll(page);
        pane.setBorder(BorderFactory.createEmptyBorder());
        pane.getViewport().setBackground(Theme.SURFACE);
        return pane;
    }

    /** The content of {@link #pageScroll}: tracks the width of the viewport, and its height while it is shorter than the viewport. */
    @SuppressWarnings("serial")
    private static final class ScrollPage extends JPanel implements Scrollable {
        ScrollPage() {
            super(new BorderLayout());
            setOpaque(false);
        }

        @Override
        public Dimension getPreferredScrollableViewportSize() {
            return getPreferredSize();
        }

        @Override
        public int getScrollableUnitIncrement(Rectangle visible, int orientation, int direction) {
            return 24;
        }

        @Override
        public int getScrollableBlockIncrement(Rectangle visible, int orientation, int direction) {
            return Math.max(24, visible.height - 40);
        }

        @Override
        public boolean getScrollableTracksViewportWidth() {
            return true;
        }

        @Override
        public boolean getScrollableTracksViewportHeight() {
            return getParent() instanceof JViewport && getPreferredSize().height < getParent().getHeight();
        }
    }

    static JSplitPane split(int orientation, Component first, Component second, double weight) {
        JSplitPane split = new JSplitPane(orientation, first, second);
        split.setResizeWeight(weight);
        split.setBorder(BorderFactory.createEmptyBorder());
        split.setContinuousLayout(true);
        return split;
    }

    // -- Toolbars and forms.

    /** A row of controls that wraps when the window is narrow. Captions in it are muted. */
    static JPanel bar(Component... parts) {
        JPanel panel = new WrapPanel(new WrapLayout(8, 6, true));
        for (Component part : parts) {
            mute(part);
            panel.add(part);
        }
        return panel;
    }

    /** A panel whose rows wrap: it asks its layout for the size every time, because the height depends on the width it was given last. */
    @SuppressWarnings("serial")
    private static final class WrapPanel extends JPanel {
        WrapPanel(WrapLayout layout) {
            super(layout);
            setOpaque(false);
        }

        @Override
        public Dimension getPreferredSize() {
            return isPreferredSizeSet() ? super.getPreferredSize() : getLayout().preferredLayoutSize(this);
        }
    }

    /** Cards side by side that wrap onto new rows when the window is narrow. */
    static JPanel flow(JComponent... cards) {
        JPanel panel = new WrapPanel(new WrapLayout(Ui.GAP, Ui.GAP, false));
        for (JComponent card : cards) {
            panel.add(card);
        }
        return panel;
    }

    /** A caption and the control it names, close together; groups of them in a toolbar are further apart. */
    static JPanel labeled(String caption, Component control) {
        JPanel panel = line(caption(caption), control);
        panel.remove(panel.getComponentCount() - 1);
        return panel;
    }

    /**
     * The row above a table or a text: controls in groups that wrap, and a quiet hint at the right edge that gives way
     * (it is hidden, and shown as the tooltip of the row) when the row is too narrow for both.
     */
    @SuppressWarnings("serial")
    static final class Toolbar extends JPanel {
        private final JPanel left = new WrapPanel(new WrapLayout(16, 6, true));
        private final JLabel hint = new JLabel();

        Toolbar(Component... parts) {
            super(new BorderLayout(16, 0));
            setOpaque(false);
            setBorder(BorderFactory.createEmptyBorder(0, 0, 10, 0));
            for (Component part : parts) {
                mute(part);
                left.add(part);
            }
            hint.setFont(small());
            hint.setForeground(Theme.DIM);
            if (parts.length == 0) {
                add(hint, BorderLayout.CENTER);
            } else {
                add(left, BorderLayout.CENTER);
                add(hint, BorderLayout.EAST);
            }
        }

        Toolbar hint(String text) {
            hint.setText(text);
            return this;
        }

        /** A hairline under the whole row, for a row of tabs. */
        Toolbar underlined() {
            setBorder(BorderFactory.createCompoundBorder(BorderFactory.createEmptyBorder(0, 0, 10, 0),
                BorderFactory.createMatteBorder(0, 0, 1, 0, Theme.HAIRLINE)));
            return this;
        }

        @Override
        public void doLayout() {
            boolean room = left.getComponentCount() == 0 || getWidth() >= left.getPreferredSize().width + hint.getPreferredSize().width + 40;
            hint.setVisible(room && !hint.getText().isEmpty());
            setToolTipText(hint.isVisible() || hint.getText().isEmpty() ? null : hint.getText());
            super.doLayout();
        }
    }

    /** A row of tabs that switch between views of one page: the open one is lit and underlined in the accent. */
    @SuppressWarnings("serial")
    static final class TabStrip extends JPanel {
        private final java.util.List<TabButton> tabs = new java.util.ArrayList<>();
        private final java.util.function.IntConsumer onSelect;

        TabStrip(java.util.List<String> names, java.util.function.IntConsumer onSelect) {
            super(new WrapLayout(6, 0, true));
            this.onSelect = onSelect;
            setOpaque(false);
            for (int i = 0; i < names.size(); i++) {
                int index = i;
                TabButton tab = new TabButton(names.get(i));
                tab.addActionListener(e -> select(index));
                tabs.add(tab);
                add(tab);
            }
            if (!tabs.isEmpty()) {
                tabs.get(0).setSelected(true);
            }
        }

        /** Renames a tab, for a count beside its name. */
        void setText(int index, String text) {
            if (!tabs.get(index).getText().equals(text)) {
                tabs.get(index).setText(text);
                revalidate();
            }
        }

        void select(int index) {
            for (int i = 0; i < tabs.size(); i++) {
                tabs.get(i).setSelected(i == index);
            }
            onSelect.accept(index);
        }

        @Override
        public Dimension getPreferredSize() {
            return isPreferredSizeSet() ? super.getPreferredSize() : getLayout().preferredLayoutSize(this);
        }
    }

    @SuppressWarnings("serial")
    private static final class TabButton extends javax.swing.JToggleButton {
        private boolean hover;

        TabButton(String text) {
            super(text);
            setFont(Theme.semibold(12.5f));
            setFocusPainted(false);
            setContentAreaFilled(false);
            setBorderPainted(false);
            setOpaque(false);
            setBorder(BorderFactory.createEmptyBorder(8, 12, 9, 12));
            setCursor(java.awt.Cursor.getPredefinedCursor(java.awt.Cursor.HAND_CURSOR));
            addMouseListener(new java.awt.event.MouseAdapter() {
                @Override
                public void mouseEntered(java.awt.event.MouseEvent e) {
                    hover = true;
                    repaint();
                }

                @Override
                public void mouseExited(java.awt.event.MouseEvent e) {
                    hover = false;
                    repaint();
                }
            });
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
                boolean on = isSelected();
                if (hover && !on) {
                    g.setColor(Theme.alpha(Theme.RAISED, 170));
                    g.fillRoundRect(0, 2, getWidth(), getHeight() - 4, 8, 8);
                }
                g.setFont(getFont());
                g.setColor(on || hover ? Theme.TEXT : Theme.MUTED);
                java.awt.FontMetrics metrics = g.getFontMetrics();
                java.awt.Insets in = getInsets();
                g.drawString(getText(), in.left, in.top + metrics.getAscent());
                if (on) {
                    g.setColor(Theme.ACCENT);
                    g.fillRect(in.left - 4, getHeight() - 2, getWidth() - in.left - in.right + 8, 2);
                }
            } finally {
                g.dispose();
            }
        }
    }

    /** Controls side by side from the left edge, without wrapping; for a row inside a card. */
    static JPanel line(Component... parts) {
        JPanel panel = new JPanel();
        panel.setOpaque(false);
        panel.setLayout(new BoxLayout(panel, BoxLayout.X_AXIS));
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) {
                panel.add(Box.createHorizontalStrut(8));
            }
            mute(parts[i]);
            panel.add(parts[i]);
        }
        panel.add(Box.createHorizontalGlue());
        return panel;
    }

    /** A control with its caption above it, for dense numeric fields. */
    static JPanel field(String caption, Component control) {
        JPanel panel = new JPanel();
        panel.setOpaque(false);
        panel.setLayout(new BoxLayout(panel, BoxLayout.Y_AXIS));
        JLabel label = caption(caption);
        label.setAlignmentX(Component.LEFT_ALIGNMENT);
        label.setBorder(BorderFactory.createEmptyBorder(0, 2, 3, 0));
        ((JComponent) control).setAlignmentX(Component.LEFT_ALIGNMENT);
        panel.add(label);
        panel.add(control);
        return panel;
    }

    /** Labels in a column on the left, controls in a column on the right; a row may hold several controls. */
    @SuppressWarnings("serial")
    static final class Form extends JPanel {
        private final int labelWidth;
        private int row;

        Form(int labelWidth) {
            super(new GridBagLayout());
            this.labelWidth = labelWidth;
            setOpaque(false);
        }

        /** Adds a labelled row; the controls sit side by side. */
        Form row(String label, Component... parts) {
            GridBagConstraints c = new GridBagConstraints();
            c.gridy = row++;
            c.insets = new Insets(4, 0, 4, 10);
            c.anchor = GridBagConstraints.WEST;
            JLabel name = caption(label);
            name.setPreferredSize(new Dimension(labelWidth, 24));
            c.gridx = 0;
            add(name, c);
            JPanel controls = new JPanel(new FlowLayout(FlowLayout.LEFT, 0, 0));
            controls.setOpaque(false);
            for (int i = 0; i < parts.length; i++) {
                mute(parts[i]);
                if (i > 0) {
                    controls.add(Box.createHorizontalStrut(8));
                }
                controls.add(parts[i]);
            }
            c.gridx = 1;
            c.weightx = 1;
            c.fill = GridBagConstraints.HORIZONTAL;
            c.insets = new Insets(4, 0, 4, 0);
            add(controls, c);
            return this;
        }
    }

    /** The parts one under another with a gap between them, left aligned; the usual body of a card. */
    static JPanel stack(int gap, Component... parts) {
        JPanel panel = new JPanel();
        panel.setOpaque(false);
        panel.setLayout(new BoxLayout(panel, BoxLayout.Y_AXIS));
        for (int i = 0; i < parts.length; i++) {
            if (i > 0) {
                panel.add(Box.createVerticalStrut(gap));
            }
            ((JComponent) parts[i]).setAlignmentX(Component.LEFT_ALIGNMENT);
            panel.add(parts[i]);
        }
        return panel;
    }

    /** A number with its caption above it, in the instrument face; the readout of a camera or a counter. */
    @SuppressWarnings("serial")
    static final class Stat extends JPanel {
        private final JLabel value = new JLabel("-");

        Stat(String caption) {
            setOpaque(false);
            setLayout(new BoxLayout(this, BoxLayout.Y_AXIS));
            JLabel label = caption(caption);
            label.setAlignmentX(Component.LEFT_ALIGNMENT);
            value.setFont(Theme.display(20));
            value.setForeground(Theme.TEXT);
            setBorder(BorderFactory.createEmptyBorder(0, 0, 0, 6));
            value.setAlignmentX(Component.LEFT_ALIGNMENT);
            add(label);
            add(value);
        }

        void setValue(String text) {
            value.setText(text);
        }
    }

    /** A label whose html text wraps to the width of its parent, so a long text grows the label downwards instead of being cut off. */
    @SuppressWarnings("serial")
    static class WrapLabel extends JLabel {
        WrapLabel() {
            super(" ");
        }

        @Override
        public Dimension getPreferredSize() {
            Dimension size = super.getPreferredSize();
            java.awt.Container parent = getParent();
            Object view = getClientProperty(javax.swing.plaf.basic.BasicHTML.propertyKey);
            if (!(view instanceof javax.swing.text.View) || parent == null || parent.getWidth() <= 0 || size.width <= parent.getWidth()) {
                return size;
            }
            Insets insets = parent.getInsets();
            int available = parent.getWidth() - insets.left - insets.right - getInsets().left - getInsets().right;
            ((javax.swing.text.View) view).setSize(Math.max(40, available), 0);
            return new Dimension(Math.max(40, available) + getInsets().left + getInsets().right,
                (int) Math.ceil(((javax.swing.text.View) view).getPreferredSpan(javax.swing.text.View.Y_AXIS)) + getInsets().top + getInsets().bottom);
        }
    }

    // -- Buttons.

    /** The main action of a panel: filled with the accent. */
    static JButton primary(JButton button) {
        button.putClientProperty("FlatLaf.style", "background: " + Theme.hex(Theme.ACCENT) + "; foreground: " + Theme.hex(Theme.ACCENT_INK)
            + "; hoverBackground: #69C8DC; pressedBackground: #3BA3BA; borderColor: " + Theme.hex(Theme.ACCENT) + "; font: bold");
        return button;
    }

    /** An action that removes or stops something: red text on the usual button. */
    static JButton danger(JButton button) {
        button.putClientProperty("FlatLaf.style", "foreground: " + Theme.hex(Theme.BAD) + "; borderColor: " + Theme.hex(Theme.mix(Theme.BORDER, Theme.BAD, 0.5f)));
        return button;
    }

    // -- Pills and dots.

    /** A small rounded label tinted by a tone, for a state such as "Live" or "NOT ALLOWED". */
    @SuppressWarnings("serial")
    static final class Pill extends JLabel {
        private Tone tone = Tone.IDLE;

        Pill(String text) {
            super(text);
            setFont(Theme.semibold(11.5f));
            setBorder(BorderFactory.createEmptyBorder(3, 10, 3, 10));
            setOpaque(false);
        }

        void setTone(Tone tone) {
            this.tone = tone;
            setForeground(tone.color());
            repaint();
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setColor(tone.soft());
                g.fill(new RoundRectangle2D.Double(0, 0, getWidth(), getHeight(), getHeight(), getHeight()));
            } finally {
                g.dispose();
            }
            super.paintComponent(graphics);
        }
    }

    /** A filled circle of a color, the state light of the header and of the status lines. */
    static final class Dot implements Icon {
        private final Color color;
        private final int size;

        Dot(Color color, int size) {
            this.color = color;
            this.size = size;
        }

        @Override
        public void paintIcon(Component c, Graphics graphics, int x, int y) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setColor(Theme.alpha(color, 60));
                g.fill(new Ellipse2D.Double(x - 2, y - 2, size + 4, size + 4));
                g.setColor(color);
                g.fill(new Ellipse2D.Double(x, y, size, size));
            } finally {
                g.dispose();
            }
        }

        @Override
        public int getIconWidth() {
            return size + 4;
        }

        @Override
        public int getIconHeight() {
            return size + 4;
        }
    }

    // -- Text areas.

    /** A read-only monospaced text area with the padding of a code view. */
    static JTextArea code() {
        JTextArea area = new JTextArea();
        area.setEditable(false);
        area.setFont(Theme.mono(12.5f));
        area.setBackground(Theme.FIELD);
        area.setForeground(Theme.TEXT);
        area.setCaretColor(Theme.TEXT);
        area.setBorder(BorderFactory.createEmptyBorder(8, 10, 8, 10));
        return area;
    }

    /** A line border in the hairline color, for a custom panel that wants the same edge as a table. */
    static Border edge() {
        return BorderFactory.createLineBorder(Theme.HAIRLINE);
    }

    static Font small() {
        return Theme.ui(Font.PLAIN, Theme.SMALL_SIZE);
    }
}
