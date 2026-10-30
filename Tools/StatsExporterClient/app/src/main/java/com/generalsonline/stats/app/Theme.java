package com.generalsonline.stats.app;

import com.formdev.flatlaf.FlatDarkLaf;
import com.formdev.flatlaf.FlatLaf;

import javax.swing.BorderFactory;
import javax.swing.UIManager;
import javax.swing.plaf.FontUIResource;
import java.awt.Color;
import java.awt.Font;
import java.awt.GraphicsEnvironment;
import java.awt.Insets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * The design system of the window, in one place: the palette, the type, the sizes, and the defaults that make FlatLaf
 * (a dark Swing look and feel) look like this tool. Panels take their colors and fonts from here and never invent their own;
 * {@link Ui} builds the shared pieces (cards, forms, buttons) from the same tokens.
 *
 * <p>The palette is a cool slate with one accent, an instrument cyan. Amber, red and green are kept for state, so they
 * always mean something: green is a success, amber is the game refusing or a stale feed, red is a mistake or a failure.
 */
final class Theme {
    // Surfaces, deepest first. The window frame and the rail are the deepest, content sits on SURFACE, cards and table headers on RAISED.
    static final Color BG = new Color(0x0D1117);
    static final Color SURFACE = new Color(0x121821);
    static final Color RAISED = new Color(0x19212C);
    static final Color FIELD = new Color(0x0F151C);
    static final Color ZEBRA = new Color(0x131A23);
    static final Color BORDER = new Color(0x2A3544);
    static final Color HAIRLINE = new Color(0x1F2935);

    // Text.
    static final Color TEXT = new Color(0xD8E0EA);
    static final Color MUTED = new Color(0x8795A8);
    static final Color DIM = new Color(0x5E6B7C);

    // Accent and state.
    static final Color ACCENT = new Color(0x4FB8CF);
    static final Color ACCENT_SOFT = new Color(0x1B3B47);
    static final Color ACCENT_INK = new Color(0x062229);
    static final Color GOOD = new Color(0x4CC38A);
    static final Color WARN = new Color(0xF0A93B);
    static final Color BAD = new Color(0xF0626A);

    /** The state a status element shows; each has a color and a soft background for banners. */
    enum Tone {
        IDLE(MUTED), OK(GOOD), WARN(Theme.WARN), BAD(Theme.BAD);

        private final Color color;

        Tone(Color color) {
            this.color = color;
        }

        Color color() {
            return color;
        }

        /** The tone's color at low opacity over the surface, for a banner or a pill. */
        Color soft() {
            return mix(SURFACE, color, this == IDLE ? 0.10f : 0.16f);
        }
    }

    // Type: Segoe UI for text, Consolas for code and ids, Bahnschrift (a DIN style face that ships with Windows 10) for the big numbers.
    static final int TEXT_SIZE = 12;
    static final int SMALL_SIZE = 11;

    private static final Set<String> FAMILIES = new HashSet<>(Arrays.asList(GraphicsEnvironment.getLocalGraphicsEnvironment().getAvailableFontFamilyNames()));
    private static boolean installed;

    private Theme() {
    }

    static Font ui(int style, float size) {
        return new Font(FAMILIES.contains("Segoe UI") ? "Segoe UI" : Font.SANS_SERIF, style, 1).deriveFont(style, size);
    }

    static Font semibold(float size) {
        return FAMILIES.contains("Segoe UI Semibold") ? new Font("Segoe UI Semibold", Font.PLAIN, 1).deriveFont(size) : ui(Font.BOLD, size);
    }

    static Font mono(float size) {
        return new Font(FAMILIES.contains("Consolas") ? "Consolas" : Font.MONOSPACED, Font.PLAIN, 1).deriveFont(size);
    }

    /** The face of the big numbers in the header. */
    static Font display(float size) {
        return FAMILIES.contains("Bahnschrift") ? new Font("Bahnschrift", Font.PLAIN, 1).deriveFont(size) : semibold(size);
    }

    static Color mix(Color a, Color b, float t) {
        return new Color(Math.round(a.getRed() + (b.getRed() - a.getRed()) * t), Math.round(a.getGreen() + (b.getGreen() - a.getGreen()) * t),
            Math.round(a.getBlue() + (b.getBlue() - a.getBlue()) * t));
    }

    static Color alpha(Color c, int alpha) {
        return new Color(c.getRed(), c.getGreen(), c.getBlue(), alpha);
    }

    /** A player's color made readable on the dark surface: the hue stays, a very dark color is lifted. */
    static Color legible(Color c) {
        float[] hsb = Color.RGBtoHSB(c.getRed(), c.getGreen(), c.getBlue(), null);
        return Color.getHSBColor(hsb[0], Math.min(hsb[1], 0.85f), Math.max(hsb[2], 0.78f));
    }

    static String hex(Color c) {
        return String.format("#%02X%02X%02X", c.getRed(), c.getGreen(), c.getBlue());
    }

    /** Installs the look and feel and the defaults; call once, before any component is created. */
    static void install() {
        if (installed) {
            return;
        }
        installed = true;
        Map<String, String> globals = new HashMap<>();
        globals.put("@background", hex(SURFACE));
        globals.put("@foreground", hex(TEXT));
        globals.put("@componentBackground", hex(FIELD));
        globals.put("@buttonBackground", hex(RAISED));
        globals.put("@menuBackground", hex(RAISED));
        globals.put("@accentColor", hex(ACCENT));
        globals.put("@selectionBackground", hex(ACCENT_SOFT));
        globals.put("@selectionForeground", "#EAF6FA");
        globals.put("@selectionInactiveBackground", "#172C35");
        globals.put("@disabledForeground", hex(DIM));
        FlatLaf.setGlobalExtraDefaults(globals);
        try {
            FlatDarkLaf.setup();
        } catch (RuntimeException e) {
            // The default look and feel is usable; only the colors differ.
        }
        defaults();
    }

    private static void put(String key, Object value) {
        UIManager.put(key, value);
    }

    private static void defaults() {
        put("defaultFont", new FontUIResource(ui(Font.PLAIN, TEXT_SIZE)));

        // Shapes: soft corners everywhere, a thin focus ring.
        put("Component.arc", 8);
        put("Button.arc", 8);
        put("TextComponent.arc", 8);
        put("CheckBox.arc", 5);
        put("Component.focusWidth", 1);
        put("Component.innerFocusWidth", 0);
        put("Component.borderColor", BORDER);
        put("Button.borderColor", BORDER);
        put("Button.margin", new Insets(5, 14, 5, 14));
        put("Button.minimumWidth", 0);
        put("Button.hoverBackground", mix(RAISED, ACCENT, 0.10f));
        put("Button.pressedBackground", mix(RAISED, ACCENT, 0.20f));
        put("TextField.margin", new Insets(3, 8, 3, 8));
        put("ComboBox.padding", new Insets(3, 8, 3, 6));
        put("Spinner.padding", new Insets(3, 8, 3, 6));
        put("CheckBox.icon.focusedBackground", ACCENT_SOFT);

        // Tables: airy rows with a hairline, a quiet header, selection in the accent.
        put("Table.rowHeight", 28);
        put("Table.showHorizontalLines", true);
        put("Table.showVerticalLines", false);
        put("Table.gridColor", HAIRLINE);
        put("Table.alternateRowColor", ZEBRA);
        put("Table.selectionBackground", ACCENT_SOFT);
        put("Table.selectionForeground", new Color(0xEAF6FA));
        put("Table.selectionInactiveBackground", new Color(0x172C35));
        put("Table.cellMargins", new Insets(2, 10, 2, 10));
        put("TableHeader.height", 30);
        put("TableHeader.background", RAISED);
        put("TableHeader.foreground", MUTED);
        put("TableHeader.font", new FontUIResource(semibold(11.5f)));
        put("TableHeader.cellMargins", new Insets(2, 10, 2, 10));
        put("TableHeader.bottomSeparatorColor", BORDER);
        put("TableHeader.separatorColor", HAIRLINE);
        put("TableHeader.showTrailingVerticalLine", false);

        put("ScrollBar.width", 12);
        put("ScrollBar.thumbArc", 999);
        put("ScrollBar.thumbInsets", new Insets(2, 2, 2, 2));
        put("ScrollBar.thumb", new Color(0x2E3A4B));
        put("ScrollBar.hoverThumbColor", new Color(0x3C4A5F));
        put("ScrollBar.pressedThumbColor", new Color(0x4B5C75));
        put("ScrollBar.track", FIELD);
        put("ScrollPane.background", FIELD);

        put("TabbedPane.tabHeight", 36);
        put("TabbedPane.font", new FontUIResource(semibold(12)));
        put("TabbedPane.showTabSeparators", false);
        put("TabbedPane.tabInsets", new Insets(4, 14, 4, 14));
        put("TabbedPane.foreground", MUTED);
        put("TabbedPane.selectedForeground", TEXT);
        put("TabbedPane.hoverColor", RAISED);
        put("TabbedPane.contentAreaColor", HAIRLINE);
        put("TabbedPane.underlineColor", ACCENT);
        put("TabbedPane.disabledUnderlineColor", DIM);
        put("TabbedPane.tabSelectionHeight", 2);

        put("SplitPane.dividerSize", 7);
        put("SplitPaneDivider.style", "plain");
        put("SplitPaneDivider.gripColor", BORDER);
        put("SplitPane.background", SURFACE);

        put("ToolTip.background", RAISED);
        put("ToolTip.foreground", TEXT);
        put("ToolTip.border", BorderFactory.createLineBorder(BORDER));
        put("PopupMenu.background", RAISED);
        put("PopupMenu.borderColor", BORDER);
        put("MenuItem.selectionBackground", ACCENT_SOFT);
        put("MenuItem.margin", new Insets(5, 10, 5, 10));
        put("Separator.foreground", HAIRLINE);
        put("ToggleButton.selectedBackground", ACCENT_SOFT);
        put("ToggleButton.selectedForeground", new Color(0xEAF6FA));
        put("ToggleButton.toolbar.selectedBackground", ACCENT_SOFT);

        // The title bar is the window's own: the same ground as the header, so the two read as one band.
        put("TitlePane.background", BG);
        put("TitlePane.inactiveBackground", BG);
        put("TitlePane.foreground", MUTED);
        put("TitlePane.inactiveForeground", DIM);
        put("TitlePane.unifiedBackground", true);
        put("TitlePane.centerTitle", false);
        put("TitlePane.font", new FontUIResource(semibold(12)));
        put("TitledBorder.titleColor", MUTED);
    }
}
