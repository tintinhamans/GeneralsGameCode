package com.generalsonline.stats.app;

import static com.generalsonline.stats.app.PanelRig.edt;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.proto.StatEventKind;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import javax.swing.JMenuItem;
import java.nio.file.Path;

/** The Query, Camera, In view, Minimap overlay and Director panels, driven against a fake game in this process. */
class ControlPanelsTest {
    @TempDir
    Path dir;

    // -- Query.

    @Test
    void theCatalogPagesForwardAndBackAndFiltersByName() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            QueryTab q = rig.gui.queryTab();
            edt(() -> {
                q.pageSize().setValue(2);
                q.searchButton().doClick();
            });
            rig.awaitUi("the first page", () -> q.entries.getRowCount() == 2 && q.pageText().startsWith("page 1"));
            assertTrue(edt(() -> q.nextButton().isEnabled()));
            assertFalse(edt(() -> q.previousButton().isEnabled()));
            String first = edt(() -> (String) q.entries.valueAt(0, "Name"));

            edt(() -> q.nextButton().doClick());
            rig.awaitUi("the second page", () -> q.pageText().startsWith("page 2"));
            assertTrue(edt(() -> q.pageText().contains("rows 3 to 4")), edt(q::pageText));
            assertFalse(first.equals(edt(() -> (String) q.entries.valueAt(0, "Name"))));

            edt(() -> q.previousButton().doClick());
            rig.awaitUi("back on the first page", () -> q.pageText().startsWith("page 1") && first.equals(q.entries.valueAt(0, "Name")));

            edt(() -> q.lookUp("AmericaBarracks", ""));
            rig.awaitUi("the lookup", () -> q.entries.getRowCount() == 1 && "AmericaBarracks".equals(q.entries.valueAt(0, "Name")));
            assertEquals(1500L, edt(() -> q.entries.valueAt(0, "Cost")));
            assertFalse(edt(() -> q.nextButton().isEnabled()), "one match is one page");
        }
    }

    @Test
    void anObjectCanBeInspectedAndAMissingOneSaysNotFound() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            QueryTab q = rig.gui.queryTab();
            rig.awaitFrame(40);
            edt(() -> q.inspect(101));
            rig.awaitUi("the object text", () -> q.objectText().contains("AmericaTankCrusader"));
            assertTrue(edt(q::objectText).contains("Alice"), edt(q::objectText));
            assertEquals(101, q.lastObjectId());

            edt(() -> q.inspect(4242));
            rig.awaitUi("not found", () -> q.statusOf("object").contains("NOT_FOUND"));
            assertEquals("", edt(q::objectText));

            edt(() -> q.inspect(0));
            assertTrue(edt(() -> q.statusOf("object")).contains("INVALID"));
        }
    }

    @Test
    void rightClickingAnEventOffersToInspectItsObjectAndItsKiller() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 2, true)) {
            rig.awaitFrame(100);
            GuiModel model = rig.gui.model();
            int build = -1;
            int kill = -1;
            int ability = -1;
            for (int i = 0; i < model.events.getRowCount(); i++) {
                StatEventKind kind = model.events.get(i).event().getKind();
                if (kind == StatEventKind.STATEVENT_BUILD && build < 0) {
                    build = i;
                } else if (kind == StatEventKind.STATEVENT_KILL && kill < 0) {
                    kill = i;
                } else if (kind == StatEventKind.STATEVENT_ABILITY_USE && ability < 0) {
                    ability = i;
                }
            }
            assertTrue(build >= 0 && kill >= 0, "the fixture's build and kill events arrived");
            final int buildRow = build;
            final int killRow = kill;
            JMenuItem inspect = edt(() -> rig.gui.popupItem(rig.gui.eventsTable(), buildRow, "Inspect object"));
            assertTrue(edt(inspect::isEnabled));
            assertFalse(edt(() -> rig.gui.popupItem(rig.gui.eventsTable(), buildRow, "Inspect killer").isEnabled()), "a build has no killer");
            assertTrue(edt(() -> rig.gui.popupItem(rig.gui.eventsTable(), killRow, "Inspect killer").isEnabled()));
            edt(() -> rig.gui.popupItem(rig.gui.eventsTable(), buildRow, "Inspect object").doClick());
            assertEquals(rig.gui.queryTab(), edt(() -> rig.gui.tabs().getSelectedComponent()), "the Query tab comes to the front");
            long expected = model.events.get(buildRow).event().getObjectId();
            rig.awaitUi("the inspected object " + expected, () -> rig.gui.queryTab().lastObjectId() == expected);
            assertTrue(expected != 0);
            if (ability >= 0) {
                final int abilityRow = ability;
                assertFalse(edt(() -> rig.gui.popupItem(rig.gui.eventsTable(), abilityRow, "Inspect object").isEnabled()), "an ability use names no object");
            }
        }
    }

    @Test
    void thePlayerAndTheMapAreAskedFor() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            QueryTab q = rig.gui.queryTab();
            rig.awaitUi("the players of the match", () -> q.playerBox().getItemCount() == 2);
            edt(() -> q.playerButton().doClick());
            rig.awaitUi("the player text", () -> q.playerArea().getText().contains("America"));
            assertTrue(edt(() -> q.playerArea().getText()).contains("Alice"));

            edt(() -> q.mapButton().doClick());
            rig.awaitUi("the map", () -> q.mapView().hasImage());
            assertTrue(edt(() -> q.statusOf("map")).contains("fixture ridge"), edt(() -> q.statusOf("map")));
        }
    }

    @Test
    void theFeedControlsReachTheGameAndShowTheEffectiveConfiguration() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            QueryTab q = rig.gui.queryTab();
            rig.awaitFrame(10);
            edt(() -> {
                q.interestBox().setSelected(true);
                q.applyButton().doClick();
            });
            rig.awaitUi("interest on in the game", () -> rig.game.feedConfig(0).getInterest());
            assertEquals(32, rig.game.feedConfig(0).getGridResolution());
            rig.awaitUi("the status", () -> q.statusOf("feed").contains("applied"));

            edt(() -> q.resyncButton().doClick());
            rig.awaitUi("resync", () -> q.statusOf("feed").contains("resync asked"));
        }
    }

    @Test
    void withoutAGameTheQueriesSayWhy() throws Exception {
        PanelCommandsStub stub = new PanelCommandsStub();
        QueryTab q = new QueryTab(stub.commands, new GuiModel());
        q.searchButton().doClick();
        assertTrue(q.statusOf("catalog").contains("NOT CONNECTED"), q.statusOf("catalog"));
        q.inspect(5);
        assertTrue(q.statusOf("object").contains("NOT CONNECTED"));
    }

    /** Commands with no pipe, answering on the calling thread. */
    private static final class PanelCommandsStub {
        final PipeCommands commands = new PipeCommands(() -> null, Runnable::run);
    }

    // -- Camera, replay, observer.

    @Test
    void aCameraCommandMovesTheGameAndTheLiveLineSaysACommandDidIt() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            CameraTab c = rig.gui.cameraTab();
            rig.awaitFrame(10);
            edt(() -> {
                c.field("x").setText("1800");
                c.field("y").setText("2400");
                c.field("zoom").setText("1.5");
                c.field("duration").setText("0");
                c.button("move").doClick();
            });
            rig.awaitUi("the command answered", () -> c.resultText().contains("moving"));
            rig.awaitUi("the camera frame", () -> c.liveText().contains("1800, 2400") && c.liveText().contains("a command"));

            edt(() -> {
                c.field("x").setText("5");
                c.field("y").setText("");
                c.button("move").doClick();
            });
            assertTrue(edt(c::resultText).contains("together"), "x without y is refused before it is sent: " + edt(c::resultText));
            edt(() -> {
                c.field("x").setText("");
                c.field("zoom").setText("");
                c.button("move").doClick();
            });
            assertTrue(edt(c::resultText).contains("nothing to move"), edt(c::resultText));

            edt(() -> c.button("reset").doClick());
            rig.awaitUi("the reset", () -> c.resultText().contains("camera reset"));
            edt(() -> c.button("current").doClick());
            assertFalse(edt(() -> c.field("x").getText()).isEmpty(), "the fields are filled from the camera frame");
        }
    }

    @Test
    void anActivePlayersCameraIsNeverTouchedAndTheRefusalIsShownAsSuch() throws Exception {
        try (PanelRig rig = new PanelRig(dir, FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1).loop(true).situation(FakeGame.Situation.PLAYER))) {
            CameraTab c = rig.gui.cameraTab();
            rig.awaitFrame(10);
            edt(() -> {
                c.field("x").setText("10");
                c.field("y").setText("10");
                c.button("move").doClick();
            });
            rig.awaitUi("the refusal", () -> c.resultText().contains("NOT ALLOWED"));
            String text = edt(c::resultText);
            assertTrue(text.contains("the game refused") && text.contains("observer"), text);
            edt(() -> c.button("pause").doClick());
            rig.awaitUi("the replay refusal", () -> c.resultText().contains("NOT ALLOWED"));
        }
    }

    @Test
    void followingAUnitAndReleasingIt() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            CameraTab c = rig.gui.cameraTab();
            rig.awaitFrame(35);
            edt(() -> {
                c.field("follow").setText("101");
                c.button("follow").doClick();
            });
            rig.awaitUi("the lock", () -> c.liveText().contains("lock: follow object 101"));
            edt(() -> c.button("unfollow").doClick());
            rig.awaitUi("released", () -> c.liveText().contains("lock: none"));

            edt(() -> {
                c.field("follow").setText("424242");
                c.button("follow").doClick();
            });
            rig.awaitUi("no such unit", () -> c.resultText().contains("NOT_FOUND"));
        }
    }

    @Test
    void replayControlAndObserverPickReachTheGame() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            CameraTab c = rig.gui.cameraTab();
            rig.awaitFrame(10);
            edt(() -> c.button("pause").doClick());
            rig.awaitUi("paused", () -> rig.game.replayState().getPaused() && c.resultText().contains("replay paused"));
            edt(() -> c.button("resume").doClick());
            rig.awaitUi("playing", () -> !rig.game.replayState().getPaused());
            edt(() -> {
                c.speedBox().setSelectedItem("4");
                c.button("speed").doClick();
            });
            rig.awaitUi("capped", () -> c.resultText().contains("capped"));
            rig.awaitUi("observer choices", () -> c.observeBox().getItemCount() == 2);
            edt(() -> {
                c.observeBox().setSelectedIndex(1);
                c.button("observe").doClick();
            });
            rig.awaitUi("observing", () -> rig.game.observedPlayer() == 3);
            edt(() -> c.button("nobody").doClick());
            rig.awaitUi("nobody", () -> rig.game.observedPlayer() == -1);

            edt(() -> c.fastForwardBox().doClick());
            rig.awaitUi("fast forward", () -> rig.game.replayState().getFastForward());
        }
    }

    // -- In view.

    @Test
    void theInViewTabListsEachPlayersShareOfTheValueInView() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            ViewStatsTab v = rig.gui.viewTab();
            rig.awaitFrame(70);
            rig.awaitUi("view statistics rows", () -> v.rows().players.getRowCount() == 2 && v.rows().totalValue() > 0);
            double sum = 0;
            for (int i = 0; i < 2; i++) {
                sum += (Double) edt(() -> v.rows().players.valueAt(0, "Share"));
            }
            ViewStatsModel.PlayerRow alice = edt(() -> v.rows().players.get(0));
            ViewStatsModel.PlayerRow bob = edt(() -> v.rows().players.get(1));
            assertEquals("Alice", alice.name());
            assertTrue(alice.share() + bob.share() > 0.999 && alice.share() + bob.share() < 1.001, alice.share() + " + " + bob.share());
            assertTrue(alice.value() > 0 && bob.value() > 0 && alice.effective() <= alice.value());
            assertTrue(sum > 0);
        }
    }

    @Test
    void notableUnitsCanBeInspectedFromTheirMenu() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 2, true)) {
            ViewStatsTab v = rig.gui.viewTab();
            rig.awaitUi("a notable unit in view", () -> v.rows().notable.getRowCount() > 0);
            JMenuItem item = edt(() -> rig.gui.popupItem(v.notableTable(), 0, "Inspect object"));
            assertTrue(edt(item::isEnabled));
            int id = v.rows().notableRow(0).objectId();
            edt(() -> item.doClick());
            assertEquals(rig.gui.queryTab(), edt(() -> rig.gui.tabs().getSelectedComponent()));
            rig.awaitUi("the object asked for", () -> rig.gui.queryTab().statusOf("object").contains(Long.toString(Integer.toUnsignedLong(id))));
        }
    }

    // -- The interest overlay.

    @Test
    void theInterestSwitchTurnsTheFeedOnAndShowsHighlights() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 2, true)) {
            MinimapTab m = rig.gui.minimapTab();
            rig.awaitFrame(10);
            assertFalse(edt(() -> m.minimap().showsInterest()));
            edt(() -> m.interestBox().doClick());
            rig.awaitUi("the feed on", () -> rig.game.feedConfig(0).getInterest() && m.minimap().showsInterest());
            rig.awaitUi("interest frames", () -> rig.gui.model().state().interestMap().isPresent());
            rig.awaitFrame(130);
            rig.awaitUi("the superweapon highlight", () -> m.highlights.getRowCount() > 0);
            assertTrue(edt(() -> (String) m.highlights.valueAt(0, "What")).contains("Superweapon launch by Bob"), edt(() -> (String) m.highlights.valueAt(0, "What")));
            assertTrue(edt(() -> (String) m.highlights.valueAt(0, "Impact")).contains("in"), edt(() -> (String) m.highlights.valueAt(0, "Impact")));
            assertTrue(edt(() -> rig.gui.queryTab().interestBox().isSelected()), "the Query tab shows the new configuration");

            edt(() -> m.interestBox().doClick());
            rig.awaitUi("the feed off", () -> !rig.game.feedConfig(0).getInterest() && !m.minimap().showsInterest());
        }
    }

    // -- The director.

    @Test
    void theDirectorTabStartsAStopsAndShowsWhenAHumanTakesTheCamera() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 2, true)) {
            DirectorTab d = rig.gui.directorTab();
            rig.awaitFrame(10);
            assertEquals("Not running", edt(d::stateText));
            edt(() -> {
                d.modeBox().setSelectedItem(DirectorMode.HOTSPOT);
                d.startButton().doClick();
            });
            assertTrue(rig.gui.directorModel().running());
            rig.awaitUi("a move", () -> !d.targetText().contains("none yet") && d.targetText().contains("because"));
            assertTrue(edt(d::stateText).contains("mode HOTSPOT"), edt(d::stateText));

            rig.game.simulateUserCameraInput();
            rig.awaitUi("the banner", () -> d.bannerText().startsWith("PAUSED BY A HUMAN"));
            assertTrue(edt(() -> d.resumeButton().isEnabled()));
            edt(() -> d.resumeButton().doClick());
            rig.awaitUi("steering again", () -> !d.bannerText().startsWith("PAUSED BY A HUMAN"));

            edt(() -> d.startButton().doClick());
            assertFalse(rig.gui.directorModel().running());
            assertEquals("Not running", edt(d::stateText));
        }
    }

    @Test
    void theDirectorNeedsAGame() {
        DirectorModel model = new DirectorModel(new com.generalsonline.stats.MatchState(), () -> null);
        assertTrue(model.start(com.generalsonline.stats.DirectorConfig.defaults()).contains("no game is connected"));
        assertFalse(model.running());
        assertFalse(model.view().running());
    }

    // -- Show code for this.

    @Test
    void theNewPanelsOfferTheirScenarios() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            QueryTab q = rig.gui.queryTab();
            edt(() -> {
                q.pageSize().setValue(5);
                q.searchButton().doClick();
            });
            rig.awaitUi("catalog rows", () -> q.entries.getRowCount() > 0);
            String name = edt(() -> (String) q.entries.valueAt(0, "Name"));
            JMenuItem code = edt(() -> rig.gui.popupItem(q.catalogTable(), 0, "Show code for this"));
            edt(() -> code.doClick());
            CodeTab tab = rig.gui.codeTab();
            assertEquals(Examples.Scenario.QUERY_CATALOG, edt(tab::scenario));
            assertTrue(edt(tab::code).contains("\"" + name + "\""));
            assertEquals(tab, edt(() -> rig.gui.tabs().getSelectedComponent()));

            edt(() -> rig.gui.popupItem(q.catalogTable(), 0, "Look up this name").doClick());
            rig.awaitUi("the lookup", () -> q.entries.getRowCount() == 1);

            CameraTab c = rig.gui.cameraTab();
            edt(() -> {
                c.field("x").setText("11");
                c.field("y").setText("22");
                c.field("zoom").setText("3");
            });
            Examples.Selection selection = edt(c::cameraSelection);
            assertEquals(11.0, selection.cameraX());
            assertEquals(3.0, selection.zoom());
            assertNotNull(Examples.generate(Examples.Scenario.CAMERA_CONTROL, selection));
            assertEquals(0.25, edt(c::replaySelection).replaySpeed(), "the speed box starts at its first value");
        }
    }
}
