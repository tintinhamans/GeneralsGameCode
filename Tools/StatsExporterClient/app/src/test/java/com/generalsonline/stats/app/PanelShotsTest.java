package com.generalsonline.stats.app;

import static com.generalsonline.stats.app.PanelRig.edt;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.CameraMove;
import com.generalsonline.stats.DirectorMode;
import com.generalsonline.stats.FakeGame;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import javax.imageio.ImageIO;
import javax.swing.JComponent;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.TimeUnit;

/**
 * Renders the new panels offscreen, with data from a fake game, and checks that they paint something. With the system
 * property {@code shots.dir} the images are written there as PNG files, which is how the screenshots of the report are made.
 */
class PanelShotsTest {
    private static final int WIDTH = 1200;
    private static final int HEIGHT = 700;

    @TempDir
    Path dir;

    private static void save(String name, BufferedImage image) throws IOException {
        String target = System.getProperty("shots.dir");
        assertTrue(distinctColours(image) > 8, name + " painted something");
        if (target != null) {
            Path folder = Path.of(target);
            Files.createDirectories(folder);
            ImageIO.write(image, "png", folder.resolve(name + ".png").toFile());
        }
    }

    private static int distinctColours(BufferedImage image) {
        Set<Integer> colours = new HashSet<>();
        for (int y = 0; y < image.getHeight(); y += 3) {
            for (int x = 0; x < image.getWidth(); x += 3) {
                colours.add(image.getRGB(x, y));
            }
        }
        return colours.size();
    }

    private static BufferedImage shot(PanelRig rig, JComponent tab) {
        edt(() -> rig.gui.tabs().setSelectedComponent(tab));
        return PanelRig.render(rig.content, WIDTH, HEIGHT);
    }

    @Test
    void theNewPanelsPaintWithDataFromAFakeGame() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            QueryTab q = rig.gui.queryTab();
            rig.awaitFrame(70);

            // the Query tab, section by section
            edt(() -> {
                q.pageSize().setValue(12);
                q.searchButton().doClick();
            });
            rig.awaitUi("catalog rows", () -> q.entries.getRowCount() > 3);
            edt(() -> q.sections().setSelectedIndex(0));
            save("query-catalog", shot(rig, q));

            rig.awaitFrame(40);
            edt(() -> q.inspect(101));
            rig.awaitUi("the object", () -> q.objectText().contains("Crusader"));
            save("query-object", shot(rig, q));

            edt(() -> {
                q.sections().setSelectedIndex(3);
                q.mapButton().doClick();
            });
            rig.awaitUi("the map", () -> q.mapView().hasImage());
            save("query-map", shot(rig, q));

            edt(() -> q.sections().setSelectedIndex(2));
            rig.awaitUi("the players", () -> q.playerBox().getItemCount() == 2);
            edt(() -> q.playerButton().doClick());
            rig.awaitUi("the player text", () -> q.playerArea().getText().contains("America"));
            save("query-player", shot(rig, q));

            // the camera panel after a command
            CameraTab c = rig.gui.cameraTab();
            edt(() -> {
                c.field("x").setText("1800");
                c.field("y").setText("1500");
                c.field("zoom").setText("1.5");
                c.field("angle").setText("20");
                c.field("duration").setText("0");
                c.button("move").doClick();
            });
            rig.awaitUi("the camera moved", () -> c.liveText().contains("a command"));
            save("camera", shot(rig, c));

            // the interest overlay with a pending superweapon highlight, then the view statistics at the same moment
            MinimapTab m = rig.gui.minimapTab();
            edt(() -> m.interestBox().doClick());
            rig.awaitFrame(140);
            rig.awaitUi("a highlight", () -> m.highlights.getRowCount() > 0);
            rig.pipe.pauseReplay().get(10, TimeUnit.SECONDS);
            Thread.sleep(300);
            rig.awaitUi("the paused picture", () -> m.highlights.getRowCount() > 0);
            save("minimap-interest", shot(rig, m));
            save("in-view", shot(rig, rig.gui.viewTab()));
            rig.pipe.resumeReplay().get(10, TimeUnit.SECONDS);

            // the director, stepping back for a human
            DirectorTab d = rig.gui.directorTab();
            edt(() -> {
                d.modeBox().setSelectedItem(DirectorMode.HOTSPOT);
                d.startButton().doClick();
            });
            rig.awaitUi("a director move", () -> d.targetText().contains("because"));
            rig.game.simulateUserCameraInput();
            rig.awaitUi("the banner", () -> d.bannerText().startsWith("PAUSED BY A HUMAN"));
            save("director", shot(rig, d));
        }
    }

    @Test
    void theRefusalOfAnActivePlayersCameraPaintsInTheCameraPanel() throws Exception {
        try (PanelRig rig = new PanelRig(dir, FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(1).loop(true).situation(FakeGame.Situation.PLAYER))) {
            CameraTab c = rig.gui.cameraTab();
            rig.awaitFrame(20);
            edt(() -> {
                c.field("x").setText("1800");
                c.field("y").setText("1500");
                c.button("move").doClick();
            });
            rig.awaitUi("the refusal", () -> c.resultText().contains("NOT ALLOWED"));
            save("camera-not-allowed", shot(rig, c));
            rig.pipe.moveCamera(CameraMove.to(1, 1)).handle((ok, error) -> null).get(10, TimeUnit.SECONDS);
        }
    }

    @Test
    void theCodeTabListsTheNewScenarios() throws Exception {
        try (PanelRig rig = PanelRig.start(dir, 1, true)) {
            edt(() -> rig.gui.codeTab().show(new CodeTab.Request(Examples.Scenario.FAKE_SERVER, Examples.Selection.defaults())));
            save("code-fake-server", shot(rig, rig.gui.codeTab()));
        }
    }
}
