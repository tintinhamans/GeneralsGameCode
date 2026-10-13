package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

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
        assertFalse(PipeDiscovery.parsePid("gencaster-1234").isPresent());
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
}
