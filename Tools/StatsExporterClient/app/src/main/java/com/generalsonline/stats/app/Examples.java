package com.generalsonline.stats.app;

import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.proto.ChatAudience;
import com.generalsonline.stats.proto.ChatChannel;
import com.generalsonline.stats.proto.StatEventKind;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;

/**
 * The example-code generator: a list of scenarios, each a complete, minimal, runnable program that uses the library for
 * one thing, filled in with what the user selected (a Players column, an event kind, a chat channel). The GUI's Code tab
 * and the {@code example} command call {@link #generate}; the programs in {@code examples/} are the output for the
 * default selection.
 */
final class Examples {
    /** What the user can ask for. */
    enum Scenario {
        PRINT_FRAMES("print-frames", "PrintEveryFrame", "Connect and print every frame type",
            "The listener basics: connect, receive each of the eight frame types, errors and reconnects."),
        TRACK_PLAYER("track-player", "TrackPlayerStat", "Track one player's stat over time",
            "Follow a value of one player (money, income, army value, ...) with MatchState, which joins and merges the snapshots for you."),
        REACT_TO_EVENT("react-to-event", "ReactToEvent", "React to one kind of event",
            "Run code when a KILL, CAPTURE, UPGRADE_COMPLETE, ... happens, with the detail fields of that kind."),
        CAMERA_BOX("camera-box", "DrawCameraBox", "Draw the camera view box",
            "Turn a Camera frame into the four corners of the view on a minimap, using the world extent."),
        LOBBY("lobby", "ShowLobby", "Show the lobby with predicted factions",
            "Print the game setup lobby, with the predicted side, colour and start position of random slots."),
        SEND_CHAT("send-chat", "SendChat", "Send a chat line to a channel",
            "Write one line into the game's chat, with the limits and the one-reader caveat."),
        RECORD_REPLAY("record-replay", "RecordAndReplay", "Record a dump and replay it at 2x",
            "Write the raw frames to a file, then replay the file into the same listener at twice real time."),
        UI_THREAD("ui-thread", "UiThreadState", "Use MatchState from a UI thread",
            "Receive callbacks on the Swing event thread (or JavaFX) with an executor, and read the state from anywhere."),
        QUERY_CATALOG("query-catalog", "QueryCatalog", "Page through the catalog and look up a template",
            "Ask the game for its object templates a page at a time, filtered by side, and find one by its internal name."),
        INSPECT_OBJECT("inspect-object", "InspectObject", "Inspect one object by id",
            "Look a live object up by the id an event, a selection or a view statistic gave: template, owner, place, health, veterancy."),
        CAMERA_CONTROL("camera-control", "CameraControl", "Move the camera and follow a unit",
            "Pan the game's camera with a transition, then lock it on a unit. Works in any running match; the game answers with a reason when it cannot."),
        REPLAY_CONTROL("replay-control", "ReplayControl", "Pause, resume and change the speed of a replay",
            "Steer a replay playback: pause, resume, a speed ratio (capped by the game) and fast forward."),
        VIEW_STATS("view-stats", "ViewStatsShare", "Print each player's share of the value in view",
            "Read the ViewStats frames and print what share of the value inside the camera's view each player holds."),
        DIRECTOR("director", "RunDirector", "Run the automatic camera director",
            "Let the Director steer the camera in a mode (hotspot, highlights, overview or all three) and print what it does."),
        CONTROL_LOBBY("lobby-control", "ControlLobby", "Change the game setup lobby",
            "Put a computer opponent into an open slot, choose its faction and set the starting cash, through the setup screen's own controls. The screen's rules apply: NOT_ALLOWED where the user could not.");

        private final String id;
        private final String className;
        private final String title;
        private final String description;

        Scenario(String id, String className, String title, String description) {
            this.id = id;
            this.className = className;
            this.title = title;
            this.description = description;
        }

        /** The name used on the command line, for example "track-player". */
        String id() {
            return id;
        }

        /** The class (and file) name of the generated program. */
        String className() {
            return className;
        }

        String title() {
            return title;
        }

        String description() {
            return description;
        }

        /** The file name to save the program under. */
        String fileName() {
            return className + ".java";
        }

        @Override
        public String toString() {
            return title;
        }

        /** The scenario with this id (case-insensitive), or null. */
        static Scenario byId(String name) {
            for (Scenario scenario : values()) {
                if (scenario.id.equalsIgnoreCase(name)) {
                    return scenario;
                }
            }
            return null;
        }
    }

    /**
     * What the user picked on screen. Scenarios ignore the parts they do not need.
     */
    static final class Selection {
        private final String playerName;
        private final String column;
        private final StatEventKind eventKind;
        private final ChatChannel channel;
        private final ChatAudience audience;
        private final String chatText;
        private final String templateName;
        private final String side;
        private final int objectId;
        private final double cameraX;
        private final double cameraY;
        private final double zoom;
        private final double replaySpeed;
        private final DirectorMode directorMode;

        Selection(String playerName, String column, StatEventKind eventKind, ChatChannel channel, ChatAudience audience, String chatText, String templateName, String side, int objectId, double cameraX, double cameraY, double zoom, double replaySpeed, DirectorMode directorMode) {
            this.playerName = playerName;
            this.column = column;
            this.eventKind = eventKind;
            this.channel = channel;
            this.audience = audience;
            this.chatText = chatText;
            this.templateName = templateName;
            this.side = side;
            this.objectId = objectId;
            this.cameraX = cameraX;
            this.cameraY = cameraY;
            this.zoom = zoom;
            this.replaySpeed = replaySpeed;
            this.directorMode = directorMode;
        }

        String playerName() {
            return playerName;
        }

        String column() {
            return column;
        }

        StatEventKind eventKind() {
            return eventKind;
        }

        ChatChannel channel() {
            return channel;
        }

        ChatAudience audience() {
            return audience;
        }

        String chatText() {
            return chatText;
        }

        String templateName() {
            return templateName;
        }

        String side() {
            return side;
        }

        int objectId() {
            return objectId;
        }

        double cameraX() {
            return cameraX;
        }

        double cameraY() {
            return cameraY;
        }

        double zoom() {
            return zoom;
        }

        double replaySpeed() {
            return replaySpeed;
        }

        DirectorMode directorMode() {
            return directorMode;
        }

        static Selection defaults() {
            return new Selection("Alice", "Money", StatEventKind.STATEVENT_KILL, ChatChannel.CHAT_CHANNEL_MATCH, ChatAudience.CHAT_AUDIENCE_EVERYONE, "gg wp",
                "AmericaTankCrusader", "America", 1234, 1800.0, 2400.0, 1.2, 0.5, DirectorMode.DIRECTOR);
        }

        private static String orElse(String value, String fallback) {
            return value == null || value.isBlank() ? fallback : value;
        }

        Selection withPlayer(String name) {
            return new Selection(orElse(name, playerName), column, eventKind, channel, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        Selection withColumn(String name) {
            return new Selection(playerName, name, eventKind, channel, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        Selection withEventKind(StatEventKind kind) {
            return new Selection(playerName, column, kind, channel, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        Selection withChannel(ChatChannel next) {
            return new Selection(playerName, column, eventKind, next, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        Selection withAudience(ChatAudience next) {
            return new Selection(playerName, column, eventKind, channel, next, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        Selection withChatText(String text) {
            return new Selection(playerName, column, eventKind, channel, audience, text, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        /** The template to look up and the side to list (a blank one keeps the current value). */
        Selection withTemplate(String name, String forSide) {
            return new Selection(playerName, column, eventKind, channel, audience, chatText, orElse(name, templateName), orElse(forSide, side), objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        Selection withObjectId(int id) {
            return new Selection(playerName, column, eventKind, channel, audience, chatText, templateName, side, id, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        /** Where the camera goes, and at what zoom. */
        Selection withCamera(double x, double y, double nextZoom) {
            return new Selection(playerName, column, eventKind, channel, audience, chatText, templateName, side, objectId, x, y, nextZoom, replaySpeed, directorMode);
        }

        Selection withReplaySpeed(double speed) {
            return new Selection(playerName, column, eventKind, channel, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, speed, directorMode);
        }

        Selection withDirectorMode(DirectorMode mode) {
            return new Selection(playerName, column, eventKind, channel, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, mode);
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof Selection)) {
                return false;
            }
            Selection other = (Selection) o;
            return Objects.equals(playerName, other.playerName)
                    && Objects.equals(column, other.column)
                    && Objects.equals(eventKind, other.eventKind)
                    && Objects.equals(channel, other.channel)
                    && Objects.equals(audience, other.audience)
                    && Objects.equals(chatText, other.chatText)
                    && Objects.equals(templateName, other.templateName)
                    && Objects.equals(side, other.side)
                    && objectId == other.objectId
                    && Double.compare(cameraX, other.cameraX) == 0
                    && Double.compare(cameraY, other.cameraY) == 0
                    && Double.compare(zoom, other.zoom) == 0
                    && Double.compare(replaySpeed, other.replaySpeed) == 0
                    && Objects.equals(directorMode, other.directorMode);
        }

        @Override
        public int hashCode() {
            return Objects.hash(playerName, column, eventKind, channel, audience, chatText, templateName, side, objectId, cameraX, cameraY, zoom, replaySpeed, directorMode);
        }

        @Override
        public String toString() {
            return "Selection[" + "playerName=" + playerName + ", column=" + column + ", eventKind=" + eventKind + ", channel=" + channel + ", audience=" + audience + ", chatText=" + chatText + ", templateName=" + templateName + ", side=" + side + ", objectId=" + objectId + ", cameraX=" + cameraX + ", cameraY=" + cameraY + ", zoom=" + zoom + ", replaySpeed=" + replaySpeed + ", directorMode=" + directorMode + "]";
        }
    }

    /**
     * How a column of the Players table is read from a {@code PlayerView} named {@code player}.
     */
    static final class StatColumn {
        private final String name;
        private final String expression;
        private final String note;

        StatColumn(String name, String expression, String note) {
            this.name = name;
            this.expression = expression;
            this.note = note;
        }

        String name() {
            return name;
        }

        String expression() {
            return expression;
        }

        String note() {
            return note;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof StatColumn)) {
                return false;
            }
            StatColumn other = (StatColumn) o;
            return Objects.equals(name, other.name)
                    && Objects.equals(expression, other.expression)
                    && Objects.equals(note, other.note);
        }

        @Override
        public int hashCode() {
            return Objects.hash(name, expression, note);
        }

        @Override
        public String toString() {
            return "StatColumn[" + "name=" + name + ", expression=" + expression + ", note=" + note + "]";
        }
    }

    private static StatColumn col(String name, String expression) {
        return new StatColumn(name, expression, "");
    }

    private static StatColumn col(String name, String expression, String note) {
        return new StatColumn(name, expression, note);
    }

    private static final List<StatColumn> STAT_COLUMNS = List.of(
        col("Color", "Format.colorHex(player.color())", "packed 0xAARRGGBB from MatchInfo; Format.colorHex turns it into #RRGGBB"),
        col("Name", "player.name()"),
        col("Side", "player.side()", "the raw faction name from MatchInfo"),
        col("Team", "player.team() + 1", "the wire counts teams from 0, the lobby screen from 1; -1 means unknown"),
        col("Money", "Format.u(player.stats().getMoney())", "uint32 fields come as Java ints; Format.u reads them as unsigned"),
        col("Income/min", "Format.u(player.stats().getMoneyPerMinute())", "cash deposited over the last 60 logic seconds"),
        col("Army value", "player.stats().getArmyValue()", "a heavy field: a light snapshot keeps the last full value"),
        col("APM", "Format.u(player.stats().getApmCurrent())", "actions per minute over the last 60 logic seconds"),
        col("Score", "player.stats().getScore()"),
        col("Rank", "player.stats().getRankLevel()"),
        col("Skill pts", "player.stats().getSkillPoints()", "unspent science purchase points"),
        col("Power (prod / use)", "player.stats().getEnergyProduction() + \" / \" + player.stats().getEnergyConsumption()"),
        col("Power sabotaged", "player.stats().getPowerSabotaged()"),
        col("Radar", "player.stats().getRadar()", "NONE, UNPOWERED or ACTIVE"),
        col("Units built", "Format.u(player.stats().getUnitsBuilt())"),
        col("Units lost", "Format.u(player.stats().getUnitsLost())"),
        col("Units destroyed", "Format.u(player.stats().getUnitsDestroyed())"),
        col("Value destroyed", "Format.u(player.stats().getValueDestroyed())", "cumulative build cost of enemy objects destroyed"),
        col("Value lost", "Format.u(player.stats().getValueLost())", "cumulative build cost of own objects lost"),
        col("Workers", "Format.u(player.stats().getWorkers())", "a heavy field: a light snapshot keeps the last full value"),
        col("Idle workers", "Format.u(player.stats().getIdleWorkers())", "a heavy field: a light snapshot keeps the last full value"),
        col("Battle plan", "player.stats().getBattlePlan()"),
        col("Production", "player.stats().getProductionCount()", "the number of structures producing; getProductionList() has the queues"),
        col("Heroes", "player.stats().getHeroesCount()", "heroes seen this match; getHeroesList() says which are alive"),
        col("Superweapons", "player.stats().getSuperweaponsCount()", "getSuperweaponsList() has the timers"),
        col("Net (latency / fps / ping)", "player.stats().getNetwork().getLatencyMs() + \" ms / \" + player.stats().getNetwork().getReportedFps() + \" fps / ping \" + player.stats().getNetwork().getPingMs() + \" ms\"",
            "-1 means this client does not know the value"),
        col("State", "player.stateText()", "alive, defeated, left, or after the match won/lost"),
        col("Slot", "player.lobbySlot() + 1", "the 0-based lobby slot, shown from 1"),
        col("ELO", "player.elo()", "-1 when the lobby had no statistics for the player"),
        col("Harvesters", "Format.u(player.stats().getHarvesters())", "a heavy field: a light snapshot keeps the last full value"),
        col("Buildings built", "Format.u(player.stats().getBuildingsBuilt())"),
        col("Buildings lost", "Format.u(player.stats().getBuildingsLost())"),
        col("Buildings destroyed", "Format.u(player.stats().getBuildingsDestroyed())"),
        col("Money earned", "Format.u(player.stats().getTotalMoneyEarned())"),
        col("Spent units", "Format.u(player.stats().getSpentUnits())"),
        col("Spent structures", "Format.u(player.stats().getSpentStructures())"),
        col("Spent upgrades", "Format.u(player.stats().getSpentUpgrades())"),
        col("Kills (inf/veh/air/str)", "player.stats().getKills().getInfantry() + \"/\" + player.stats().getKills().getVehicle() + \"/\" + player.stats().getKills().getAircraft() + \"/\" + player.stats().getKills().getStructure()",
            "kills by victim category"),
        col("Losses (inf/veh/air/str)", "player.stats().getLosses().getInfantry() + \"/\" + player.stats().getLosses().getVehicle() + \"/\" + player.stats().getLosses().getAircraft() + \"/\" + player.stats().getLosses().getStructure()",
            "losses by category"),
        col("Latency ms", "player.stats().getNetwork().getLatencyMs()", "-1 when this client does not know it"),
        col("Display FPS", "player.stats().getNetwork().getReportedFps()", "-1 when this client does not know it"),
        col("Ping ms", "player.stats().getNetwork().getPingMs()", "-1 for the local player or no connection"));

    /** One line of the generated event handler. */
    private static final class EventField {
        private final String label;
        private final String expression;

        private EventField(String label, String expression) {
            this.label = label;
            this.expression = expression;
        }

        String label() {
            return label;
        }

        String expression() {
            return expression;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof EventField)) {
                return false;
            }
            EventField other = (EventField) o;
            return Objects.equals(label, other.label)
                    && Objects.equals(expression, other.expression);
        }

        @Override
        public int hashCode() {
            return Objects.hash(label, expression);
        }

        @Override
        public String toString() {
            return "EventField[" + "label=" + label + ", expression=" + expression + "]";
        }
    }

    private static final class EventInfo {
        private final String description;
        private final List<EventField> fields;

        private EventInfo(String description, List<EventField> fields) {
            this.description = description;
            this.fields = fields;
        }

        String description() {
            return description;
        }

        List<EventField> fields() {
            return fields;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof EventInfo)) {
                return false;
            }
            EventInfo other = (EventInfo) o;
            return Objects.equals(description, other.description)
                    && Objects.equals(fields, other.fields);
        }

        @Override
        public int hashCode() {
            return Objects.hash(description, fields);
        }

        @Override
        public String toString() {
            return "EventInfo[" + "description=" + description + ", fields=" + fields + "]";
        }
    }

    private static EventField field(String label, String expression) {
        return new EventField(label, expression);
    }

    private static final EventField WHERE = field("where", "Format.eventLocation(e)");

    private static final Map<StatEventKind, EventInfo> EVENT_INFO = new LinkedHashMap<>();

    static {
        EVENT_INFO.put(StatEventKind.STATEVENT_ABILITY_USE, new EventInfo("a special power or ability was used (player = the user).",
            List.of(field("power", "Format.shortPowerName(e.getPrimaryName())"), field("source unit or target", "Format.shortName(e.getSecondaryName())"),
                field("variant", "e.getVariant()"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_SCIENCE_PURCHASE, new EventInfo("a science was purchased.",
            List.of(field("science", "e.getPrimaryName()"), field("rank level", "e.getIntValue1()"), field("purchase points left", "e.getIntValue2()"))));
        EVENT_INFO.put(StatEventKind.STATEVENT_KILL, new EventInfo("a player destroyed an object (player = the killer).",
            List.of(field("victim", "Format.shortName(e.getPrimaryName())"), field("category", "e.getCategory()"),
                field("victim cost", "Format.u(e.getCost())"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_LOSS, new EventInfo("a player lost an object (player = the owner).",
            List.of(field("lost", "Format.shortName(e.getPrimaryName())"), field("category", "e.getCategory()"),
                field("cost", "Format.u(e.getCost())"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_BUILD, new EventInfo("a player finished building an object (player = the owner).",
            List.of(field("built", "Format.shortName(e.getPrimaryName())"), field("category", "e.getCategory()"),
                field("starting veterancy", "Format.veterancyName(e.getIntValue1())"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_EVACUATE, new EventInfo("a transport or garrison was emptied.",
            List.of(field("transport", "Format.shortName(e.getPrimaryName())"), field("variant", "e.getVariant()"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_VETERANCY_CHANGE, new EventInfo("a unit earned a promotion.",
            List.of(field("unit", "Format.shortName(e.getPrimaryName())"),
                field("change", "Format.veterancyName(e.getIntValue1()) + \" -> \" + Format.veterancyName(e.getIntValue2())"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_SALVAGE_UPGRADE, new EventInfo("a unit got a salvage upgrade.",
            List.of(field("unit", "Format.shortName(e.getPrimaryName())"), field("kind", "e.getCategory()"), field("tier", "e.getIntValue1()"))));
        EVENT_INFO.put(StatEventKind.STATEVENT_BEACON, new EventInfo("a player placed a beacon.",
            List.of(WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_PLAYER_LEFT, new EventInfo("a player left the match, once per player.",
            List.of(field("how", "e.getVariant()"))));
        EVENT_INFO.put(StatEventKind.STATEVENT_PLAYER_DEFEATED, new EventInfo("the engine marked a player defeated.",
            List.of()));
        EVENT_INFO.put(StatEventKind.STATEVENT_SELL, new EventInfo("a structure finished selling.",
            List.of(field("structure", "Format.shortName(e.getPrimaryName())"), field("refund", "Format.u(e.getCost())"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_RADAR_CHANGED, new EventInfo("a player's radar changed state.",
            List.of(field("change", "state.eventDetail(e)"))));
        EVENT_INFO.put(StatEventKind.STATEVENT_BATTLE_PLAN_CHANGED, new EventInfo("a player's battle plan changed.",
            List.of(field("change", "state.eventDetail(e)"))));
        EVENT_INFO.put(StatEventKind.STATEVENT_UPGRADE_COMPLETE, new EventInfo("an upgrade finished.",
            List.of(field("upgrade", "Format.shortUpgradeName(e.getPrimaryName())"), field("cost", "Format.u(e.getCost())"),
                field("per object", "e.getVariant().equals(\"object\")"))));
        EVENT_INFO.put(StatEventKind.STATEVENT_CAPTURE, new EventInfo("a structure changed owner by capture (player = the new owner).",
            List.of(field("structure", "Format.shortName(e.getPrimaryName())"),
                field("old owner", "e.getIntValue1() < 0 ? \"neutral\" : state.playerName(e.getIntValue1())"),
                field("tech building", "e.getVariant().equals(\"tech\")"), field("cash bonus", "Format.u(e.getCost())"), WHERE)));
        EVENT_INFO.put(StatEventKind.STATEVENT_POWER_SABOTAGED, new EventInfo("a player's power was sabotaged (player = the victim).",
            List.of(field("lasts", "Format.gameTime(e.getIntValue1(), state.logicFps())"))));
    }

    private Examples() {
    }

    // -- Catalogues, for the GUI and the tests.

    /** The columns of the Players table that the generator knows how to read, in table order. */
    static List<StatColumn> statColumns() {
        return STAT_COLUMNS;
    }

    /** The accessor of a Players column, or null for a column the generator does not know. */
    static StatColumn statColumn(String name) {
        for (StatColumn column : STAT_COLUMNS) {
            if (column.name().equals(name)) {
                return column;
            }
        }
        return null;
    }

    /** The event kinds the generator has fields for: every kind of the protocol. */
    static List<StatEventKind> eventKinds() {
        return List.copyOf(EVENT_INFO.keySet());
    }

    /** The chat channels a line can be sent to. */
    static List<ChatChannel> chatChannels() {
        List<ChatChannel> channels = new ArrayList<>();
        for (ChatChannel channel : ChatChannel.values()) {
            if (channel != ChatChannel.UNRECOGNIZED) {
                channels.add(channel);
            }
        }
        return channels;
    }

    /** The audiences a line can be sent to. */
    static List<ChatAudience> chatAudiences() {
        return List.of(ChatAudience.CHAT_AUDIENCE_EVERYONE, ChatAudience.CHAT_AUDIENCE_ALLIES, ChatAudience.CHAT_AUDIENCE_OBSERVERS);
    }

    // -- Generation.

    /**
     * Fills the template of a scenario with the selection.
     *
     * @return the complete source of a Java program in the default package, named {@link Scenario#className()}
     * @throws IllegalArgumentException when the selection names a column or event kind the generator does not know
     */
    static String generate(Scenario scenario, Selection selection) {
        Map<String, String> values = new LinkedHashMap<>();
        String template;
        switch (scenario) {
            case PRINT_FRAMES:
                template = ExampleTemplates.PRINT_FRAMES;
                break;
            case TRACK_PLAYER:
                StatColumn column = statColumn(selection.column());
                if (column == null) {
                    throw new IllegalArgumentException("unknown column '" + selection.column() + "'; known: " + columnNames());
                }
                values.put("PLAYER", javaString(selection.playerName()));
                values.put("COLUMN", comment(column.name()));
                values.put("EXPRESSION", column.expression());
                values.put("NOTE", column.note().isEmpty() ? "." : " (" + column.note() + ").");
                template = ExampleTemplates.TRACK_PLAYER;
                break;
            case REACT_TO_EVENT:
                EventInfo info = EVENT_INFO.get(selection.eventKind());
                if (info == null) {
                    throw new IllegalArgumentException("unknown event kind " + selection.eventKind());
                }
                values.put("KIND", kindName(selection.eventKind()));
                values.put("KIND_CONSTANT", selection.eventKind().name());
                values.put("KIND_DESCRIPTION", info.description());
                values.put("FIELDS", fieldLines(info));
                template = ExampleTemplates.REACT_TO_EVENT;
                break;
            case CAMERA_BOX:
                template = ExampleTemplates.CAMERA_BOX;
                break;
            case LOBBY:
                template = ExampleTemplates.LOBBY;
                break;
            case SEND_CHAT:
                ChatChannel channel = selection.channel();
                values.put("TEXT", javaString(selection.chatText()));
                values.put("CHANNEL_CONSTANT", channel.name());
                values.put("CHANNEL_DESCRIPTION", channelDescription(channel, selection.audience()));
                values.put("AUDIENCE_ARGUMENT", channel == ChatChannel.CHAT_CHANNEL_MATCH && selection.audience() != ChatAudience.CHAT_AUDIENCE_EVERYONE
                    ? ", ChatAudience." + selection.audience().name() : "");
                template = ExampleTemplates.SEND_CHAT;
                break;
            case RECORD_REPLAY:
                template = ExampleTemplates.RECORD_REPLAY;
                break;
            case UI_THREAD:
                template = ExampleTemplates.UI_THREAD;
                break;
            case QUERY_CATALOG:
                values.put("SIDE", javaString(selection.side()));
                values.put("NAME", javaString(selection.templateName()));
                template = ExampleTemplates.QUERY_CATALOG;
                break;
            case INSPECT_OBJECT:
                values.put("OBJECT_ID", Integer.toString(selection.objectId()));
                template = ExampleTemplates.INSPECT_OBJECT;
                break;
            case CAMERA_CONTROL:
                values.put("X", number(selection.cameraX()));
                values.put("Y", number(selection.cameraY()));
                values.put("ZOOM", number(selection.zoom()));
                values.put("OBJECT_ID", Integer.toString(selection.objectId()));
                template = ExampleTemplates.CAMERA_CONTROL;
                break;
            case REPLAY_CONTROL:
                values.put("SPEED", number(selection.replaySpeed()));
                template = ExampleTemplates.REPLAY_CONTROL;
                break;
            case VIEW_STATS:
                template = ExampleTemplates.VIEW_STATS;
                break;
            case DIRECTOR:
                values.put("MODE", selection.directorMode().name());
                values.put("MODE_DESCRIPTION", modeDescription(selection.directorMode()));
                template = ExampleTemplates.DIRECTOR;
                break;
            case CONTROL_LOBBY:
                template = ExampleTemplates.CONTROL_LOBBY;
                break;
            default:
                throw new IllegalArgumentException("unknown scenario " + scenario);
        }
        return fill(template, values);
    }

    /** Replaces every {@code ${NAME}}; a name without a value is a programming error. */
    static String fill(String template, Map<String, String> values) {
        StringBuilder out = new StringBuilder(template.length() + 256);
        int at = 0;
        while (true) {
            int open = template.indexOf("${", at);
            if (open < 0) {
                out.append(template, at, template.length());
                return out.toString();
            }
            int close = template.indexOf('}', open);
            String name = template.substring(open + 2, close);
            String value = values.get(name);
            if (value == null) {
                throw new IllegalStateException("template placeholder ${" + name + "} has no value");
            }
            out.append(template, at, open).append(value);
            at = close + 1;
        }
    }

    private static String fieldLines(EventInfo info) {
        StringBuilder lines = new StringBuilder();
        String indent = " ".repeat(20);
        for (EventField field : info.fields()) {
            if (field.expression().equals("Format.eventLocation(e)")) {
                lines.append(indent).append("if (e.getHasLocation()) {\n")
                    .append(indent).append("    System.out.println(\"  ").append(field.label()).append(": \" + ").append(wrap(field.expression())).append(");\n")
                    .append(indent).append("}\n");
            } else {
                lines.append(indent).append("System.out.println(\"  ").append(field.label()).append(": \" + ").append(wrap(field.expression())).append(");\n");
            }
        }
        if (lines.length() == 0) {
            lines.append(indent).append("// no further fields: the player is the player_index of the event\n");
        }
        lines.setLength(lines.length() - 1); // the template supplies the newline
        return lines.toString();
    }

    /** A conditional expression needs parentheses to stay one operand of the string concatenation. */
    private static String wrap(String expression) {
        return expression.contains(" ? ") ? "(" + expression + ")" : expression;
    }

    /** A finite number as a Java double literal ("1800.0"). */
    private static String number(double value) {
        if (!Double.isFinite(value)) {
            throw new IllegalArgumentException("a number is needed here, got " + value);
        }
        return Double.toString(value);
    }

    private static String modeDescription(DirectorMode mode) {
        switch (mode) {
            case HOTSPOT:
                return "it goes to the place with the most going on and stays until something clearly better appears";
            case HIGHLIGHTS:
                return "it waits for a superweapon launch, a hero death or a big structure loss, shows it and returns";
            case OVERVIEW:
                return "it tours every player's base and army while it is quiet, and hands over to hotspots in a fight";
            case DIRECTOR:
                return "highlights interrupt, hotspots are followed and the overview fills the quiet";
            default:
                throw new IllegalArgumentException(String.valueOf(mode));
        }
    }

    private static String channelDescription(ChatChannel channel, ChatAudience audience) {
        switch (channel) {
            case CHAT_CHANNEL_MATCH:
                return "the in-match chat (" + audience.name().replace("CHAT_AUDIENCE_", "").toLowerCase(Locale.ROOT) + ")";
            case CHAT_CHANNEL_GAME_LOBBY:
                return "the game setup lobby chat";
            case CHAT_CHANNEL_ROOM:
                return "the chat room";
            case CHAT_CHANNEL_LAN_LOBBY:
                return "the LAN lobby chat";
            case CHAT_CHANNEL_LAN_GAME:
                return "the LAN game setup chat";
            default:
                return channel.name();
        }
    }

    /** "KILL" for STATEVENT_KILL. */
    static String kindName(StatEventKind kind) {
        return kind.name().replace("STATEVENT_", "");
    }

    private static List<String> columnNames() {
        List<String> names = new ArrayList<>();
        for (StatColumn column : STAT_COLUMNS) {
            names.add(column.name());
        }
        return names;
    }

    /** Escapes text for a Java string literal. */
    static String javaString(String text) {
        StringBuilder out = new StringBuilder();
        for (char c : text.toCharArray()) {
            switch (c) {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    out.append(c < 32 ? String.format("\\u%04x", (int) c) : String.valueOf(c));
                    break;
            }
        }
        return out.toString();
    }

    /** Makes text safe for a one-line comment and a string literal: no quotes, backslashes or line breaks. */
    private static String comment(String text) {
        return text.replace("\\", "/").replace("\"", "'").replace("\n", " ");
    }
}
