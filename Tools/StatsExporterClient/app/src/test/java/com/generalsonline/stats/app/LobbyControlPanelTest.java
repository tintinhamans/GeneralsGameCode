package com.generalsonline.stats.app;

import static com.generalsonline.stats.app.PanelRig.edt;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.DumpWriter;
import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.RawFrame;
import com.generalsonline.stats.proto.LobbyKind;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.UiScreen;
import com.generalsonline.stats.proto.UiState;
import com.google.protobuf.Message;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import javax.imageio.ImageIO;
import javax.swing.JMenuItem;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;

/** The lobby controls under the Lobby tab, against a fake game whose dump sits in a LAN lobby. */
class LobbyControlPanelTest {
    @TempDir
    Path dir;

    private static LobbySlot human(int slot, String name, boolean host, String side, int color) {
        return LobbySlot.newBuilder().setState(LobbySlotState.LOBBY_SLOT_HUMAN).setIsHost(host).setStartPosition(-1)
            .setPlayer(MatchPlayer.newBuilder().setLobbySlot(slot).setPlayerIndex(-1).setName(name).setIsHuman(true).setSide(side).setColor(color).setTeam(-1).setStartPosition(-1))
            .build();
    }

    private Path lobbyDump() throws IOException {
        LobbyState.Builder lobby = LobbyState.newBuilder().setInLobby(true).setKind(LobbyKind.LOBBY_KIND_LAN).setMapName("maps\\tournament desert\\tournament desert.map")
            .addSlots(human(0, "Alice", true, "America", 0xFF0000FF)).addSlots(human(1, "Bob", false, "China", 0xFFFF0000));
        for (int i = 2; i < 8; i++) {
            lobby.addSlots(LobbySlot.newBuilder().setStartPosition(-1));
        }
        Path file = dir.resolve("lobby.dump");
        try (DumpWriter writer = new DumpWriter(file)) {
            long n = 0;
            for (Message m : new Message[] {TestMessages.hello(), UiState.newBuilder().setScreen(UiScreen.UI_SCREEN_LAN_GAME_SETUP).build(), lobby.build()}) {
                writer.write(new RawFrame(TestMessages.frameTypeOf(m), n, n * 1000, m.toByteArray()));
                n++;
            }
        }
        return file;
    }

    private PanelRig rig(int localSlot) throws Exception {
        PanelRig rig = new PanelRig(dir, FakeGame.builder(lobbyDump()).pid(5170).speed(0).lobbyLocalSlot(localSlot));
        rig.awaitUi("the lobby", () -> rig.gui.model().lobbySlots.getRowCount() == 8);
        return rig;
    }

    private static boolean slotState(PanelRig rig, int slot, LobbySlotState state) {
        return rig.gui.model().lobbySlots.get(slot).data().getState() == state;
    }

    @Test
    void theHostAddsAComputerOpponentFromTheMenuAndThePanel() throws Exception {
        try (PanelRig rig = rig(-1)) {
            LobbyControlPanel panel = rig.gui.lobbyPanel();
            JMenuItem addAi = edt(() -> rig.gui.popupItem(rig.gui.lobbyTable(), 2, "Add hard AI"));
            assertTrue(edt(addAi::isEnabled), "an open slot takes an AI");
            assertFalse(edt(() -> rig.gui.popupItem(rig.gui.lobbyTable(), 1, "Add hard AI").isEnabled()), "a human's slot does not");
            assertTrue(edt(() -> rig.gui.popupItem(rig.gui.lobbyTable(), 1, "Kick player").isEnabled()));
            assertFalse(edt(() -> rig.gui.popupItem(rig.gui.lobbyTable(), 2, "Kick player").isEnabled()));
            edt(() -> addAi.doClick());
            rig.awaitUi("the AI", () -> slotState(rig, 2, LobbySlotState.LOBBY_SLOT_AI_BRUTAL) && panel.resultText().contains("AI added"));
            assertTrue(edt(panel::resultText).contains("applied at once"), edt(panel::resultText));

            edt(() -> {
                panel.slotSpinner().setValue(3);
                panel.box("faction").setSelectedItem("GLA");
                panel.button("faction").doClick();
            });
            rig.awaitUi("the faction", () -> rig.gui.model().lobbySlots.get(2).data().getPlayer().getSide().equals("GLA"));

            edt(() -> {
                panel.box("color").setSelectedItem("#00FF00");
                panel.button("color").doClick();
            });
            rig.awaitUi("the color", () -> rig.gui.model().lobbySlots.get(2).data().getPlayer().getColor() == 0xFF00FF00);

            edt(() -> panel.box("color").setSelectedItem("#0000FF"));
            edt(() -> panel.button("color").doClick());
            rig.awaitUi("the refusal", () -> panel.resultText().contains("NOT ALLOWED"));
            assertTrue(edt(panel::resultText).contains("held by another slot"), edt(panel::resultText));

            edt(() -> rig.gui.popupItem(rig.gui.lobbyTable(), 1, "Kick player").doClick());
            rig.awaitUi("the kick", () -> slotState(rig, 1, LobbySlotState.LOBBY_SLOT_OPEN));
        }
    }

    @Test
    void aClientSeesTheScreensRefusalsInOrange() throws Exception {
        try (PanelRig rig = rig(1)) {
            LobbyControlPanel panel = rig.gui.lobbyPanel();
            edt(() -> {
                panel.slotSpinner().setValue(3);
                panel.button("ai").doClick();
            });
            rig.awaitUi("the refusal", () -> panel.resultText().contains("NOT ALLOWED"));
            assertTrue(edt(panel::resultText).contains("only the host"), edt(panel::resultText));
            save("lobby-control-not-allowed", rig);

            edt(() -> panel.button("accept").doClick());
            rig.awaitUi("accepted", () -> rig.gui.model().lobbySlots.get(1).data().getIsReady() && panel.resultText().contains("requested from the host"));
            save("lobby-control", rig);
        }
    }

    @Test
    void colorsAreTypedAsHexOrRandom() {
        assertEquals(0, LobbyControlPanel.parseColor("Random"));
        assertEquals(0, LobbyControlPanel.parseColor("  "));
        assertEquals(0xFF3060FF, LobbyControlPanel.parseColor("#3060FF"));
        assertEquals(0x803060FF, LobbyControlPanel.parseColor("0x803060FF"));
        assertNull(LobbyControlPanel.parseColor("blue"));
        assertNull(LobbyControlPanel.parseColor("#123456789"));
        assertNotNull(LobbyControlPanel.aiLevel(2));
    }

    private static void save(String name, PanelRig rig) throws IOException {
        edt(() -> rig.gui.tabs().setSelectedIndex(indexOf(rig, "Lobby")));
        BufferedImage image = PanelRig.render(rig.content, 1200, 700);
        String target = System.getProperty("shots.dir");
        if (target != null) {
            Path folder = Path.of(target);
            Files.createDirectories(folder);
            ImageIO.write(image, "png", folder.resolve(name + ".png").toFile());
        }
    }

    private static int indexOf(PanelRig rig, String title) {
        return edt(() -> rig.gui.tabs().indexOfTab(title));
    }
}
