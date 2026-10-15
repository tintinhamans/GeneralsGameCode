package com.generalsonline.stats.app;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.StatsPipe;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.StatEventKind;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import javax.tools.Diagnostic;
import javax.tools.DiagnosticCollector;
import javax.tools.JavaCompiler;
import javax.tools.JavaFileObject;
import javax.tools.SimpleJavaFileObject;
import javax.tools.ToolProvider;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The example-code generator: the templates are filled from the selection, every generated program compiles against the
 * library without warnings, and the programs in the examples project are the generator's output.
 */
class ExamplesTest {
    private static final Examples.Selection DEFAULTS = Examples.Selection.defaults();

    /** A Java source held in memory. */
    private static final class Source extends SimpleJavaFileObject {
        private final String text;

        Source(String className, String text) {
            super(URI.create("string:///" + className + ".java"), Kind.SOURCE);
            this.text = text;
        }

        @Override
        public CharSequence getCharContent(boolean ignoreEncodingErrors) {
            return text;
        }
    }

    /** The class path the generated programs need: the library classes and protobuf-java, found through the classes themselves. */
    private static String libraryClassPath() throws Exception {
        List<String> entries = new ArrayList<>();
        for (Class<?> type : List.of(StatsPipe.class, Message.class)) {
            entries.add(Path.of(type.getProtectionDomain().getCodeSource().getLocation().toURI()).toString());
        }
        return String.join(java.io.File.pathSeparator, entries);
    }

    /**
     * Compiles every program in one run with -Xlint:all. Each gets a numbered class name so that many selections of the
     * same scenario fit into one compilation. Returns every diagnostic of any kind: a warning is as bad as an error.
     */
    private static List<String> compile(Path output, List<String> programs, List<Examples.Scenario> scenarios) throws Exception {
        JavaCompiler compiler = ToolProvider.getSystemJavaCompiler();
        assertNotNull(compiler, "the tests need a JDK, not a JRE");
        List<JavaFileObject> sources = new ArrayList<>();
        for (int i = 0; i < programs.size(); i++) {
            String className = scenarios.get(i).className();
            String renamed = className + "_" + i;
            sources.add(new Source(renamed, programs.get(i).replace(className, renamed)));
        }
        DiagnosticCollector<JavaFileObject> diagnostics = new DiagnosticCollector<>();
        List<String> options = List.of("--release", "17", "-Xlint:all", "-proc:none", "-encoding", "UTF-8", "-classpath", libraryClassPath(), "-d", output.toString());
        boolean ok = compiler.getTask(null, null, diagnostics, options, null, sources).call();
        List<String> problems = new ArrayList<>();
        for (Diagnostic<? extends JavaFileObject> d : diagnostics.getDiagnostics()) {
            problems.add(d.getKind() + " " + (d.getSource() == null ? "" : d.getSource().getName()) + ":" + d.getLineNumber() + " " + d.getMessage(null));
        }
        if (!ok && problems.isEmpty()) {
            problems.add("compilation failed without a diagnostic");
        }
        return problems;
    }

    private static void assertCompiles(Path output, List<String> programs, List<Examples.Scenario> scenarios) throws Exception {
        List<String> problems = compile(output, programs, scenarios);
        assertTrue(problems.isEmpty(), String.join("\n", problems));
    }

    // -- Compile checks.

    @Test
    void everyScenarioCompilesWithTheDefaultSelection(@TempDir Path output) throws Exception {
        List<String> programs = new ArrayList<>();
        List<Examples.Scenario> scenarios = new ArrayList<>();
        for (Examples.Scenario scenario : Examples.Scenario.values()) {
            programs.add(Examples.generate(scenario, DEFAULTS));
            scenarios.add(scenario);
        }
        assertEquals(15, programs.size(), "the eight scenarios of the first version and the seven of the control features");
        assertCompiles(output, programs, scenarios);
    }

    @Test
    void trackPlayerCompilesForEveryPlayersColumnAndAnAwkwardPlayerName(@TempDir Path output) throws Exception {
        List<String> programs = new ArrayList<>();
        List<Examples.Scenario> scenarios = new ArrayList<>();
        for (Examples.StatColumn column : Examples.statColumns()) {
            programs.add(Examples.generate(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withColumn(column.name()).withPlayer("Al\"ice\\ \u4e2d")));
            scenarios.add(Examples.Scenario.TRACK_PLAYER);
        }
        assertCompiles(output, programs, scenarios);
    }

    @Test
    void reactToEventCompilesForEveryEventKind(@TempDir Path output) throws Exception {
        List<String> programs = new ArrayList<>();
        List<Examples.Scenario> scenarios = new ArrayList<>();
        for (StatEventKind kind : Examples.eventKinds()) {
            programs.add(Examples.generate(Examples.Scenario.REACT_TO_EVENT, DEFAULTS.withEventKind(kind)));
            scenarios.add(Examples.Scenario.REACT_TO_EVENT);
        }
        assertCompiles(output, programs, scenarios);
    }

    @Test
    void sendChatCompilesForEveryChannelAudienceAndAnAwkwardText(@TempDir Path output) throws Exception {
        List<String> programs = new ArrayList<>();
        List<Examples.Scenario> scenarios = new ArrayList<>();
        for (ChatChannel channel : Examples.chatChannels()) {
            for (ChatAudience audience : Examples.chatAudiences()) {
                programs.add(Examples.generate(Examples.Scenario.SEND_CHAT, DEFAULTS.withChannel(channel).withAudience(audience).withChatText("say \"gg\" \\o/ \u00e9\u00e8 \u4e2d\nnext")));
                scenarios.add(Examples.Scenario.SEND_CHAT);
            }
        }
        assertCompiles(output, programs, scenarios);
    }

    @Test
    void theCompileCheckReallyFailsOnAProgramThatDoesNotCompile(@TempDir Path output) throws Exception {
        String broken = Examples.generate(Examples.Scenario.LOBBY, DEFAULTS).replace("lobby.getInLobby()", "lobby.getInLobbyy()");
        List<String> problems = compile(output, List.of(broken), List.of(Examples.Scenario.LOBBY));
        assertFalse(problems.isEmpty());
        assertTrue(problems.get(0).contains("getInLobbyy"), problems.toString());

        // An unused warning-class problem counts too: a raw type triggers -Xlint:all.
        String warns = Examples.generate(Examples.Scenario.LOBBY, DEFAULTS).replace("public class ShowLobby {", "public class ShowLobby {\n    static java.util.List raw = new java.util.ArrayList();");
        assertFalse(compile(output, List.of(warns), List.of(Examples.Scenario.LOBBY)).isEmpty(), "warnings fail the check as well");
    }

    // -- What the user selected shows in the program.

    @Test
    void theSelectedColumnAndPlayerFillTheTrackPlayerProgram() {
        String money = Examples.generate(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withPlayer("Bob").withColumn("Money"));
        assertTrue(money.contains("private static final String PLAYER = \"Bob\";"), money);
        assertTrue(money.contains("player.stats().getMoney()"), money);
        assertTrue(money.contains("\"  Money = \""), money);

        String income = Examples.generate(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withColumn("Income/min"));
        assertTrue(income.contains("player.stats().getMoneyPerMinute()"), income);
        assertFalse(income.contains("getMoney()"), "only the selected stat");

        String army = Examples.generate(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withColumn("Army value"));
        assertTrue(army.contains("player.stats().getArmyValue()"), army);

        String name = Examples.generate(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withPlayer("A\"B\\C"));
        assertTrue(name.contains("PLAYER = \"A\\\"B\\\\C\";"), "quotes and backslashes are escaped: " + name);
    }

    @Test
    void theSelectedEventKindFillsTheEventProgramWithItsDetailFields() {
        String capture = Examples.generate(Examples.Scenario.REACT_TO_EVENT, DEFAULTS.withEventKind(StatEventKind.STATEVENT_CAPTURE));
        assertTrue(capture.contains("StatEventKind.STATEVENT_CAPTURE"), capture);
        assertTrue(capture.contains("old owner"), capture);
        assertTrue(capture.contains("state.playerName(e.getIntValue1())"), capture);
        assertTrue(capture.contains("cash bonus"), capture);

        String upgrade = Examples.generate(Examples.Scenario.REACT_TO_EVENT, DEFAULTS.withEventKind(StatEventKind.STATEVENT_UPGRADE_COMPLETE));
        assertTrue(upgrade.contains("StatEventKind.STATEVENT_UPGRADE_COMPLETE"), upgrade);
        assertTrue(upgrade.contains("Format.shortUpgradeName(e.getPrimaryName())"), upgrade);

        String kill = Examples.generate(Examples.Scenario.REACT_TO_EVENT, DEFAULTS);
        assertTrue(kill.contains("victim cost"), kill);
        assertTrue(kill.contains("if (e.getHasLocation())"), "a location is only read when the event has one: " + kill);
    }

    @Test
    void theChatProgramCarriesTheChannelAudienceTextAndTheCaveats() {
        String allies = Examples.generate(Examples.Scenario.SEND_CHAT, DEFAULTS.withChannel(ChatChannel.CHAT_CHANNEL_MATCH).withAudience(ChatAudience.CHAT_AUDIENCE_ALLIES).withChatText("push now"));
        assertTrue(allies.contains("ChatChannel.CHAT_CHANNEL_MATCH, text, ChatAudience.CHAT_AUDIENCE_ALLIES"), allies);
        assertTrue(allies.contains("\"push now\""), allies);
        assertTrue(allies.contains("readers per pipe"), "the reader slot caveat: " + allies);
        assertTrue(allies.contains("1024 bytes"), "the length limit: " + allies);
        assertTrue(allies.contains("There is no reply") || allies.contains("no reply"), "the no-feedback caveat: " + allies);

        String room = Examples.generate(Examples.Scenario.SEND_CHAT, DEFAULTS.withChannel(ChatChannel.CHAT_CHANNEL_ROOM).withAudience(ChatAudience.CHAT_AUDIENCE_ALLIES));
        assertTrue(room.contains("ChatChannel.CHAT_CHANNEL_ROOM, text);"), "the audience only matters for the match channel: " + room);
        assertFalse(room.contains("CHAT_AUDIENCE_ALLIES, "), room);
    }

    @Test
    void theOtherProgramsShowTheirApi() {
        assertTrue(Examples.generate(Examples.Scenario.PRINT_FRAMES, DEFAULTS).contains("public void onCamera(Camera camera)"));
        String camera = Examples.generate(Examples.Scenario.CAMERA_BOX, DEFAULTS);
        assertTrue(camera.contains("getViewTlX()") && camera.contains("getViewBlY()") && camera.contains("worldExtent()"), camera);
        String lobby = Examples.generate(Examples.Scenario.LOBBY, DEFAULTS);
        assertTrue(lobby.contains("\"Random -> \" + slot.getPredictedSide()"), lobby);
        String record = Examples.generate(Examples.Scenario.RECORD_REPLAY, DEFAULTS);
        assertTrue(record.contains("DumpReader.replay(file, new Handler(null), 2.0)"), record);
        String ui = Examples.generate(Examples.Scenario.UI_THREAD, DEFAULTS);
        assertTrue(ui.contains(".executor(uiThread)") && ui.contains("Platform::runLater"), ui);
    }

    @Test
    void everyProgramIsCompleteAndNamesItsClassAndHowToRunIt() {
        Set<String> classNames = new HashSet<>();
        for (Examples.Scenario scenario : Examples.Scenario.values()) {
            String program = Examples.generate(scenario, DEFAULTS);
            assertTrue(classNames.add(scenario.className()), "distinct class names");
            assertTrue(program.contains("public class " + scenario.className() + " {"), scenario.id());
            assertTrue(program.contains("public static void main(String[] args)"), scenario.id());
            assertTrue(program.contains(scenario.className() + ".java"), "how to run it: " + scenario.id());
            assertFalse(program.contains("${"), "no placeholder left in " + scenario.id());
            assertFalse(program.contains("generalsonline.stats.internal"), "a program only uses the public API: " + scenario.id());
            assertFalse(program.startsWith("package "), "default package, so the single file runs: " + scenario.id());
            assertEquals(scenario.className() + ".java", scenario.fileName());
        }
    }

    // -- The scenarios of the query, control, director and fake game features.

    @Test
    void theControlScenariosCompileForAwkwardSelections(@TempDir Path output) throws Exception {
        List<String> programs = new ArrayList<>();
        List<Examples.Scenario> scenarios = new ArrayList<>();
        String awkward = "A\"b\\c \u4e2d";
        programs.add(Examples.generate(Examples.Scenario.QUERY_CATALOG, DEFAULTS.withTemplate(awkward, awkward)));
        scenarios.add(Examples.Scenario.QUERY_CATALOG);
        programs.add(Examples.generate(Examples.Scenario.INSPECT_OBJECT, DEFAULTS.withObjectId(Integer.MAX_VALUE)));
        scenarios.add(Examples.Scenario.INSPECT_OBJECT);
        programs.add(Examples.generate(Examples.Scenario.CAMERA_CONTROL, DEFAULTS.withCamera(-1.5e7, 1e-3, 19.5).withObjectId(7)));
        scenarios.add(Examples.Scenario.CAMERA_CONTROL);
        programs.add(Examples.generate(Examples.Scenario.REPLAY_CONTROL, DEFAULTS.withReplaySpeed(16)));
        scenarios.add(Examples.Scenario.REPLAY_CONTROL);
        programs.add(Examples.generate(Examples.Scenario.VIEW_STATS, DEFAULTS));
        scenarios.add(Examples.Scenario.VIEW_STATS);
        for (DirectorMode mode : DirectorMode.values()) {
            programs.add(Examples.generate(Examples.Scenario.DIRECTOR, DEFAULTS.withDirectorMode(mode)));
            scenarios.add(Examples.Scenario.DIRECTOR);
        }
        programs.add(Examples.generate(Examples.Scenario.FAKE_SERVER, DEFAULTS.withDumpFile("C:\\dumps\\a \"b\".dump")));
        scenarios.add(Examples.Scenario.FAKE_SERVER);
        assertCompiles(output, programs, scenarios);
    }

    @Test
    void theControlScenariosShowTheirSelectionAndTheirApi() {
        String catalog = Examples.generate(Examples.Scenario.QUERY_CATALOG, DEFAULTS.withTemplate("ChinaTankOverlord", "China"));
        assertTrue(catalog.contains("SIDE = \"China\"") && catalog.contains("NAME = \"ChinaTankOverlord\""), catalog);
        assertTrue(catalog.contains("PageRequest.newBuilder().setCursor(cursor)") && catalog.contains("catalogAll"), catalog);

        String inspect = Examples.generate(Examples.Scenario.INSPECT_OBJECT, DEFAULTS.withObjectId(77));
        assertTrue(inspect.contains("DEFAULT_ID = 77;") && inspect.contains("pipe.object(id)") && inspect.contains("RequestFailedException"), inspect);

        String camera = Examples.generate(Examples.Scenario.CAMERA_CONTROL, DEFAULTS.withCamera(10, 20.5, 2).withObjectId(9));
        assertTrue(camera.contains("CameraMove.to(10.0, 20.5).zoom(2.0)") && camera.contains("followUnit(9)") && camera.contains("NotAllowedException"), camera);
        assertTrue(camera.contains("easeInOut()") && camera.contains("resetCamera()") && camera.contains("unfollowUnit()"), camera);

        String replay = Examples.generate(Examples.Scenario.REPLAY_CONTROL, DEFAULTS.withReplaySpeed(0.25));
        assertTrue(replay.contains("setReplaySpeed(0.25)") && replay.contains("getSpeedCapped()") && replay.contains("setReplayFastForward(true)"), replay);

        String view = Examples.generate(Examples.Scenario.VIEW_STATS, DEFAULTS);
        assertTrue(view.contains("viewStatsChanged(ViewStats view)") && view.contains("getValueInView()") && view.contains("100.0 * value / total"), view);

        String director = Examples.generate(Examples.Scenario.DIRECTOR, DEFAULTS.withDirectorMode(DirectorMode.HIGHLIGHTS));
        assertTrue(director.contains("DirectorMode.HIGHLIGHTS") && director.contains("Director.start(pipe, state, config)") && director.contains("director.lastMove()"), director);
        assertTrue(director.contains("a superweapon launch, a hero death"), director);

        String fake = Examples.generate(Examples.Scenario.FAKE_SERVER, DEFAULTS.withDumpFile("x.dump"));
        assertTrue(fake.contains("\"x.dump\"") && fake.contains("game.connect(StatsPipe.builder()") && fake.contains("namedPipe(false)") && fake.contains("holdForReader(true)"), fake);
        assertFalse(fake.contains("\\\\.\\pipe"), "no backslash path in a template: " + fake);
    }

    @Test
    void aNumberThatIsNotFiniteIsRefusedNotWrittenIntoTheProgram() {
        assertThrows(IllegalArgumentException.class, () -> Examples.generate(Examples.Scenario.CAMERA_CONTROL, DEFAULTS.withCamera(Double.NaN, 0, 1)));
        assertThrows(IllegalArgumentException.class, () -> Examples.generate(Examples.Scenario.REPLAY_CONTROL, DEFAULTS.withReplaySpeed(Double.POSITIVE_INFINITY)));
    }

    @Test
    void theExampleCommandFillsTheNewScenarios() {
        assertEquals(Examples.generate(Examples.Scenario.QUERY_CATALOG, DEFAULTS.withTemplate("GLATankScorpion", "GLA")), run("--template", "GLATankScorpion", "--side", "GLA", "query-catalog"));
        assertEquals(Examples.generate(Examples.Scenario.INSPECT_OBJECT, DEFAULTS.withObjectId(55)), run("--object", "55", "inspect-object"));
        assertEquals(Examples.generate(Examples.Scenario.CAMERA_CONTROL, DEFAULTS.withCamera(5, 6, 0.5).withObjectId(8)), run("--x", "5", "--y", "6", "--zoom", "0.5", "--object", "8", "camera-control"));
        assertEquals(Examples.generate(Examples.Scenario.REPLAY_CONTROL, DEFAULTS.withReplaySpeed(4)), run("--speed", "4", "replay-control"));
        assertEquals(Examples.generate(Examples.Scenario.DIRECTOR, DEFAULTS.withDirectorMode(DirectorMode.OVERVIEW)), run("--mode", "Overview", "director"));
        assertEquals(Examples.generate(Examples.Scenario.FAKE_SERVER, DEFAULTS.withDumpFile("a.dump")), run("--dump", "a.dump", "fake-server"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> run("--mode", "nope", "director")).getMessage().contains("hotspot"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> run("--x", "abc", "camera-control")).getMessage().contains("--x needs a number"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> run("--object", "1.5", "inspect-object")).getMessage().contains("whole number"));
    }

    @Test
    void theCodeTabFillsAndReadsTheNewControls() {
        CodeTab tab = new CodeTab();
        tab.show(new CodeTab.Request(Examples.Scenario.CAMERA_CONTROL, DEFAULTS.withCamera(11, 22, 3).withObjectId(33)));
        assertTrue(tab.code().contains("CameraMove.to(11.0, 22.0).zoom(3.0)") && tab.code().contains("followUnit(33)"), tab.code());
        assertEquals(11.0, tab.selection().cameraX());
        assertEquals(33, tab.selection().objectId());
        tab.show(new CodeTab.Request(Examples.Scenario.DIRECTOR, DEFAULTS.withDirectorMode(DirectorMode.HOTSPOT)));
        assertTrue(tab.code().contains("DirectorMode.HOTSPOT"), tab.code());
        tab.show(new CodeTab.Request(Examples.Scenario.QUERY_CATALOG, DEFAULTS.withTemplate("Foo", "Bar")));
        assertTrue(tab.code().contains("\"Foo\"") && tab.code().contains("\"Bar\""), tab.code());
        tab.show(new CodeTab.Request(Examples.Scenario.FAKE_SERVER, DEFAULTS.withDumpFile("zz.dump")));
        assertTrue(tab.code().contains("\"zz.dump\""), tab.code());
    }

    // -- The catalogues stay complete.

    @Test
    void everyPlayersColumnHasAnAccessorAndNoAccessorIsForAMissingColumn() {
        GuiModel model = new GuiModel();
        Set<String> columns = new HashSet<>();
        for (int i = 0; i < model.players.getColumnCount(); i++) {
            columns.add(model.players.getColumnName(i));
        }
        Set<String> accessors = new HashSet<>();
        for (Examples.StatColumn column : Examples.statColumns()) {
            assertTrue(accessors.add(column.name()), "duplicate accessor " + column.name());
        }
        assertEquals(columns, accessors, "a new Players column needs an accessor for the code generator");
    }

    @Test
    void everyEventKindOfTheProtocolHasFields() {
        Set<StatEventKind> known = new HashSet<>(Examples.eventKinds());
        for (StatEventKind kind : StatEventKind.values()) {
            if (kind != StatEventKind.UNRECOGNIZED) {
                assertTrue(known.contains(kind), "no detail fields for " + kind);
            }
        }
    }

    @Test
    void placeholdersWithoutAValueAndUnknownSelectionsAreErrors() {
        assertThrows(IllegalStateException.class, () -> Examples.fill("a ${MISSING} b", java.util.Map.of()));
        assertEquals("a 1 b 1", Examples.fill("a ${X} b ${X}", java.util.Map.of("X", "1")));
        assertThrows(IllegalArgumentException.class, () -> Examples.generate(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withColumn("Nonsense")));
        assertThrows(IllegalArgumentException.class, () -> Examples.generate(Examples.Scenario.REACT_TO_EVENT, DEFAULTS.withEventKind(StatEventKind.UNRECOGNIZED)));
        assertEquals(Examples.Scenario.CAMERA_BOX, Examples.Scenario.byId("Camera-Box"));
        assertEquals(null, Examples.Scenario.byId("nothing"));
    }

    // -- The examples project.

    @Test
    void theExamplesFolderHoldsExactlyTheGeneratedPrograms() throws Exception {
        Path folder = Path.of(System.getProperty("examples.dir", "../examples/src/main/java"));
        assertTrue(Files.isDirectory(folder), "the examples project is next to the app project: " + folder.toAbsolutePath());
        Set<String> expected = new HashSet<>();
        // git may check the files out with CRLF (core.autocrlf); the generator emits LF, so only the text is compared
        for (Examples.Scenario scenario : Examples.Scenario.values()) {
            expected.add(scenario.fileName());
            Path file = folder.resolve(scenario.fileName());
            assertTrue(Files.isRegularFile(file), "missing " + file);
            assertEquals(Examples.generate(scenario, DEFAULTS), Files.readString(file, StandardCharsets.UTF_8).replace("\r\n", "\n"),
                file.getFileName() + " is out of date: regenerate it with  example " + scenario.id() + " > " + scenario.fileName());
        }
        Set<String> found = new HashSet<>();
        try (var files = Files.list(folder)) {
            files.forEach(f -> found.add(f.getFileName().toString()));
        }
        assertEquals(expected, found, "no other file in the examples project");
    }

    // -- The example command.

    private static String run(String... args) {
        ByteArrayOutputStream captured = new ByteArrayOutputStream();
        Main.example(CommandLine.parse(prepend("example", args)), new PrintStream(captured, true, StandardCharsets.UTF_8));
        return captured.toString(StandardCharsets.UTF_8);
    }

    private static String[] prepend(String first, String[] rest) {
        String[] all = new String[rest.length + 1];
        all[0] = first;
        System.arraycopy(rest, 0, all, 1, rest.length);
        return all;
    }

    @Test
    void theExampleCommandListsPrintsAndFillsTheScenarios() {
        String list = run();
        for (Examples.Scenario scenario : Examples.Scenario.values()) {
            assertTrue(list.contains(scenario.id()) && list.contains(scenario.title()), scenario.id());
        }

        assertEquals(Examples.generate(Examples.Scenario.LOBBY, DEFAULTS), run("lobby"));
        String track = run("--column", "income/min", "--player", "Carol", "track-player");
        assertTrue(track.contains("player.stats().getMoneyPerMinute()") && track.contains("\"Carol\""), track);
        assertTrue(run("--kind", "capture", "react-to-event").contains("STATEVENT_CAPTURE"));
        assertTrue(run("--kind", "STATEVENT_SELL", "react-to-event").contains("STATEVENT_SELL"));
        String chat = run("--channel", "match", "--audience", "observers", "--text", "hi all", "send-chat");
        assertTrue(chat.contains("CHAT_AUDIENCE_OBSERVERS") && chat.contains("\"hi all\""), chat);
    }

    @Test
    void theExampleCommandExplainsMistakes() {
        String unknown = assertThrows(IllegalArgumentException.class, () -> run("frobnicate")).getMessage();
        assertTrue(unknown.contains("track-player") && unknown.contains("ui-thread"), unknown);
        assertTrue(assertThrows(IllegalArgumentException.class, () -> run("--column", "nope", "track-player")).getMessage().contains("Money"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> run("--kind", "nope", "react-to-event")).getMessage().contains("KILL"));
        assertTrue(assertThrows(IllegalArgumentException.class, () -> run("--channel", "nope", "send-chat")).getMessage().contains("game-lobby"));
    }

    // -- The Code tab.

    @Test
    void theCodeTabShowsTheSelectedScenarioAndFollowsItsControls() {
        CodeTab tab = new CodeTab();
        assertEquals(Examples.Scenario.PRINT_FRAMES, tab.scenario());
        assertTrue(tab.code().contains("class PrintEveryFrame"));

        tab.show(new CodeTab.Request(Examples.Scenario.TRACK_PLAYER, DEFAULTS.withPlayer("Dave").withColumn("APM")));
        assertEquals(Examples.Scenario.TRACK_PLAYER, tab.scenario());
        assertTrue(tab.code().contains("\"Dave\"") && tab.code().contains("player.stats().getApmCurrent()"), tab.code());
        assertEquals("Dave", tab.selection().playerName());
        assertEquals("APM", tab.selection().column());

        tab.show(new CodeTab.Request(Examples.Scenario.REACT_TO_EVENT, DEFAULTS.withEventKind(StatEventKind.STATEVENT_BUILD)));
        assertTrue(tab.code().contains("StatEventKind.STATEVENT_BUILD"), tab.code());

        tab.show(new CodeTab.Request(Examples.Scenario.SEND_CHAT, DEFAULTS.withChannel(ChatChannel.CHAT_CHANNEL_GAME_LOBBY).withChatText("ready?")));
        assertTrue(tab.code().contains("CHAT_CHANNEL_GAME_LOBBY") && tab.code().contains("\"ready?\""), tab.code());
    }
}
