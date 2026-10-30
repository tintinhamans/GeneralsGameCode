package com.generalsonline.stats.app;

import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.UiScreen;

import javax.swing.BorderFactory;
import javax.swing.JButton;
import javax.swing.JComboBox;
import javax.swing.JLabel;
import javax.swing.JPanel;
import javax.swing.JScrollBar;
import javax.swing.JScrollPane;
import javax.swing.JTextField;
import javax.swing.JTextPane;
import javax.swing.SwingUtilities;
import javax.swing.text.BadLocationException;
import javax.swing.text.SimpleAttributeSet;
import javax.swing.text.StyleConstants;
import javax.swing.text.StyledDocument;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Toolkit;
import java.awt.datatransfer.StringSelection;
import java.awt.event.KeyAdapter;
import java.awt.event.KeyEvent;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.function.Supplier;

/**
 * The Chat section: a live conversation grouped by channel (one tab each, with the number of lines you have not seen), and a
 * composer that offers only the channels the game would deliver to right now. Lines of the game itself are set apart, sender names
 * wear the player's colour, the view follows new lines until you scroll up, and a search narrows it. The game gives no reply to
 * a chat line; the echo of your own line (it comes back marked local) is what confirms it.
 */
@SuppressWarnings("serial")
final class ChatTab extends JPanel {
    private static final String[] CHANNEL_TABS = {"All", "Match", "Game lobby", "Room", "LAN lobby", "LAN game"};
    private static final String[] CHANNEL_KEYS = {"", "MATCH", "GAME_LOBBY", "ROOM", "LAN_LOBBY", "LAN_GAME"};
    private static final int HISTORY_LIMIT = 50;
    private static final long CONFIRM_NANOS = 6_000_000_000L;

    private final GuiModel model;
    private final Supplier<StatsPipe> pipe;
    private final Ui.TabStrip tabs;
    private final JTextPane view = new JTextPane();
    private final JScrollPane scroll;
    private final JTextField search = new JTextField(16);
    private final JComboBox<Channel> channelBox = new JComboBox<>();
    private final JComboBox<ChatAudience> audienceBox = new JComboBox<>(new ChatAudience[] {ChatAudience.CHAT_AUDIENCE_EVERYONE, ChatAudience.CHAT_AUDIENCE_ALLIES, ChatAudience.CHAT_AUDIENCE_OBSERVERS});
    private final JTextField input = new JTextField();
    private final JLabel counter = Ui.caption("0 / " + StatsPipe.MAX_CHAT_BYTES);
    private final JLabel notice = Ui.caption(" ");
    private final JButton sendButton = new JButton("Send");
    private final JPanel canned = Ui.bar();
    private final List<String> history = new ArrayList<>();
    private final List<Pending> pending = new ArrayList<>();
    private final int[] seen = new int[CHANNEL_KEYS.length];
    private int selected;
    private int rendered;
    private int historyAt = -1;
    private String composerKey = "";

    /** What can be said now: a channel and the screen that makes the game deliver to it. */
    private static final class Channel {
        private final ChatChannel channel;
        private final String name;

        Channel(ChatChannel channel, String name) {
            this.channel = channel;
            this.name = name;
        }

        @Override
        public String toString() {
            return name;
        }
    }

    /** A line sent from this window that has not come back yet. */
    private static final class Pending {
        private final String text;
        private final String where;
        private final long at = System.nanoTime();
        private String failure;

        Pending(String text, String where) {
            this.text = text;
            this.where = where;
        }
    }

    ChatTab(GuiModel model, Supplier<StatsPipe> pipe) {
        super(new BorderLayout(0, 8));
        setBorder(BorderFactory.createEmptyBorder(Ui.PAGE_PADDING, Ui.PAGE_PADDING, Ui.PAGE_PADDING, Ui.PAGE_PADDING));
        this.model = model;
        this.pipe = pipe;
        tabs = new Ui.TabStrip(Arrays.asList(CHANNEL_TABS), index -> {
            selected = index;
            seen[index] = model.chat.getRowCount();
            rebuild();
            updateTabTitles();
        });
        view.setEditable(false);
        view.setBackground(Theme.FIELD);
        view.setFont(Theme.ui(java.awt.Font.PLAIN, 13));
        view.setBorder(BorderFactory.createEmptyBorder(8, 12, 8, 12));
        scroll = Ui.scroll(view);
        search.putClientProperty("JTextField.placeholderText", "Search the conversation");
        search.getDocument().addDocumentListener(new javax.swing.event.DocumentListener() {
            @Override
            public void insertUpdate(javax.swing.event.DocumentEvent e) {
                rebuild();
            }

            @Override
            public void removeUpdate(javax.swing.event.DocumentEvent e) {
                rebuild();
            }

            @Override
            public void changedUpdate(javax.swing.event.DocumentEvent e) {
                rebuild();
            }
        });
        JButton copy = new JButton("Copy");
        copy.setToolTipText("Copy the lines shown (or the selected text)");
        copy.addActionListener(e -> {
            String text = view.getSelectedText() != null ? view.getSelectedText() : view.getText();
            Toolkit.getDefaultToolkit().getSystemClipboard().setContents(new StringSelection(text), null);
        });

        JPanel top = new Ui.Toolbar(tabs, search, copy).underlined();
        add(top, BorderLayout.NORTH);
        add(scroll, BorderLayout.CENTER);
        add(composer(), BorderLayout.SOUTH);
        reloadCanned();
    }

    private JPanel composer() {
        input.putClientProperty("JTextField.placeholderText", "Say something, or a game command such as /help");
        input.addActionListener(e -> send());
        input.getDocument().addDocumentListener(new javax.swing.event.DocumentListener() {
            @Override
            public void insertUpdate(javax.swing.event.DocumentEvent e) {
                updateCounter();
            }

            @Override
            public void removeUpdate(javax.swing.event.DocumentEvent e) {
                updateCounter();
            }

            @Override
            public void changedUpdate(javax.swing.event.DocumentEvent e) {
                updateCounter();
            }
        });
        input.addKeyListener(new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                if (e.getKeyCode() == KeyEvent.VK_UP) {
                    recall(1);
                } else if (e.getKeyCode() == KeyEvent.VK_DOWN) {
                    recall(-1);
                }
            }
        });
        Ui.primary(sendButton);
        sendButton.addActionListener(e -> send());
        audienceBox.setToolTipText("Who sees a match line");

        JPanel row = new JPanel(new BorderLayout(8, 0));
        row.setOpaque(false);
        JPanel pickers = Ui.line(channelBox, audienceBox);
        JPanel right = Ui.line(counter, sendButton);
        row.add(pickers, BorderLayout.WEST);
        row.add(input, BorderLayout.CENTER);
        row.add(right, BorderLayout.EAST);
        JPanel box = new JPanel(new BorderLayout(0, 6));
        box.setOpaque(false);
        box.add(canned, BorderLayout.NORTH);
        box.add(row, BorderLayout.CENTER);
        box.add(notice, BorderLayout.SOUTH);
        return box;
    }

    /** Rebuilds the quick lines from the Settings. */
    void reloadCanned() {
        canned.removeAll();
        for (String line : cannedLines()) {
            JButton button = new JButton(line);
            button.putClientProperty("FlatLaf.style", "arc: 999; margin: 3,12,3,12");
            button.addActionListener(e -> {
                input.setText(line);
                send();
            });
            canned.add(button);
        }
        canned.revalidate();
    }

    static List<String> cannedLines() {
        List<String> lines = new ArrayList<>();
        for (String line : Prefs.get("cannedLines", "gl hf\ngg\ngg wp\nbrb").split("\n")) {
            if (!line.trim().isEmpty()) {
                lines.add(line.trim());
            }
        }
        return lines;
    }

    private void updateCounter() {
        int bytes = input.getText().getBytes(StandardCharsets.UTF_8).length;
        counter.setText(bytes + " / " + StatsPipe.MAX_CHAT_BYTES);
        counter.setForeground(bytes > StatsPipe.MAX_CHAT_BYTES ? Theme.BAD : bytes > 900 ? Theme.WARN : Theme.MUTED);
        sendButton.setEnabled(composerReady() && bytes > 0 && bytes <= StatsPipe.MAX_CHAT_BYTES);
    }

    private boolean composerReady() {
        return pipe.get() != null && channelBox.getItemCount() > 0;
    }

    private void recall(int direction) {
        if (history.isEmpty()) {
            return;
        }
        historyAt = Math.max(-1, Math.min(history.size() - 1, historyAt + direction));
        input.setText(historyAt < 0 ? "" : history.get(history.size() - 1 - historyAt));
    }

    private void send() {
        String text = input.getText();
        Channel channel = (Channel) channelBox.getSelectedItem();
        StatsPipe connection = pipe.get();
        if (text.trim().isEmpty() || channel == null || connection == null || text.getBytes(StandardCharsets.UTF_8).length > StatsPipe.MAX_CHAT_BYTES) {
            return;
        }
        ChatAudience audience = channel.channel == ChatChannel.CHAT_CHANNEL_MATCH ? (ChatAudience) audienceBox.getSelectedItem() : ChatAudience.CHAT_AUDIENCE_EVERYONE;
        Pending line = new Pending(text, channel.name);
        try {
            if (!connection.sendChat(channel.channel, text, audience)) {
                line.failure = "the connection is not ready";
            }
        } catch (IOException | RuntimeException e) {
            line.failure = e.getMessage() == null ? e.toString() : e.getMessage();
        }
        pending.add(line);
        history.add(text);
        if (history.size() > HISTORY_LIMIT) {
            history.remove(0);
        }
        historyAt = -1;
        input.setText("");
        rebuild();
    }

    /** Called from the window's timer: new lines, the unread counts, what the composer may offer. */
    void refresh() {
        refreshComposer();
        int rows = model.chat.getRowCount();
        if (rows < rendered) {
            rebuild();
        } else if (rows > rendered || !pending.isEmpty()) {
            boolean changedPending = confirmPending();
            if (rows > rendered) {
                append(rendered, rows, !changedPending);
            }
            if (changedPending || !pending.isEmpty()) {
                rebuild();
            }
        }
        if (selected == 0 || rows >= seen[selected]) {
            seen[selected] = rows;
        }
        updateTabTitles();
    }

    private boolean confirmPending() {
        boolean changed = false;
        for (int i = pending.size() - 1; i >= 0; i--) {
            Pending line = pending.get(i);
            for (int r = Math.max(0, rendered - 40); r < model.chat.getRowCount(); r++) {
                GuiModel.ChatRow row = model.chat.get(r);
                if (row.flags().contains("local") && row.text().equals(line.text)) {
                    pending.remove(i);
                    changed = true;
                    break;
                }
            }
        }
        long now = System.nanoTime();
        for (Pending line : pending) {
            if (line.failure == null && now - line.at > CONFIRM_NANOS) {
                line.failure = "the game did not echo it; it drops a line unless that chat is open";
                changed = true;
            }
        }
        return changed;
    }

    private void updateTabTitles() {
        int[] counts = new int[CHANNEL_KEYS.length];
        for (int r = 0; r < model.chat.getRowCount(); r++) {
            String channel = model.chat.get(r).channel();
            for (int k = 1; k < CHANNEL_KEYS.length; k++) {
                if (channel.equals(CHANNEL_KEYS[k]) && r >= seen[k]) {
                    counts[k]++;
                }
            }
            if (r >= seen[0]) {
                counts[0]++;
            }
        }
        for (int k = 0; k < CHANNEL_TABS.length; k++) {
            tabs.setText(k, k != selected && counts[k] > 0 ? CHANNEL_TABS[k] + " " + counts[k] : CHANNEL_TABS[k]);
        }
    }

    /** Offers the channels the game delivers to on the screen it is on, and says why when there is none. */
    private void refreshComposer() {
        UiScreen screen = model.state().ui().map(u -> u.getScreen()).orElse(UiScreen.UI_SCREEN_UNKNOWN);
        boolean connected = pipe.get() != null;
        String key = connected + ":" + screen;
        if (!key.equals(composerKey)) {
            composerKey = key;
            channelBox.removeAllItems();
            if (connected) {
                switch (screen) {
                    case UI_SCREEN_IN_GAME:
                    case UI_SCREEN_IN_GAME_MENU:
                        channelBox.addItem(new Channel(ChatChannel.CHAT_CHANNEL_MATCH, "Match"));
                        break;
                    case UI_SCREEN_ONLINE_GAME_SETUP:
                    case UI_SCREEN_SKIRMISH_SETUP:
                        channelBox.addItem(new Channel(ChatChannel.CHAT_CHANNEL_GAME_LOBBY, "Game lobby"));
                        break;
                    case UI_SCREEN_ONLINE_LOBBY:
                        channelBox.addItem(new Channel(ChatChannel.CHAT_CHANNEL_ROOM, "Room"));
                        break;
                    case UI_SCREEN_LAN_LOBBY:
                        channelBox.addItem(new Channel(ChatChannel.CHAT_CHANNEL_LAN_LOBBY, "LAN lobby"));
                        break;
                    case UI_SCREEN_LAN_GAME_SETUP:
                        channelBox.addItem(new Channel(ChatChannel.CHAT_CHANNEL_LAN_GAME, "LAN game"));
                        break;
                    default:
                        break;
                }
            }
            boolean ready = channelBox.getItemCount() > 0;
            channelBox.setEnabled(ready);
            input.setEnabled(ready);
            for (java.awt.Component part : canned.getComponents()) {
                part.setEnabled(ready);
            }
            notice.setText(!connected ? "Connect to a game to chat."
                : ready ? "Lines of the game, such as /help, can be sent like any other line. The game gives no reply to a chat line."
                : "The game is on '" + HeaderBar.prettyScreen(screen.name().replace("UI_SCREEN_", "")) + "'. Chat works in a match, a game lobby, the online room and a LAN lobby.");
        }
        audienceBox.setEnabled(channelBox.getItemCount() > 0 && ((Channel) channelBox.getSelectedItem()).channel == ChatChannel.CHAT_CHANNEL_MATCH);
        audienceBox.setVisible(channelBox.getItemCount() > 0 && audienceBox.isEnabled());
        updateCounter();
    }

    private boolean shows(GuiModel.ChatRow row) {
        if (selected != 0 && !row.channel().equals(CHANNEL_KEYS[selected])) {
            return false;
        }
        String needle = search.getText().trim().toLowerCase(Locale.ROOT);
        return needle.isEmpty() || (row.sender() + " " + row.text()).toLowerCase(Locale.ROOT).contains(needle);
    }

    private void rebuild() {
        try {
            view.getDocument().remove(0, view.getDocument().getLength());
        } catch (BadLocationException e) {
            // an empty document is the aim
        }
        rendered = 0;
        append(0, model.chat.getRowCount(), false);
        if (!pending.isEmpty()) {
            for (Pending line : pending) {
                write(pendingText(line), line.failure != null ? Theme.BAD : Theme.MUTED, false, true);
            }
        }
        scrollToEnd(true);
    }

    private String pendingText(Pending line) {
        return "you, " + line.where + ": " + line.text + (line.failure == null ? "   sending..." : "   NOT CONFIRMED: " + line.failure) + "\n";
    }

    private void append(int from, int to, boolean followEnd) {
        JScrollBar bar = scroll.getVerticalScrollBar();
        boolean atEnd = bar.getValue() + bar.getModel().getExtent() >= bar.getMaximum() - 24;
        for (int r = from; r < to; r++) {
            GuiModel.ChatRow row = model.chat.get(r);
            if (!shows(row)) {
                continue;
            }
            boolean system = row.flags().contains("system");
            Color senderColor = system ? Theme.DIM : row.flags().contains("local") ? Theme.ACCENT : senderColor(row);
            write(row.received() + "  ", Theme.DIM, false, false);
            if (selected == 0) {
                write(row.channel().replace('_', ' ').toLowerCase(Locale.ROOT) + "  ", Theme.DIM, false, false);
            }
            if (system) {
                write(row.text() + "\n", Theme.DIM, false, true);
            } else {
                write(row.sender(), senderColor, true, false);
                String who = !row.audience().isEmpty() && !row.audience().equals("EVERYONE") ? " (" + row.audience().toLowerCase(Locale.ROOT) + ")" : "";
                write(who + (row.flags().contains("action") ? " " : ": "), who.isEmpty() ? Theme.MUTED : Theme.WARN, false, false);
                write(row.text() + "\n", Theme.TEXT, false, false);
            }
        }
        rendered = to;
        if (atEnd) {
            scrollToEnd(false);
        }
    }

    private Color senderColor(GuiModel.ChatRow row) {
        int argb = row.playerIndex() >= 0 ? model.state().playerColor(row.playerIndex()) : 0;
        return argb == 0 ? Theme.TEXT : Theme.legible(new Color(argb | 0xFF000000));
    }

    private void write(String text, Color color, boolean bold, boolean italic) {
        SimpleAttributeSet style = new SimpleAttributeSet();
        StyleConstants.setForeground(style, color);
        StyleConstants.setBold(style, bold);
        StyleConstants.setItalic(style, italic);
        StyledDocument document = view.getStyledDocument();
        try {
            document.insertString(document.getLength(), text, style);
        } catch (BadLocationException e) {
            // the end of the document is always valid
        }
    }

    private void scrollToEnd(boolean force) {
        SwingUtilities.invokeLater(() -> {
            JScrollBar bar = scroll.getVerticalScrollBar();
            if (force || bar.getValue() + bar.getModel().getExtent() >= bar.getMaximum() - 60) {
                view.setCaretPosition(view.getDocument().getLength());
            }
        });
    }

    JTextField input() {
        return input;
    }
}
