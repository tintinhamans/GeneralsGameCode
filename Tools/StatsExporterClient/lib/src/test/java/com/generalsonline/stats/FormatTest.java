package com.generalsonline.stats;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.generalsonline.stats.proto.Camera;
import com.generalsonline.stats.proto.StatEvent;
import com.generalsonline.stats.proto.StatEventKind;
import org.junit.jupiter.api.Test;

import java.io.IOException;

class FormatTest {
    @Test
    void gameTimeFormatsAtTheGivenLogicRate() {
        assertEquals("0:00", Format.gameTime(0, 30));
        assertEquals("0:00", Format.gameTime(-5, 30));
        assertEquals("1:05", Format.gameTime(65 * 30, 30));
        assertEquals("1:05", Format.gameTime(65 * 60, 60));
        assertEquals("1:01:01", Format.gameTime(3661 * 30, 30));
        assertEquals(3, Format.seconds(90, 30));
        assertEquals("1:30", Format.gameTime(90, 0), "a rate of 0 or less is treated as 1");
    }

    @Test
    void unsignedReadsAUint32AboveTwoToThe31() {
        assertEquals(4_000_000_000L, Format.u((int) 4_000_000_000L));
        assertEquals(5, Format.u(5));
    }

    @Test
    void colorsFromPackedArgb() {
        assertEquals(0xFF0000, Format.rgb(0xFFFF0000));
        assertEquals("#00FF7F", Format.colorHex(0x8000FF7F));
        assertTrue(Format.isPackedColor(0xFFFF0000));
        assertTrue(Format.isPackedColor(0xFF0000));
        assertFalse(Format.isPackedColor(0), "0 is a random colour");
        assertFalse(Format.isPackedColor(3), "a small value is a colour index");
    }

    @Test
    void namesAreShortenedOnlyForKnownPrefixes() {
        assertEquals("Crusader", Format.shortName("AirF_AmericaTankCrusader"));
        assertEquals("Redguard", Format.shortName("ChinaInfantryRedguard"));
        assertEquals("Terrorist", Format.shortName("GLAInfantryTerrorist"));
        assertEquals("Redguard", Format.shortName("Boss_InfantryRedguard"));
        assertEquals("SomethingElse", Format.shortName("SomethingElse"));
        assertEquals("Americana", Format.shortName("Americana"));
        assertEquals("Tank", Format.shortName("Tank"));
        assertEquals("NeutronMissile", Format.shortPowerName("SuperweaponNeutronMissile"));
        assertEquals("Anything", Format.shortPowerName("Anything"));
        assertEquals("AdvancedTraining (upgrade)", Format.shortUpgradeName("Upgrade_AmericaAdvancedTraining"));
        assertEquals("alpine assault", Format.mapDisplayName("maps/alpine assault/alpine assault.map"));
        assertEquals("tournament desert", Format.mapDisplayName("maps\\tournament desert"));
        assertEquals("", Format.mapDisplayName(""));
        assertEquals("OK", Format.strip("X_OK", "X_"));
        assertEquals("?", Format.enumName(null, "X_"));
    }

    @Test
    void causeUnwrapsToTheRealMessage() {
        assertEquals("The pipe has been ended", Format.cause(new IOException("read failed", new java.util.concurrent.ExecutionException(new IOException("The pipe has been ended")))));
        assertEquals("IllegalStateException", Format.cause(new IllegalStateException()));
        assertEquals("", Format.cause(null));
    }

    @Test
    void eventDetailDecodesTheStateChanges() {
        java.util.function.IntFunction<String> names = i -> i == 3 ? "Bob" : "P" + i;
        assertEquals("ACTIVE -> UNPOWERED", Format.eventDetail(event(StatEventKind.STATEVENT_RADAR_CHANGED, 2, 1, ""), names, 30));
        assertEquals("NONE -> BOMBARDMENT", Format.eventDetail(event(StatEventKind.STATEVENT_BATTLE_PLAN_CHANGED, 0, 1, ""), names, 30));
        assertEquals("Regular -> Veteran", Format.eventDetail(event(StatEventKind.STATEVENT_VETERANCY_CHANGE, 0, 1, ""), names, 30));
        assertEquals("level 7", Format.veterancyName(7));
        assertEquals("veteran", Format.eventDetail(event(StatEventKind.STATEVENT_BUILD, 1, 0, ""), names, 30));
        assertEquals("", Format.eventDetail(event(StatEventKind.STATEVENT_BUILD, 0, 0, ""), names, 30));
        assertEquals("tier 2", Format.eventDetail(event(StatEventKind.STATEVENT_SALVAGE_UPGRADE, 2, 0, ""), names, 30));
        assertEquals("rank 3, 1 points left", Format.eventDetail(event(StatEventKind.STATEVENT_SCIENCE_PURCHASE, 3, 1, ""), names, 30));
        assertEquals("from Bob (tech)", Format.eventDetail(event(StatEventKind.STATEVENT_CAPTURE, 3, 0, "tech"), names, 30));
        assertEquals("from neutral", Format.eventDetail(event(StatEventKind.STATEVENT_CAPTURE, -1, 0, ""), names, 30));
        assertEquals("for 0:45", Format.eventDetail(event(StatEventKind.STATEVENT_POWER_SABOTAGED, 30 * 45, 0, ""), names, 30));
        assertEquals("count=3", Format.eventDetail(event(StatEventKind.STATEVENT_EVACUATE, 0, 0, "count=3"), names, 30));
    }

    @Test
    void eventLocationOnlyWithALocation() {
        assertEquals("12, 35", Format.eventLocation(StatEvent.newBuilder().setHasLocation(true).setX(12.2f).setY(34.7f).build()));
        assertEquals("", Format.eventLocation(StatEvent.newBuilder().setX(1).build()));
    }

    @Test
    void rotatedViewBoxIsSummarisedAsCentreSizeAndRotation() {
        // A 400 x 200 box turned 90 degrees counter-clockwise about its centre (1000, 500).
        Camera turned = Camera.newBuilder().setHasViewBox(true)
            .setViewTlX(900).setViewTlY(300).setViewTrX(900).setViewTrY(700)
            .setViewBrX(1100).setViewBrY(700).setViewBlX(1100).setViewBlY(300).build();
        Format.ViewBox box = Format.viewBox(turned);
        assertNotNull(box);
        assertEquals(1000.0, box.centerX(), 0.001);
        assertEquals(500.0, box.centerY(), 0.001);
        assertEquals(400.0, box.width(), 0.001);
        assertEquals(200.0, box.height(), 0.001);
        assertEquals(90.0, box.rotationDegrees(), 0.001);
        assertNull(Format.viewBox(Camera.getDefaultInstance()));
    }

    private static StatEvent event(StatEventKind kind, int value1, int value2, String variant) {
        return StatEvent.newBuilder().setKind(kind).setIntValue1(value1).setIntValue2(value2).setVariant(variant).build();
    }
}
