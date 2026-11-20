package com.generalsonline.stats.app;

import javax.swing.Icon;
import javax.swing.JLabel;
import javax.swing.JPanel;
import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.Component;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.geom.Ellipse2D;
import java.awt.geom.Line2D;
import java.awt.geom.Path2D;
import java.awt.geom.Rectangle2D;
import java.util.ArrayList;
import java.util.List;

/** The key of the minimap: what each mark on the map is. The marks of the interest overlay appear while it is on. */
@SuppressWarnings("serial")
final class MapLegend extends JPanel {
    private final List<JLabel> interestItems = new ArrayList<>();

    MapLegend() {
        super(new WrapLayout(18, 4, true));
        setOpaque(false);
        item("Start position", (g, x, y) -> {
            g.setColor(Theme.MUTED);
            g.setStroke(new BasicStroke(1.8f));
            g.draw(new Ellipse2D.Double(x + 2, y + 2, 10, 10));
        }, false);
        item("Supply source", (g, x, y) -> {
            g.setColor(new Color(0xF2C94C));
            g.fill(new Ellipse2D.Double(x + 2, y + 2, 10, 10));
        }, false);
        item("Tech structure", (g, x, y) -> {
            Path2D diamond = new Path2D.Double();
            diamond.moveTo(x + 7, y + 1);
            diamond.lineTo(x + 13, y + 7);
            diamond.lineTo(x + 7, y + 13);
            diamond.lineTo(x + 1, y + 7);
            diamond.closePath();
            g.setColor(Theme.MUTED);
            g.fill(diamond);
        }, false);
        item("Kill", (g, x, y) -> dot(g, x, y, new Color(0xFF5252)), false);
        item("Capture", (g, x, y) -> dot(g, x, y, new Color(0xFFEB3B)), false);
        item("Beacon", (g, x, y) -> dot(g, x, y, new Color(0x18FFFF)), false);
        item("Camera view", (g, x, y) -> {
            Path2D box = new Path2D.Double();
            box.moveTo(x + 2, y + 2);
            box.lineTo(x + 12, y + 2);
            box.lineTo(x + 10, y + 12);
            box.lineTo(x + 4, y + 12);
            box.closePath();
            g.setColor(Theme.alpha(Theme.ACCENT, 60));
            g.fill(box);
            g.setColor(Theme.ACCENT);
            g.setStroke(new BasicStroke(1.4f));
            g.draw(box);
        }, false);
        item("Superweapon launch", (g, x, y) -> {
            g.setColor(new Color(0xFF1744));
            g.setStroke(new BasicStroke(1.6f));
            g.draw(new Ellipse2D.Double(x + 1, y + 1, 12, 12));
            g.draw(new Ellipse2D.Double(x + 4, y + 4, 6, 6));
        }, true);
        item("Hero lost", (g, x, y) -> {
            g.setColor(Color.WHITE);
            g.setStroke(new BasicStroke(1.6f));
            g.draw(new Line2D.Double(x + 3, y + 3, x + 11, y + 11));
            g.draw(new Line2D.Double(x + 3, y + 11, x + 11, y + 3));
        }, true);
        item("Structure lost", (g, x, y) -> {
            g.setColor(new Color(0xFF9100));
            g.setStroke(new BasicStroke(1.6f));
            g.draw(new Rectangle2D.Double(x + 2, y + 2, 10, 10));
        }, true);
        setInterest(false);
    }

    private interface Mark {
        void paint(Graphics2D g, int x, int y);
    }

    private static void dot(Graphics2D g, int x, int y, Color color) {
        g.setColor(color);
        g.fill(new Ellipse2D.Double(x + 3, y + 3, 8, 8));
    }

    private void item(String text, Mark mark, boolean interest) {
        JLabel label = new JLabel(text);
        label.setIcon(new Icon() {
            @Override
            public void paintIcon(Component c, Graphics graphics, int x, int y) {
                Graphics2D g = (Graphics2D) graphics.create();
                try {
                    g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                    mark.paint(g, x, y);
                } finally {
                    g.dispose();
                }
            }

            @Override
            public int getIconWidth() {
                return 14;
            }

            @Override
            public int getIconHeight() {
                return 14;
            }
        });
        label.setIconTextGap(6);
        label.setFont(Ui.small());
        label.setForeground(Theme.MUTED);
        if (interest) {
            interestItems.add(label);
        }
        add(label);
    }

    /** Shows the marks of the interest overlay too. */
    void setInterest(boolean on) {
        for (JLabel label : interestItems) {
            label.setVisible(on);
        }
        revalidate();
    }

    @Override
    public java.awt.Dimension getPreferredSize() {
        return isPreferredSizeSet() ? super.getPreferredSize() : getLayout().preferredLayoutSize(this);
    }
}
