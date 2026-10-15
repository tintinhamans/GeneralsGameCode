package com.generalsonline.stats.app;

import com.generalsonline.stats.FakeGame;
import com.generalsonline.stats.MatchState;
import com.generalsonline.stats.StatsPipe;

import javax.swing.JComponent;
import javax.swing.SwingUtilities;
import java.awt.Component;
import java.awt.Container;
import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.nio.file.Path;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import java.util.function.Supplier;

/**
 * The window content wired to a fake game in this process, for the tests of the panels that ask the game things. Frames
 * reach the window model on the event thread like in the real window; the tests touch Swing only through {@link #edt}.
 */
final class PanelRig implements AutoCloseable {
    final FakeGame game;
    final StatsGui gui;
    final JComponent content;
    final StatsPipe pipe;

    PanelRig(Path dir, FakeGame.Builder configure) throws Exception {
        game = configure.namedPipe(false).holdForReader(true).build().start();
        gui = new StatsGui();
        content = edt(gui::buildContent);
        pipe = game.connect(StatsPipe.builder().listener(gui.feed()).reconnect(false)).build().start();
        gui.connectTo(pipe);
        if (pipe.awaitHello(Duration.ofSeconds(10)).isEmpty()) {
            throw new AssertionError("no Hello from the fake game");
        }
    }

    static PanelRig start(Path dir, double speed, boolean loop) throws Exception {
        return new PanelRig(dir, FakeGame.builder(FakeDumpFixture.write(dir)).pid(5150).speed(speed).loop(loop));
    }

    MatchState state() {
        return gui.model().state();
    }

    // -- The event thread.

    static void edt(Runnable action) {
        if (SwingUtilities.isEventDispatchThread()) {
            action.run();
            return;
        }
        try {
            SwingUtilities.invokeAndWait(action);
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    static <T> T edt(Supplier<T> action) {
        AtomicReference<T> result = new AtomicReference<>();
        edt(() -> result.set(action.get()));
        return result.get();
    }

    /** Waits until the condition holds, evaluating it on the event thread after the timers' work ran once. */
    void awaitUi(String what, BooleanSupplier condition) throws InterruptedException {
        long deadline = System.nanoTime() + Duration.ofSeconds(15).toNanos();
        while (true) {
            boolean done = edt(() -> {
                gui.refreshAll();
                return condition.getAsBoolean();
            });
            if (done) {
                return;
            }
            if (System.nanoTime() > deadline) {
                throw new AssertionError("timed out waiting for " + what);
            }
            Thread.sleep(20);
        }
    }

    /** Waits for the game to reach a logic frame (as the window's state saw it). */
    void awaitFrame(long frame) throws InterruptedException {
        awaitUi("game frame " + frame, () -> gui.model().state().frame() >= frame);
    }

    @Override
    public void close() {
        gui.directorModel().stop();
        pipe.close();
        game.close();
    }

    // -- Painting offscreen.

    /** Lays a component out and everything in it, top down, without a window. A table in a scroll pane gets its header, which a window would add when it shows the table. */
    static void layoutAll(Component component) {
        if (component instanceof javax.swing.JScrollPane scroll && scroll.getViewport().getView() instanceof javax.swing.JTable table && (scroll.getColumnHeader() == null
            || scroll.getColumnHeader().getView() == null)) {
            scroll.setColumnHeaderView(table.getTableHeader());
        }
        if (component instanceof Container container) {
            container.doLayout();
            for (Component child : container.getComponents()) {
                layoutAll(child);
            }
        }
    }

    /** Paints a component into an image of this size. */
    static BufferedImage render(JComponent component, int width, int height) {
        return edt(() -> {
            component.setSize(width, height);
            layoutAll(component);
            layoutAll(component);
            BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_ARGB);
            Graphics2D g = image.createGraphics();
            g.setColor(java.awt.Color.WHITE);
            g.fillRect(0, 0, width, height);
            component.paint(g);
            g.dispose();
            return image;
        });
    }
}
