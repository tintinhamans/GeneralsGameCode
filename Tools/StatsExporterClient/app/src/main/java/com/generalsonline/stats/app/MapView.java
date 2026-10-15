package com.generalsonline.stats.app;

import com.generalsonline.stats.proto.MapDetail;
import com.generalsonline.stats.proto.Minimap;
import com.generalsonline.stats.proto.StartPositionInfo;
import com.generalsonline.stats.proto.WaypointInfo;

import javax.swing.JPanel;
import java.awt.BasicStroke;
import java.awt.Color;
import java.awt.FontMetrics;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.geom.Ellipse2D;
import java.awt.image.BufferedImage;

/**
 * Draws a {@link MapDetail}: the minimap image the game sent (north up, like the Minimap tab), the start positions as
 * numbered rings and the waypoints as dots. The image is placed by its own origin and cell size, so it lines up with the
 * world coordinates of the markers.
 */
@SuppressWarnings("serial")
final class MapView extends JPanel {
    private static final int MARGIN = 10;

    private MapDetail map;
    private BufferedImage image;

    MapView() {
        setBackground(new Color(0x1B1F24));
        setPreferredSize(new java.awt.Dimension(420, 420));
    }

    /** Shows a map; null clears the view. */
    void show(MapDetail detail) {
        this.map = detail;
        this.image = detail != null && detail.hasMinimap() && detail.getMinimap().getWidth() > 0 ? image(detail.getMinimap()) : null;
        repaint();
    }

    boolean hasImage() {
        return image != null;
    }

    /**
     * The radar image as a Java image. Pixel (px, py) covers the world cell whose lower corner is
     * {@code origin + (px, py) * cell}, so row 0 is the southern edge; the result is flipped to have north up.
     */
    static BufferedImage image(Minimap m) {
        int width = m.getWidth();
        int height = m.getHeight();
        BufferedImage out = new BufferedImage(Math.max(1, width), Math.max(1, height), BufferedImage.TYPE_INT_ARGB);
        byte[] rgba = m.getRgba().toByteArray();
        for (int py = 0; py < height; py++) {
            for (int px = 0; px < width; px++) {
                int at = (py * width + px) * 4;
                if (at + 3 >= rgba.length) {
                    return out;
                }
                int argb = ((rgba[at + 3] & 0xFF) << 24) | ((rgba[at] & 0xFF) << 16) | ((rgba[at + 1] & 0xFF) << 8) | (rgba[at + 2] & 0xFF);
                out.setRGB(px, height - 1 - py, argb);
            }
        }
        return out;
    }

    @Override
    protected void paintComponent(Graphics graphics) {
        super.paintComponent(graphics);
        Graphics2D g = (Graphics2D) graphics.create();
        try {
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            if (map == null || !map.getHasExtent()) {
                g.setColor(Color.GRAY);
                String hint = map == null ? "Press Load map to ask the game for the map." : "The map has no extent.";
                FontMetrics metrics = g.getFontMetrics();
                g.drawString(hint, Math.max(8, (getWidth() - metrics.stringWidth(hint)) / 2), getHeight() / 2);
                return;
            }
            double width = map.getMaxX() - map.getMinX();
            double height = map.getMaxY() - map.getMinY();
            double scale = Math.min((getWidth() - 2.0 * MARGIN) / width, (getHeight() - 2.0 * MARGIN) / height);
            double left = (getWidth() - width * scale) / 2;
            double top = (getHeight() - height * scale) / 2;
            if (image != null) {
                g.drawImage(image, (int) left, (int) top, (int) Math.round(width * scale), (int) Math.round(height * scale), null);
            } else {
                g.setColor(new Color(0x2A3038));
                g.fillRect((int) left, (int) top, (int) Math.round(width * scale), (int) Math.round(height * scale));
            }
            g.setColor(new Color(0x59626E));
            g.drawRect((int) left, (int) top, (int) Math.round(width * scale), (int) Math.round(height * scale));

            g.setColor(new Color(0xFFFFFF));
            for (WaypointInfo w : map.getWaypointsList()) {
                double x = left + (w.getX() - map.getMinX()) * scale;
                double y = top + (map.getMaxY() - w.getY()) * scale;
                g.fill(new Ellipse2D.Double(x - 2, y - 2, 4, 4));
            }
            g.setStroke(new BasicStroke(2f));
            g.setFont(g.getFont().deriveFont(java.awt.Font.BOLD, 11f));
            for (StartPositionInfo s : map.getStartPositionsList()) {
                double x = left + (s.getX() - map.getMinX()) * scale;
                double y = top + (map.getMaxY() - s.getY()) * scale;
                g.setColor(new Color(0, 0, 0, 150));
                g.fill(new Ellipse2D.Double(x - 9, y - 9, 18, 18));
                g.setColor(Color.WHITE);
                g.draw(new Ellipse2D.Double(x - 9, y - 9, 18, 18));
                g.drawString(Integer.toString(s.getStartPosition() + 1), (float) x - 3, (float) y + 4);
            }
        } finally {
            g.dispose();
        }
    }
}
