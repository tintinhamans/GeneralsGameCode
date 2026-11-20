package com.generalsonline.stats.app;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/**
 * Parsed command line: the command, its options and its positional arguments. Every mistake becomes a
 * {@link UsageException} with a message for the user; nothing here throws NumberFormatException or
 * ArrayIndexOutOfBoundsException.
 *
 * <p>Rules: options start with {@code --}, take their value as the next argument (or {@code --name=value}), and
 * must come before the positional arguments. {@code --} ends the options. For {@code chat}, everything after the
 * channel is the text, so a word that looks like an option there is never silently taken as one: a known option name
 * in the text is an error that says where the option belongs, and {@code --} before the channel sends it literally.
 */
final class CommandLine {
    /** A mistake in how the program was called. */
    static final class UsageException extends RuntimeException {
        private static final long serialVersionUID = 1L;

        UsageException(String message) {
            super(message);
        }
    }

    /**
     * What a command accepts.
     */
    private static final class Spec {
        private final String form;
        private final Set<String> valueOptions;
        private final Set<String> flags;
        private final int minPositional;
        private final int maxPositional;
        private final int textFrom;

        private Spec(String form, Set<String> valueOptions, Set<String> flags, int minPositional, int maxPositional, int textFrom) {
            this.form = form;
            this.valueOptions = valueOptions;
            this.flags = flags;
            this.minPositional = minPositional;
            this.maxPositional = maxPositional;
            this.textFrom = textFrom;
        }

        String form() {
            return form;
        }

        Set<String> valueOptions() {
            return valueOptions;
        }

        Set<String> flags() {
            return flags;
        }

        int minPositional() {
            return minPositional;
        }

        int maxPositional() {
            return maxPositional;
        }

        int textFrom() {
            return textFrom;
        }

        boolean isKnownOption(String name) {
            return valueOptions.contains(name) || flags.contains(name);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof Spec)) {
                return false;
            }
            Spec other = (Spec) o;
            return Objects.equals(form, other.form)
                    && Objects.equals(valueOptions, other.valueOptions)
                    && Objects.equals(flags, other.flags)
                    && minPositional == other.minPositional
                    && maxPositional == other.maxPositional
                    && textFrom == other.textFrom;
        }

        @Override
        public int hashCode() {
            return Objects.hash(form, valueOptions, flags, minPositional, maxPositional, textFrom);
        }

        @Override
        public String toString() {
            return "Spec[" + "form=" + form + ", valueOptions=" + valueOptions + ", flags=" + flags + ", minPositional=" + minPositional + ", maxPositional=" + maxPositional + ", textFrom=" + textFrom + "]";
        }
    }

    private static final Map<String, Spec> SPECS = new LinkedHashMap<>();

    static {
        SPECS.put("watch", new Spec("watch [--pid N | --pipe PATH] [--only KINDS]", Set.of("pid", "pipe", "only"), Set.of(), 0, 0, -1));
        SPECS.put("dump", new Spec("dump [--pid N | --pipe PATH] [--overwrite] [--no-camera] <file>", Set.of("pid", "pipe"), Set.of("overwrite", "no-camera"), 1, 1, -1));
        SPECS.put("replay", new Spec("replay <file>", Set.of(), Set.of(), 1, 1, -1));
        SPECS.put("chat", new Spec("chat [--audience A] [--pid N | --pipe PATH] [--] <channel> <text...>", Set.of("pid", "pipe", "audience"), Set.of(), 2, -1, 1));
        SPECS.put("gui", new Spec("gui [--pid N | --pipe PATH | --replay FILE [--speed MULTIPLIER]]", Set.of("pid", "pipe", "replay", "speed"), Set.of(), 0, 0, -1));
        SPECS.put("schema", new Spec("schema", Set.of(), Set.of(), 0, 0, -1));
        SPECS.put("example", new Spec("example [--player NAME] [--column NAME] [--kind KIND] [--channel CH] [--audience A] [--text TEXT] [--template NAME] [--side SIDE]"
            + " [--object ID] [--x X] [--y Y] [--zoom Z] [--speed RATIO] [--mode MODE] [<name>]",
            Set.of("player", "column", "kind", "channel", "audience", "text", "template", "side", "object", "x", "y", "zoom", "speed", "mode"), Set.of(), 0, 1, -1));
    }

    private final String command;
    private final Spec spec;
    private final Map<String, String> values = new LinkedHashMap<>();
    private final Set<String> flags = new LinkedHashSet<>();
    private final List<String> positional = new ArrayList<>();

    private CommandLine(String command, Spec spec) {
        this.command = command;
        this.spec = spec;
    }

    static Set<String> commands() {
        return SPECS.keySet();
    }

    static CommandLine parse(String[] args) {
        if (args.length == 0) {
            throw new UsageException("no command given");
        }
        Spec spec = SPECS.get(args[0]);
        if (spec == null) {
            throw new UsageException("unknown command '" + args[0] + "'");
        }
        CommandLine line = new CommandLine(args[0], spec);

        boolean optionsEnded = false;
        boolean inText = false;
        for (int i = 1; i < args.length; i++) {
            String arg = args[i];
            boolean optionLike = arg.startsWith("--") && arg.length() > 2;
            if (!optionsEnded && !inText && arg.equals("--")) {
                optionsEnded = true;
            } else if (!optionsEnded && !inText && optionLike) {
                i = line.parseOption(args, i);
            } else {
                if (inText && !optionsEnded && optionLike && spec.isKnownOption(optionName(arg))) {
                    throw new UsageException("'" + arg + "' comes after the text, where it would be sent as part of the message. "
                        + "Put options before the channel, or put -- before the channel to send it literally. Usage: " + spec.form());
                }
                line.positional.add(arg);
                inText = spec.textFrom() >= 0 && line.positional.size() >= spec.textFrom();
            }
        }

        if (line.positional.size() < spec.minPositional() || (spec.maxPositional() >= 0 && line.positional.size() > spec.maxPositional())) {
            throw new UsageException("usage: " + spec.form());
        }
        return line;
    }

    private static String optionName(String arg) {
        String name = arg.substring(2);
        int equals = name.indexOf('=');
        return equals >= 0 ? name.substring(0, equals) : name;
    }

    /** Parses the option at args[i]; returns the index of the last argument it used. */
    private int parseOption(String[] args, int i) {
        String arg = args[i];
        String name = optionName(arg);
        boolean hasInlineValue = arg.indexOf('=') >= 0;
        if (spec.flags().contains(name)) {
            if (hasInlineValue) {
                throw new UsageException("--" + name + " does not take a value");
            }
            flags.add(name);
            return i;
        }
        if (!spec.valueOptions().contains(name)) {
            throw new UsageException("unknown option '--" + name + "' for " + command + ". Usage: " + spec.form());
        }
        if (hasInlineValue) {
            values.put(name, arg.substring(arg.indexOf('=') + 1));
            return i;
        }
        if (i + 1 >= args.length) {
            throw new UsageException("--" + name + " needs a value");
        }
        values.put(name, args[i + 1]);
        return i + 1;
    }

    String command() {
        return command;
    }

    List<String> positional() {
        return positional;
    }

    boolean flag(String name) {
        return flags.contains(name);
    }

    /** The option's value, or null when it was not given. */
    String value(String name) {
        return values.get(name);
    }

    /** A non-negative whole number option; -1 when the option was not given. */
    int pid() {
        String text = values.get("pid");
        if (text == null) {
            return -1;
        }
        try {
            int pid = Integer.parseInt(text.trim());
            if (pid < 0) {
                throw new NumberFormatException();
            }
            return pid;
        } catch (NumberFormatException e) {
            throw new UsageException("--pid needs a process id (a whole number), got '" + text + "'");
        }
    }

    /** The replay speed multiplier: 1.0 is real time, 0 (the default) as fast as possible. */
    double speed() {
        String text = values.get("speed");
        if (text == null) {
            return 0;
        }
        try {
            double speed = Double.parseDouble(text.trim());
            if (speed < 0 || Double.isNaN(speed) || Double.isInfinite(speed)) {
                throw new NumberFormatException();
            }
            return speed;
        } catch (NumberFormatException e) {
            throw new UsageException("--speed needs a multiplier such as 1 (real time), 2.5 or 0 (as fast as possible), got '" + text + "'");
        }
    }

    /** Like {@link #speed()} but with a default for when the option was not given (a replay runs as fast as possible unless told otherwise). */
    double speed(double fallback) {
        return values.containsKey("speed") ? speed() : fallback;
    }

    static String usage() {
        StringBuilder text = new StringBuilder("usage: stats-exporter-client <command> [options]\n");
        for (Spec spec : SPECS.values()) {
            text.append("  ").append(spec.form()).append('\n');
        }
        return text.toString();
    }
}
