package com.generalsonline.stats;

import com.generalsonline.stats.proto.LobbyControlQuery;
import com.generalsonline.stats.proto.LobbyKick;
import com.generalsonline.stats.proto.LobbyPress;
import com.generalsonline.stats.proto.LobbySlotState;

import java.util.Objects;

/**
 * Builds the lobby control actions: what a user does on the game setup screen (skirmish, LAN or Generals Online),
 * as a {@link LobbyControlQuery} for {@link StatsPipe#lobbyControl(LobbyControlQuery)}.
 *
 * <pre>{@code
 * pipe.lobbyControl(LobbyControl.addAi(3, LobbyControl.AiLevel.MEDIUM));
 * pipe.lobbyControl(LobbyControl.faction(3, "China"));
 * pipe.lobbyControl(LobbyControl.startPosition(3, 1));
 * pipe.lobbyControl(LobbyControl.start());
 * }</pre>
 *
 * <p>The game drives the setup screen's own controls, so the screen's rules apply: only the host changes the map, the
 * cash and the slots; a client changes its own slot and accepts; a control that is locked or hidden is not touched. What
 * the screen would refuse comes back as a {@link NotAllowedException} with the reason. A slot is the 0-based lobby slot
 * of {@code LobbyState}; colors are packed {@code 0xAARRGGBB} like {@code MatchPlayer.color}.
 */
public final class LobbyControl {
    /** The number of slots of a lobby. */
    public static final int MAX_SLOTS = 8;

    /** The computer opponent levels the setup screen offers. */
    public enum AiLevel {
        /** The screen's Easy AI. */
        EASY(LobbySlotState.LOBBY_SLOT_AI_EASY),
        /** The screen's Medium AI. */
        MEDIUM(LobbySlotState.LOBBY_SLOT_AI_MEDIUM),
        /** The screen's Hard AI, which the game calls brutal. */
        BRUTAL(LobbySlotState.LOBBY_SLOT_AI_BRUTAL);

        private final LobbySlotState state;

        AiLevel(LobbySlotState state) {
            this.state = state;
        }

        LobbySlotState state() {
            return state;
        }
    }

    private LobbyControl() {
    }

    private static LobbyControlQuery.Builder slot(int slot) {
        if (slot < 0 || slot >= MAX_SLOTS) {
            throw new IllegalArgumentException("a lobby slot is 0 to " + (MAX_SLOTS - 1));
        }
        return LobbyControlQuery.newBuilder().setLobbySlot(slot);
    }

    /**
     * Opens an AI or empty slot (host only).
     *
     * @param slot the slot
     * @return the action
     */
    public static LobbyControlQuery openSlot(int slot) {
        return slot(slot).setSetSlotState(LobbySlotState.LOBBY_SLOT_OPEN).build();
    }

    /**
     * Closes an AI or empty slot (host only).
     *
     * @param slot the slot
     * @return the action
     */
    public static LobbyControlQuery closeSlot(int slot) {
        return slot(slot).setSetSlotState(LobbySlotState.LOBBY_SLOT_CLOSED).build();
    }

    /**
     * Puts a computer opponent into a slot (host only).
     *
     * @param slot the slot
     * @param level the AI level
     * @return the action
     */
    public static LobbyControlQuery addAi(int slot, AiLevel level) {
        return slot(slot).setSetSlotState(Objects.requireNonNull(level, "level").state()).build();
    }

    /**
     * Removes the human in a slot, the way the host's slot dropdown does (host only).
     *
     * @param slot the slot
     * @param closeSlot true to leave the slot closed instead of open
     * @return the action
     */
    public static LobbyControlQuery kick(int slot, boolean closeSlot) {
        return slot(slot).setKick(LobbyKick.newBuilder().setCloseSlot(closeSlot)).build();
    }

    /**
     * Sets a slot's side, as the faction dropdown lists it.
     *
     * @param slot the slot (your own, or an AI slot as the host)
     * @param side a side as {@code LobbyState} names it, such as {@code "America"}, or {@code "Random"} or {@code "Observer"}
     * @return the action
     */
    public static LobbyControlQuery faction(int slot, String side) {
        return slot(slot).setFaction(Objects.requireNonNull(side, "side")).build();
    }

    /**
     * Sets a slot's color.
     *
     * @param slot the slot (your own, or an AI slot as the host)
     * @param argb a lobby color packed as {@code 0xAARRGGBB}, or 0 for Random; a color another slot holds is refused
     * @return the action
     */
    public static LobbyControlQuery color(int slot, int argb) {
        return slot(slot).setColor(argb).build();
    }

    /**
     * Sets a slot's team.
     *
     * @param slot the slot (your own, or an AI slot as the host)
     * @param team the 0-based team, or -1 for none
     * @return the action
     */
    public static LobbyControlQuery team(int slot, int team) {
        return slot(slot).setTeam(team).build();
    }

    /**
     * Puts a slot on a start position of the map, or takes it off one.
     *
     * @param slot the slot (your own, or an AI slot as the host)
     * @param position the 0-based start position, or -1 to clear
     * @return the action
     */
    public static LobbyControlQuery startPosition(int slot, int position) {
        return slot(slot).setStartPosition(position).build();
    }

    /**
     * Changes the map (host only).
     *
     * @param mapName a multiplayer map as {@code LobbyState.map_name} gives it
     * @return the action
     */
    public static LobbyControlQuery map(String mapName) {
        return LobbyControlQuery.newBuilder().setMapName(Objects.requireNonNull(mapName, "mapName")).build();
    }

    /**
     * Sets the starting cash (host only).
     *
     * @param cash one of the amounts the setup screen offers
     * @return the action
     */
    public static LobbyControlQuery startingCash(int cash) {
        if (cash < 0) {
            throw new IllegalArgumentException("the cash is not negative");
        }
        return LobbyControlQuery.newBuilder().setStartingCash(cash).build();
    }

    /**
     * Switches the superweapon limit on or off (host only).
     *
     * @param limited true for limited superweapons
     * @return the action
     */
    public static LobbyControlQuery limitSuperweapons(boolean limited) {
        return LobbyControlQuery.newBuilder().setLimitSuperweapons(limited).build();
    }

    /**
     * Sets the skirmish speed slider (skirmish only).
     *
     * @param fps the frame rate limit, 15 to 61; 61 is no limit
     * @return the action
     */
    public static LobbyControlQuery gameSpeed(int fps) {
        if (fps < 15 || fps > 61) {
            throw new IllegalArgumentException("the game speed is 15 to 61");
        }
        return LobbyControlQuery.newBuilder().setGameSpeedFps(fps).build();
    }

    /**
     * Presses the Accept button (a client).
     *
     * @return the action
     */
    public static LobbyControlQuery accept() {
        return LobbyControlQuery.newBuilder().setAccept(LobbyPress.getDefaultInstance()).build();
    }

    /**
     * Presses the Start button (the host). With players that have not accepted yet, the game only announces the intent to start.
     *
     * @return the action
     */
    public static LobbyControlQuery start() {
        return LobbyControlQuery.newBuilder().setStart(LobbyPress.getDefaultInstance()).build();
    }
}
