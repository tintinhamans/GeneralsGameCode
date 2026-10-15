package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.OnlinePlayer;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import com.generalsonline.stats.proto.StatsSnapshot;
import com.generalsonline.stats.proto.Events;
import org.junit.jupiter.api.Test;

import javax.swing.JTable;
import javax.swing.event.TableModelEvent;
import javax.swing.table.TableColumn;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Table models, column views, sorting and filters. JTable and TableRowSorter work without a display. */
class TablesTest {
    private static GuiModel modelWithThreePlayers() {
        GuiModel model = new GuiModel();
        model.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m")
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(3).setPlayerIndex(2).setName("Zed").setTeam(1).setColor(0xFF0000FF).setIsHuman(true)
                .setOnline(OnlinePlayer.newBuilder().setHasStats(true).setEloRating(1200)))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(1).setPlayerIndex(3).setName("Amy").setTeam(0).setColor(0xFFFF0000).setIsHuman(true))
            .addPlayers(MatchPlayer.newBuilder().setLobbySlot(0).setPlayerIndex(4).setName("Cal").setTeam(1).setColor(0xFF00FF00).setIsHuman(true))
            .build());
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(10).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(50))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(60))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(4).setMoney(70)).build());
        return model;
    }

    // -- RowTableModel.

    @Test
    void addAllFiresOneInsertAndOneTrim() {
        RowTableModel<Integer> model = new RowTableModel<>(5, List.of(RowTableModel.column("n", Integer.class, (Integer r) -> r)));
        List<TableModelEvent> events = new ArrayList<>();
        model.addTableModelListener(events::add);

        model.addAll(List.of(1, 2, 3, 4));
        model.addAll(List.of(5, 6, 7, 8));

        assertEquals(5, model.getRowCount());
        assertEquals(4, model.getValueAt(0, 0), "the oldest rows were trimmed");
        assertEquals(3, events.size(), "two inserts and one trim, not one event per row");
        assertEquals(TableModelEvent.INSERT, events.get(0).getType());
        assertEquals(TableModelEvent.INSERT, events.get(1).getType());
        assertEquals(TableModelEvent.DELETE, events.get(2).getType());
    }

    @Test
    void replaceAllUpdatesInPlaceWhenTheRowCountIsUnchanged() {
        RowTableModel<Integer> model = new RowTableModel<>(5, List.of(RowTableModel.column("n", Integer.class, (Integer r) -> r)));
        model.addAll(List.of(1, 2, 3));
        List<TableModelEvent> events = new ArrayList<>();
        model.addTableModelListener(events::add);

        model.replaceAll(List.of(4, 5, 6));
        assertEquals(1, events.size());
        assertEquals(TableModelEvent.UPDATE, events.get(0).getType());
        assertEquals(TableModelEvent.ALL_COLUMNS, events.get(0).getColumn(), "rows updated, not a structure change");

        model.replaceAll(List.of(7));
        assertEquals(2, events.size());
        assertEquals(TableModelEvent.UPDATE, events.get(1).getType());
        assertEquals(Integer.MAX_VALUE, events.get(1).getLastRow(), "a different row count tells the table all data changed");
    }

    @Test
    void selectionSurvivesALiveUpdateOfTheSameRows() {
        GuiModel model = modelWithThreePlayers();
        JTable table = GuiTables.table(model.players, "x", Map.of(), GuiTables.Sorting.BY_HEADER);
        table.setRowSelectionInterval(1, 1);
        Object selectedName = table.getValueAt(1, table.getColumn("Name").getModelIndex());

        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(20).setFull(true)
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(51))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(3).setMoney(61))
            .addPlayers(PlayerStats.newBuilder().setPlayerIndex(4).setMoney(71)).build());

        assertEquals(1, table.getSelectedRow());
        assertEquals(selectedName, table.getValueAt(1, table.getColumn("Name").getModelIndex()));
    }

    // -- Sorting.

    @Test
    void playersAreSortedByTeamThenLobbySlotByDefault() {
        GuiModel model = modelWithThreePlayers();
        JTable table = GuiTables.table(model.players, "x", Map.of(), GuiTables.Sorting.BY_HEADER);
        GuiTables.sortBy(table, "Team", "Slot");

        int name = model.players.columnIndex("Name");
        // team 1: Amy (slot 2); team 2: Cal (slot 1), Zed (slot 4)
        assertEquals("Amy", table.getValueAt(0, name));
        assertEquals("Cal", table.getValueAt(1, name));
        assertEquals("Zed", table.getValueAt(2, name));

        // A live update keeps that order.
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(20).setFull(true).addPlayers(PlayerStats.newBuilder().setPlayerIndex(4).setMoney(1)).build());
        assertEquals("Cal", table.getValueAt(1, name));
    }

    @Test
    void colorColumnSortsByTeamThenNameNotByTheSignedArgb() {
        ColorCell blueTeam1 = new ColorCell(0xFF0000FF, 1, "Zed");
        ColorCell redTeam1 = new ColorCell(0xFFFF0000, 1, "Amy");
        ColorCell greenTeam0 = new ColorCell(0xFF00FF00, 0, "Cal");
        List<ColorCell> cells = new ArrayList<>(List.of(blueTeam1, redTeam1, greenTeam0));
        java.util.Collections.sort(cells);
        assertEquals(List.of(greenTeam0, redTeam1, blueTeam1), cells);
        assertEquals(ColorCell.class, modelWithThreePlayers().players.getColumnClass(0));
    }

    @Test
    void logTablesDoNotSortWhenAHeaderIsClicked() {
        GuiModel model = new GuiModel();
        model.onEvents(Events.newBuilder().setFrame(1)
            .addEvents(StatEvent.newBuilder().setFrame(5).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("b"))
            .addEvents(StatEvent.newBuilder().setFrame(1).setKind(StatEventKind.STATEVENT_KILL).setPrimaryName("a")).build());
        JTable table = GuiTables.table(model.events, "x", Map.of(), GuiTables.Sorting.NONE);
        for (int i = 0; i < table.getColumnCount(); i++) {
            assertFalse(((javax.swing.table.TableRowSorter<?>) table.getRowSorter()).isSortable(i), "column " + i);
        }
        // Even asked to toggle, the order stays the arrival order.
        table.getRowSorter().toggleSortOrder(0);
        assertEquals(5L, table.getValueAt(0, 0));
    }

    @Test
    void headersAreNeverNarrowerThanTheirText() {
        GuiModel model = new GuiModel();
        JTable table = GuiTables.table(model.players, "x", Map.of("Name", 10), GuiTables.Sorting.BY_HEADER);
        java.awt.FontMetrics header = table.getTableHeader().getFontMetrics(table.getTableHeader().getFont());
        for (int i = 0; i < table.getColumnCount(); i++) {
            TableColumn column = table.getColumnModel().getColumn(i);
            assertTrue(column.getPreferredWidth() >= header.stringWidth(String.valueOf(column.getHeaderValue())), "column " + column.getHeaderValue());
        }
    }

    // -- Column views (P2).

    @Test
    void everyViewNamesRealColumnsAndEveryColumnIsInSomeView() {
        GuiModel model = new GuiModel();
        Set<String> shown = new HashSet<>();
        for (ColumnViews.View view : ColumnViews.PLAYERS) {
            assertEquals(List.of(), ColumnViews.unknownColumns(view, model.players), view.name());
            shown.addAll(view.columns());
        }
        for (int i = 0; i < model.players.getColumnCount(); i++) {
            String name = model.players.getColumnName(i);
            // Slot is the secondary sort key and has no place in a view of its own.
            assertTrue(shown.contains(name) || name.equals("Slot"), "column not in any view: " + name);
        }
        assertEquals("Overview", ColumnViews.defaultView().name());
        assertEquals("Overview", ColumnViews.PLAYERS.get(0).name());
    }

    @Test
    void applyingAViewShowsExactlyItsColumnsInOrderAndKeepsTheOthersForLater() {
        GuiModel model = modelWithThreePlayers();
        JTable table = GuiTables.table(model.players, "x", Map.of(), GuiTables.Sorting.BY_HEADER);
        Map<String, TableColumn> all = ColumnViews.columnsByName(table);

        ColumnViews.apply(table, all, ColumnViews.defaultView());
        List<String> shown = new ArrayList<>();
        for (int i = 0; i < table.getColumnCount(); i++) {
            shown.add(table.getColumnName(i));
        }
        assertEquals(ColumnViews.defaultView().columns(), shown);

        ColumnViews.apply(table, all, ColumnViews.PLAYERS.get(4));
        assertEquals("Net (latency / fps / ping)", table.getColumnName(2));
        assertEquals(ColumnViews.PLAYERS.get(4).columns().size(), table.getColumnCount());

        ColumnViews.apply(table, all, ColumnViews.defaultView());
        assertEquals("Money", table.getColumnName(table.convertColumnIndexToView(model.players.columnIndex("Money"))));
        // The sorter still works on hidden columns.
        GuiTables.sortBy(table, "Team", "Slot");
        assertEquals("Amy", table.getValueAt(0, table.convertColumnIndexToView(model.players.columnIndex("Name"))));
    }

    // -- Event filter (P4).

    private static GuiModel.EventRow row(GuiModel model, StatEventKind kind, int player, String name) {
        model.onEvents(Events.newBuilder().setFrame(1).addEvents(StatEvent.newBuilder().setFrame(1).setKind(kind).setPlayerIndex(player).setPrimaryName(name)).build());
        return model.events.get(model.events.getRowCount() - 1);
    }

    @Test
    void eventFilterCombinesKindPlayerAndText() {
        GuiModel model = modelWithThreePlayers();
        GuiModel.EventRow kill = row(model, StatEventKind.STATEVENT_KILL, 2, "ChinaTankGattling");
        GuiModel.EventRow build = row(model, StatEventKind.STATEVENT_BUILD, 3, "AmericaBarracks");

        EventFilter filter = new EventFilter();
        assertTrue(filter.matches(kill) && filter.matches(build));

        filter.setKind(StatEventKind.STATEVENT_KILL);
        assertTrue(filter.matches(kill));
        assertFalse(filter.matches(build));

        filter.setKind(null);
        filter.setPlayer(3);
        assertFalse(filter.matches(kill));
        assertTrue(filter.matches(build));

        filter.setPlayer(null);
        filter.setText("  BARRACKS ");
        assertFalse(filter.matches(kill));
        assertTrue(filter.matches(build));

        filter.setText("amy");
        assertTrue(filter.matches(build), "the player name is searched too");
    }

    @Test
    void eventFilterWorksThroughTheTableSorter() {
        GuiModel model = modelWithThreePlayers();
        row(model, StatEventKind.STATEVENT_KILL, 2, "a");
        row(model, StatEventKind.STATEVENT_BUILD, 3, "b");
        row(model, StatEventKind.STATEVENT_KILL, 3, "c");
        JTable table = GuiTables.table(model.events, "x", Map.of(), GuiTables.Sorting.NONE);
        @SuppressWarnings("unchecked")
        javax.swing.table.TableRowSorter<javax.swing.table.TableModel> sorter = (javax.swing.table.TableRowSorter<javax.swing.table.TableModel>) table.getRowSorter();

        EventFilter filter = new EventFilter();
        filter.setKind(StatEventKind.STATEVENT_KILL);
        sorter.setRowFilter(filter.asRowFilter());
        assertEquals(2, table.getRowCount());
        assertEquals("a", table.getValueAt(0, model.events.columnIndex("Name")));
        assertEquals("c", table.getValueAt(1, model.events.columnIndex("Name")));
    }

    // -- Stat cards (P3).

    @Test
    void playerCardHasEveryFieldIncludingTheOnlineRating() {
        GuiModel model = modelWithThreePlayers();
        String card = StatCard.forPlayer(model.players.get(0), model.players.get(0).stateText(), model.logicFps());
        assertTrue(card.startsWith("Zed  -  alive  -  as of frame 10"), card);
        assertTrue(card.contains("elo_rating: 1200"), card);
        assertTrue(card.contains("money: 50"), card);
        assertTrue(card.contains("name: \"Zed\""), card);
        assertTrue(card.contains("== PlayerStats"), card);
    }

    @Test
    void eventCardHasTheDecodedDetailAndTheRawEvent() {
        GuiModel model = modelWithThreePlayers();
        model.onEvents(Events.newBuilder().setFrame(90)
            .addEvents(StatEvent.newBuilder().setFrame(90).setPlayerIndex(2).setKind(StatEventKind.STATEVENT_VETERANCY_CHANGE).setPrimaryName("Crusader").setIntValue1(1).setIntValue2(2)).build());
        String card = StatCard.forEvent(model.events.get(0));
        assertTrue(card.contains("detail: Veteran -> Elite"), card);
        assertTrue(card.contains("primary_name: \"Crusader\""), card);
        assertTrue(card.contains("int_value2: 2"), card);
    }
}
