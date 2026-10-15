package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

class PipeDiscoveryTest {
    @Test
    void parsesThePidOfAStatsPipeName() {
        assertEquals(1234, PipeDiscovery.parsePid("generalsonline-stats-1234").getAsInt());
        assertEquals(0, PipeDiscovery.parsePid("generalsonline-stats-0").getAsInt());
    }

    @Test
    void rejectsNamesThatAreNotStatsPipes() {
        assertFalse(PipeDiscovery.parsePid("generalsonline-stats-").isPresent());
        assertFalse(PipeDiscovery.parsePid("generalsonline-stats-12ab").isPresent());
        assertFalse(PipeDiscovery.parsePid("generalsonline-stats-99999999999").isPresent());
        assertFalse(PipeDiscovery.parsePid("generalsonline-stats--5").isPresent());
        assertFalse(PipeDiscovery.parsePid("other-1234").isPresent());
        assertFalse(PipeDiscovery.parsePid("generalsonline-1234").isPresent());
        assertFalse(PipeDiscovery.parsePid("generalsonline-stats1234").isPresent());
        assertFalse(PipeDiscovery.parsePid("xgeneralsonline-stats-1234").isPresent());
        assertFalse(PipeDiscovery.parsePid(null).isPresent());
    }

    @Test
    void picksOutStatsPipesFromADirectoryListing() {
        String[] listing = {"InitShutdown", "generalsonline-stats-300", "mojo.5688.8052", "generalsonline-stats-42", "generalsonline-stats-x"};
        assertEquals(List.of(42, 300), PipeDiscovery.pidsFromNames(listing));
        assertTrue(PipeDiscovery.pidsFromNames(new String[0]).isEmpty());
        assertTrue(PipeDiscovery.pidsFromNames(null).isEmpty());
    }

    @Test
    void buildsThePipePath() {
        assertEquals("\\\\.\\pipe\\generalsonline-stats-77", PipeDiscovery.pipePath(77));
        assertEquals(PipeDiscovery.pipePath(5), PipeDiscovery.choosePipe(5));
    }

    @Test
    void candidatesAreTheHighestPidFirstButEveryPipeIsTried() {
        assertEquals(List.of("\\\\.\\pipe\\generalsonline-stats-900", "\\\\.\\pipe\\generalsonline-stats-300", "\\\\.\\pipe\\generalsonline-stats-42"),
            PipeDiscovery.candidatesFromPids(List.of(42, 300, 900)));
        assertEquals(List.of("explicit"), PipeDiscovery.candidates(7, "explicit"), "an explicit pipe wins");
        assertEquals(List.of(PipeDiscovery.pipePath(7)), PipeDiscovery.candidates(7, null));
    }

    @Test
    void aBusyHighestPipeFallsThroughToTheNextFreeOne() {
        List<String> tried = new ArrayList<>();
        PipeDiscovery.Opened<String> opened = PipeDiscovery.openFirst(List.of("busy-900", "free-300", "free-42"), path -> {
            tried.add(path);
            if (path.startsWith("busy")) {
                throw new IOException("All pipe instances are busy");
            }
            return "channel for " + path;
        });
        assertEquals("free-300", opened.path());
        assertEquals("channel for free-300", opened.channel());
        assertEquals(List.of("busy-900", "free-300"), tried, "stops at the first free pipe");
    }

    @Test
    void whenEveryPipeIsBusyNothingOpens() {
        assertNull(PipeDiscovery.openFirst(List.of("a", "b"), path -> {
            throw new IOException("busy");
        }));
        assertNull(PipeDiscovery.openFirst(List.of(), path -> "never"));
    }

    @Test
    void thePidIsReadFromAPipePathForTheStatusBar() {
        assertEquals(1234, PipeDiscovery.pidOfPath("\\\\.\\pipe\\generalsonline-stats-1234").getAsInt());
        assertEquals(5, PipeDiscovery.pidOfPath("generalsonline-stats-5").getAsInt());
        assertFalse(PipeDiscovery.pidOfPath("\\\\.\\pipe\\other").isPresent());
        assertFalse(PipeDiscovery.pidOfPath(null).isPresent());
    }
}
