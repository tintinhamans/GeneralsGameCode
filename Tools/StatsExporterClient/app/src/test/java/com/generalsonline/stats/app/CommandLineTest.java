package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.util.List;

/** Command line parsing (items 8 and 11) and the chat text rules. */
class CommandLineTest {
    private static CommandLine parse(String... args) {
        return CommandLine.parse(args);
    }

    private static String error(String... args) {
        return assertThrows(CommandLine.UsageException.class, () -> CommandLine.parse(args)).getMessage();
    }

    @Test
    void chatTakesOptionsBeforeTheChannelAndEverythingAfterItIsText() {
        CommandLine line = parse("chat", "--audience", "allies", "--pid", "42", "match", "gg", "wp");
        assertEquals("allies", line.value("audience"));
        assertEquals(42, line.pid());
        assertEquals(List.of("match", "gg", "wp"), line.positional());
    }

    @Test
    void wordsAfterTheTextThatLookLikeOptionsAreNotSilentlyTakenAsOptionsOrSent() {
        String message = error("chat", "match", "gg", "wp", "--audience", "allies");
        assertTrue(message.contains("--audience"), message);
        assertTrue(message.contains("before the channel"), message);
    }

    @Test
    void anUnknownDashDashWordInTheTextIsJustText() {
        CommandLine line = parse("chat", "match", "this", "is", "--fine");
        assertEquals(List.of("match", "this", "is", "--fine"), line.positional());
    }

    @Test
    void dashDashEndsTheOptionsAndAllowsAKnownOptionNameInTheText() {
        CommandLine line = parse("chat", "--audience", "observers", "--", "match", "use", "--pid", "to", "pick");
        assertEquals("observers", line.value("audience"));
        assertEquals(List.of("match", "use", "--pid", "to", "pick"), line.positional());
        assertEquals(-1, line.pid());

        assertEquals(List.of("--match"), parse("dump", "--", "--match").positional());
    }

    @Test
    void missingValuesAndBadNumbersAreUsageErrorsNotStackTraces() {
        assertTrue(error("watch", "--pid").contains("--pid needs a value"));
        assertTrue(assertThrows(CommandLine.UsageException.class, () -> parse("watch", "--pid", "abc").pid()).getMessage().contains("got 'abc'"));
        assertTrue(assertThrows(CommandLine.UsageException.class, () -> parse("watch", "--pid", "-3").pid()).getMessage().contains("process id"));
        assertTrue(assertThrows(CommandLine.UsageException.class, () -> parse("gui", "--speed", "fast").speed()).getMessage().contains("multiplier"));
        assertTrue(error("watch", "--bogus").contains("unknown option"));
        assertTrue(error("watch", "--overwrite").contains("unknown option"), "an option of another command");
        assertTrue(error("frobnicate").contains("unknown command"));
        assertTrue(error().contains("no command"));
        assertTrue(error("chat", "match").contains("usage"), "channel without text");
        assertTrue(error("dump").contains("usage"));
        assertTrue(error("dump", "a", "b").contains("usage"));
    }

    @Test
    void optionsMayCarryTheirValueInline() {
        assertEquals(7, parse("watch", "--pid=7").pid());
        assertEquals("events,chat", parse("watch", "--only=events,chat").value("only"));
        assertTrue(error("dump", "--overwrite=yes", "f").contains("does not take a value"));
    }

    @Test
    void dumpAndGuiOptions() {
        CommandLine dump = parse("dump", "--no-camera", "--overwrite", "match.dump", "--pipe", "\\\\.\\pipe\\x");
        assertTrue(dump.flag("no-camera"));
        assertTrue(dump.flag("overwrite"));
        assertEquals("\\\\.\\pipe\\x", dump.value("pipe"));
        assertEquals(List.of("match.dump"), dump.positional());
        assertFalse(parse("dump", "f").flag("overwrite"));

        CommandLine gui = parse("gui", "--replay", "a.dump", "--speed", "2.5");
        assertEquals(2.5, gui.speed());
        assertEquals(0.0, parse("gui").speed(), "default: as fast as possible");
        assertNull(parse("gui").value("replay"));
    }

    @Test
    void namesOfChannelsAndAudiencesAreCheckedWithAUsableMessage() {
        assertEquals(com.generalsonline.stats.proto.ChatChannel.CHAT_CHANNEL_GAME_LOBBY, ChatNames.channel("Game-Lobby"));
        assertEquals(com.generalsonline.stats.proto.ChatAudience.CHAT_AUDIENCE_ALLIES, ChatNames.audience("ALLIES"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> ChatNames.channel("lobby")).getMessage().contains("game-lobby"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> ChatNames.audience("all")).getMessage().contains("everyone"));
    }

    @Test
    void usageListsEveryCommand() {
        String usage = CommandLine.usage();
        for (String command : CommandLine.commands()) {
            assertTrue(usage.contains("  " + command), command);
        }
    }

    @Test
    void exampleTakesOneOptionalNameAndTheSelectionOptions() {
        assertEquals(List.of(), parse("example").positional());
        CommandLine line = parse("example", "--column", "Money", "--player", "Bob", "track-player");
        assertEquals(List.of("track-player"), line.positional());
        assertEquals("Money", line.value("column"));
        assertEquals("Bob", line.value("player"));
        assertEquals("kill", parse("example", "--kind=kill", "react-to-event").value("kind"));
        assertTrue(error("example", "a", "b").contains("usage"), "two names are one too many");
        assertTrue(error("example", "--bogus", "x").contains("unknown option"));
    }
}
