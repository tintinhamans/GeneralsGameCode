package com.generalsonline.stats.app;

import com.generalsonline.stats.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.MatchInfo;
import com.generalsonline.stats.proto.MatchPhase;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.PlayerStats;
import com.generalsonline.stats.proto.StatsSnapshot;
import org.junit.jupiter.api.Test;

import javax.swing.JComponent;
import javax.swing.JTabbedPane;
import java.awt.Component;
import java.awt.Container;
import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.util.HashSet;
import java.util.Set;

/** The Swing layer builds and paints without a display; what it looks like on screen is not tested. */
class GuiRenderTest {
    private static JTabbedPane tabsOf(Container root) {
        for (Component child : root.getComponents()) {
            if (child instanceof JTabbedPane tabs) {
                return tabs;
            }
        }
        throw new AssertionError("no tabs");
    }

    @Test
    void theWholeWindowContentBuildsAndPaintsWithData() {
        StatsGui gui = new StatsGui();
        JComponent content = gui.buildContent();
        GuiModel model = gui.model();
        model.onMatchInfo(MatchInfo.newBuilder().setPhase(MatchPhase.MATCH_PHASE_STARTED).setMatchId("m").setHasWorldExtent(true).setWorldMaxX(1000).setWorldMaxY(1000)
            .addPlayers(MatchPlayer.newBuilder().setPlayerIndex(2).setName("Alice").setColor(0xFFFF0000).setIsHuman(true)).build());
        model.onStatsSnapshot(StatsSnapshot.newBuilder().setFrame(300).setFull(true).addPlayers(PlayerStats.newBuilder().setPlayerIndex(2).setMoney(1234)).build());
        model.onCamera(TestMessages.camera(), 1);

        JTabbedPane tabs = tabsOf(content);
        Set<String> titles = new HashSet<>();
        for (int i = 0; i < tabs.getTabCount(); i++) {
            titles.add(tabs.getTitleAt(i));
        }
        assertEquals(Set.of("Players", "Minimap", "Events", "Chat", "Lobby", "Match", "Frames", "Code"), titles);

        content.setSize(1200, 700);
        content.doLayout();
        for (int i = 0; i < tabs.getTabCount(); i++) {
            tabs.setSelectedIndex(i);
            tabs.doLayout();
            BufferedImage image = new BufferedImage(1200, 700, BufferedImage.TYPE_INT_ARGB);
            Graphics2D g = image.createGraphics();
            content.paint(g);
            g.dispose();
            assertTrue(image.getRGB(600, 350) != 0 || image.getRGB(10, 10) != 0, "tab " + tabs.getTitleAt(i) + " painted");
        }
    }
}
