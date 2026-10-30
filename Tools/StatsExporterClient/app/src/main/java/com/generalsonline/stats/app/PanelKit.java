package com.generalsonline.stats.app;

import com.generalsonline.stats.app.Theme.Tone;

import javax.swing.BorderFactory;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JSpinner;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SpinnerNumberModel;
import java.awt.Color;
import java.awt.Component;
import java.awt.Font;

/** Small Swing helpers the panels share: rows of controls, status lines with the tones of success, warning and failure, number fields. */
final class PanelKit {
    static final Color GOOD = Theme.GOOD;
    static final Color BAD = Theme.BAD;
    static final Color WARN = Theme.WARN;

    private PanelKit() {
    }

    /** A left aligned row of components that wraps when the window is narrow. */
    static JPanel row(Component... parts) {
        return Ui.bar(parts);
    }

    /** A label for the outcome of the last command; it keeps its height while empty. */
    static JLabel statusLabel() {
        return statusLabel(null);
    }

    /** A status line that says {@code idle} (muted) while there is nothing to report; without it the line takes no room until it has something to say. */
    static JLabel statusLabel(String idle) {
        StatusLabel label = new StatusLabel(idle);
        show(label, Tone.IDLE, false, Font.PLAIN);
        info(label, "");
        return label;
    }

    /** Applies a tone to a status line: a tinted strip with a bar on the left for success, warning and failure, plain muted text for progress. */
    private static void show(JLabel label, Tone tone, boolean tinted, int style) {
        label.setVisible(true);
        label.setOpaque(tinted);
        label.setBackground(tone.soft());
        label.setForeground(tinted ? tone.color() : Theme.MUTED);
        label.setFont(Theme.ui(style, Theme.TEXT_SIZE));
        Color bar = tinted ? tone.color() : Theme.alpha(Theme.MUTED, 0);
        label.setBorder(BorderFactory.createCompoundBorder(BorderFactory.createMatteBorder(0, 3, 0, 0, bar), BorderFactory.createEmptyBorder(6, 10, 6, 10)));
    }

    /** A status line: it wraps to the width it is given, and says {@code idle} while there is nothing to report. */
    @SuppressWarnings("serial")
    private static final class StatusLabel extends Ui.WrapLabel {
        private final String idle;

        StatusLabel(String idle) {
            this.idle = idle;
        }
    }

    static void info(JLabel label, String text) {
        show(label, Tone.IDLE, false, Font.PLAIN);
        label.setText(text.isEmpty() ? " " : text);
        if (text.isEmpty() && label instanceof StatusLabel) {
            String idle = ((StatusLabel) label).idle;
            label.setText(idle == null ? " " : idle);
            label.setVisible(idle != null);
        }
    }

    static void good(JLabel label, String text) {
        show(label, Tone.OK, true, Font.PLAIN);
        label.setText("<html>" + escape(text) + "</html>");
    }

    /** An amber status line, for a state that needs attention but is not a failure. */
    static void warn(JLabel label, String text) {
        show(label, Tone.WARN, true, Font.BOLD);
        label.setText("<html>" + escape(text) + "</html>");
    }

    /** A red status line. */
    static void bad(JLabel label, String text) {
        show(label, Tone.BAD, true, Font.PLAIN);
        label.setText("<html>" + escape(text) + "</html>");
    }

    /** A failure in red; the game's refusal because of its situation is amber and bold, so it is not mistaken for a mistake in the input. */
    static void fail(JLabel label, PipeCommands.Failure failure) {
        show(label, failure.notAllowed() ? Tone.WARN : Tone.BAD, true, failure.notAllowed() ? Font.BOLD : Font.PLAIN);
        label.setText("<html>" + escape(failure.line()) + "</html>");
    }

    static String escape(String text) {
        return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }

    /** A read-only monospaced text area. */
    static JTextArea monospaced() {
        return Ui.code();
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
