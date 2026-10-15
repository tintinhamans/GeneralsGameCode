package com.generalsonline.stats.app;

import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SpinnerNumberModel;
import java.awt.Color;
import java.awt.Component;
import java.awt.FlowLayout;
import java.awt.Font;

/** Small Swing helpers the new panels share: rows of controls, status labels with the colours of success and failure, number fields. */
final class PanelKit {
    static final Color GOOD = new Color(0x1B5E20);
    static final Color BAD = new Color(0xB71C1C);
    static final Color WARN = new Color(0xE65100);

    private PanelKit() {
    }

    /** A left aligned row of components. */
    static JPanel row(Component... parts) {
        JPanel panel = new JPanel(new FlowLayout(FlowLayout.LEFT, 6, 3));
        for (Component part : parts) {
            panel.add(part);
        }
        return panel;
    }

    /** A label for the outcome of the last command; it keeps its height while empty. */
    static JLabel statusLabel() {
        return new JLabel(" ");
    }

    static void info(JLabel label, String text) {
        label.setForeground(null);
        label.setFont(label.getFont().deriveFont(Font.PLAIN));
        label.setText(text.isEmpty() ? " " : text);
    }

    static void good(JLabel label, String text) {
        label.setForeground(GOOD);
        label.setFont(label.getFont().deriveFont(Font.PLAIN));
        label.setText(text);
    }

    /** A failure in red; the game's refusal because of its situation is bold, so it is not mistaken for a mistake in the input. */
    static void fail(JLabel label, PipeCommands.Failure failure) {
        label.setForeground(failure.notAllowed() ? WARN : BAD);
        label.setFont(label.getFont().deriveFont(failure.notAllowed() ? Font.BOLD : Font.PLAIN));
        label.setText("<html>" + escape(failure.line()) + "</html>");
    }

    static String escape(String text) {
        return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }

    /** A read-only monospaced text area. */
    static JTextArea monospaced() {
        JTextArea area = new JTextArea();
        area.setEditable(false);
        area.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
        return area;
    }

    static JSpinner spinner(int value, int min, int max, int step) {
        JSpinner spinner = new JSpinner(new SpinnerNumberModel(value, min, max, step));
        ((JSpinner.DefaultEditor) spinner.getEditor()).getTextField().setColumns(Math.max(4, Integer.toString(max).length()));
        return spinner;
    }

    static int intOf(JSpinner spinner) {
        return ((Number) spinner.getValue()).intValue();
    }

    /** A whole number from a text field, or the fallback when it is empty or not a number. */
    static long whole(JTextField field, long fallback) {
        try {
            return Long.parseLong(field.getText().trim());
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    /** A decimal number from a text field, or the fallback when it is empty, not a number or not finite. */
    static double decimal(JTextField field, double fallback) {
        try {
            double value = Double.parseDouble(field.getText().trim());
            return Double.isFinite(value) ? value : fallback;
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    /** A decimal number from a text field, or NaN when there is none; for the fields a command may leave out. */
    static double optionalDecimal(JTextField field) {
        return field.getText().isBlank() ? Double.NaN : decimal(field, Double.NaN);
    }

    /** A number as the short text a field shows: "1800", "1.2". */
    static String shortNumber(double value) {
        return value == Math.rint(value) && Math.abs(value) < 1e9 ? Long.toString((long) value) : String.format(java.util.Locale.ROOT, "%.3f", value).replaceAll("0+$", "").replaceAll("\\.$", "");
    }
}
