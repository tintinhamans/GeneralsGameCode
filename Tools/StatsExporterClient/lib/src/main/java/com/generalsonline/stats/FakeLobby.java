package com.generalsonline.stats;

import com.generalsonline.stats.proto.LobbyChangeRoute;
import com.generalsonline.stats.proto.LobbyControlQuery;
import com.generalsonline.stats.proto.LobbyControlResult;
import com.generalsonline.stats.proto.LobbyKind;
import com.generalsonline.stats.proto.LobbySlot;
import com.generalsonline.stats.proto.LobbySlotState;
import com.generalsonline.stats.proto.LobbyState;
import com.generalsonline.stats.proto.MatchPlayer;
import com.generalsonline.stats.proto.ResponseStatus;

import java.util.Locale;

/**
 * The lobby model of {@link FakeGame}: applies a lobby control action to a {@link LobbyState} by the rules of the game's
 * setup screens (the host changes slots, map and rules; a client changes only its own slot and accepts). Pure: it takes
 * a state and returns the new one, or a refusal.
 */
final class FakeLobby {
    /** The amounts of the starting cash dropdown. */
    static final int[] STARTING_CASH = {5000, 10000, 20000, 50000, 100000};

    private static final int SLOTS = LobbyControl.MAX_SLOTS;
    private static final String[] SIDES = {"America", "China", "GLA"};
    private static final int[] COLORS = {0xFF0000FF, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00, 0xFFFF8000, 0xFF00FFFF, 0xFFFF00FF, 0xFF808080};

    /** A refused action. */
    record Refusal(ResponseStatus status, String message) {
    }

    /** The outcome: a refusal, or the new lobby and the result for the response. */
    record Outcome(Refusal refusal, LobbyState state, LobbyControlResult result) {
        static Outcome refused(ResponseStatus status, String message) {
            return new Outcome(new Refusal(status, message), null, null);
        }
    }

    private FakeLobby() {
    }

    private static Outcome notAllowed(String message) {
        return Outcome.refused(ResponseStatus.RESPONSE_STATUS_NOT_ALLOWED, message);
    }

    private static Outcome badRequest(String message) {
        return Outcome.refused(ResponseStatus.RESPONSE_STATUS_BAD_REQUEST, message);
    }

    private static int hostSlot(LobbyState.Builder b) {
        for (int i = 0; i < b.getSlotsCount(); i++) {
            if (b.getSlots(i).getIsHost()) {
                return i;
            }
        }
        return 0;
    }

    private static boolean isAi(LobbySlot s) {
        return s.getState() == LobbySlotState.LOBBY_SLOT_AI_EASY || s.getState() == LobbySlotState.LOBBY_SLOT_AI_MEDIUM || s.getState() == LobbySlotState.LOBBY_SLOT_AI_BRUTAL;
    }

    private static boolean isHuman(LobbySlot s) {
        return s.getState() == LobbySlotState.LOBBY_SLOT_HUMAN;
    }

    private static boolean isObserver(LobbySlot s) {
        return s.getPlayer().getIsObserver();
    }

    private static LobbyChangeRoute route(LobbyKind kind, boolean host) {
        return switch (kind) {
            case LOBBY_KIND_SKIRMISH -> LobbyChangeRoute.LOBBY_CHANGE_APPLIED;
            case LOBBY_KIND_LAN -> host ? LobbyChangeRoute.LOBBY_CHANGE_APPLIED : LobbyChangeRoute.LOBBY_CHANGE_REQUESTED_FROM_HOST;
            default -> LobbyChangeRoute.LOBBY_CHANGE_REQUESTED_FROM_SERVICE;
        };
    }

    /**
     * Applies one action.
     *
     * @param current the lobby now
     * @param query the action
     * @param localSlot the slot the local user holds, or -1 when the local user is the host
     * @return the outcome
     */
    static Outcome apply(LobbyState current, LobbyControlQuery query, int localSlot) {
        LobbyState.Builder b = current.toBuilder();
        while (b.getSlotsCount() < SLOTS) {
            b.addSlots(LobbySlot.getDefaultInstance());
        }
        int host = hostSlot(b);
        int local = localSlot < 0 ? host : localSlot;
        boolean isHost = local == host;
        int slot = query.getLobbySlot();
        String note = "";

        boolean slotAction = query.getActionCase().getNumber() >= LobbyControlQuery.ActionCase.SET_SLOT_STATE.getNumber()
            && query.getActionCase().getNumber() <= LobbyControlQuery.ActionCase.START_POSITION.getNumber();
        if (slotAction && (slot < 0 || slot >= SLOTS)) {
            return badRequest("lobby_slot must be 0 to 7");
        }
        LobbySlot target = slotAction ? b.getSlots(slot) : null;
        boolean own = slotAction && slot == local;
        boolean editable = slotAction && (own || (isHost && isAi(target)));

        switch (query.getActionCase()) {
            case SET_SLOT_STATE -> {
                LobbySlotState state = query.getSetSlotState();
                if (state == LobbySlotState.LOBBY_SLOT_HUMAN) {
                    return badRequest("state must be OPEN, CLOSED or an AI level");
                }
                if (!isHost) {
                    return notAllowed("only the host changes what a slot holds");
                }
                if (own) {
                    return notAllowed("the host's own slot cannot be changed");
                }
                if (isHuman(target)) {
                    return notAllowed("a human holds that slot: use kick");
                }
                setState(b, slot, state);
            }
            case KICK -> {
                if (!isHost) {
                    return notAllowed("only the host changes what a slot holds");
                }
                if (own) {
                    return notAllowed("the host's own slot cannot be changed");
                }
                if (!isHuman(target)) {
                    return notAllowed("no human holds that slot");
                }
                setState(b, slot, query.getKick().getCloseSlot() ? LobbySlotState.LOBBY_SLOT_CLOSED : LobbySlotState.LOBBY_SLOT_OPEN);
            }
            case FACTION -> {
                if (!editable) {
                    return notAllowed("that slot's faction dropdown is not available to you");
                }
                String side = query.getFaction();
                String known = null;
                if (side.equalsIgnoreCase("random")) {
                    known = "Random";
                } else if (side.equalsIgnoreCase("observer")) {
                    known = "Observer";
                } else {
                    for (String candidate : SIDES) {
                        if (candidate.equalsIgnoreCase(side)) {
                            known = candidate;
                        }
                    }
                }
                if (known == null) {
                    return Outcome.refused(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no such side");
                }
                MatchPlayer.Builder p = b.getSlotsBuilder(slot).getPlayerBuilder();
                p.setSide(known).setIsObserver(known.equals("Observer")).setIsSideRandom(known.equals("Random"));
                if (known.equals("Observer")) {
                    p.setColor(0).setIsColorRandom(true).setTeam(-1).setStartPosition(-1);
                    b.getSlotsBuilder(slot).setStartPosition(-1);
                }
            }
            case COLOR -> {
                if (!editable || isObserver(target)) {
                    return notAllowed("that slot's colour dropdown is not available to you");
                }
                int argb = query.getColor();
                if (argb != 0) {
                    boolean known = false;
                    for (int c : COLORS) {
                        known |= c == argb;
                    }
                    if (!known) {
                        return badRequest("not one of the lobby colours");
                    }
                    for (int i = 0; i < SLOTS; i++) {
                        if (i != slot && b.getSlots(i).getPlayer().getColor() == argb) {
                            return notAllowed("the colour is held by another slot");
                        }
                    }
                }
                b.getSlotsBuilder(slot).getPlayerBuilder().setColor(argb).setIsColorRandom(argb == 0);
            }
            case TEAM -> {
                int team = query.getTeam();
                if (team < -1 || team >= SLOTS / 2) {
                    return badRequest("team must be -1 to 3");
                }
                if (!editable || isObserver(target)) {
                    return notAllowed("that slot's team dropdown is not available to you");
                }
                b.getSlotsBuilder(slot).getPlayerBuilder().setTeam(team);
            }
            case START_POSITION -> {
                int position = query.getStartPosition();
                if (position < -1 || position >= SLOTS) {
                    return badRequest("start_position must be -1 to 7");
                }
                if (!editable) {
                    return notAllowed("only your own slot, or an AI slot as the host, can be placed");
                }
                if (isObserver(target)) {
                    return notAllowed("an observer has no start position");
                }
                for (int i = 0; position >= 0 && i < SLOTS; i++) {
                    if (i != slot && b.getSlots(i).getStartPosition() == position) {
                        return notAllowed("another slot holds that start position");
                    }
                }
                b.getSlotsBuilder(slot).setStartPosition(position);
                if (b.getSlots(slot).hasPlayer()) {
                    b.getSlotsBuilder(slot).getPlayerBuilder().setStartPosition(position);
                }
            }
            case MAP_NAME -> {
                if (!isHost) {
                    return notAllowed("only the host picks the map");
                }
                String map = query.getMapName();
                if (map.isEmpty()) {
                    return badRequest("map_name is empty");
                }
                if (!map.toLowerCase(Locale.ROOT).endsWith(".map")) {
                    return Outcome.refused(ResponseStatus.RESPONSE_STATUS_NOT_FOUND, "no such map in the map cache");
                }
                b.setMapName(map);
                for (int i = 0; i < SLOTS; i++) {
                    b.getSlotsBuilder(i).setStartPosition(-1).setIsReady(isHuman(b.getSlots(i)) && i == host);
                    if (b.getSlots(i).hasPlayer()) {
                        b.getSlotsBuilder(i).getPlayerBuilder().setStartPosition(-1);
                    }
                }
            }
            case STARTING_CASH -> {
                if (!isHost) {
                    return notAllowed("only the host changes the starting cash");
                }
                boolean offered = false;
                for (int cash : STARTING_CASH) {
                    offered |= cash == query.getStartingCash();
                }
                if (!offered) {
                    return badRequest("starting_cash must be one of: 5000 10000 20000 50000 100000");
                }
                if (b.getKind() == LobbyKind.LOBBY_KIND_ONLINE) {
                    b.getOnlineBuilder().setStartingCash(query.getStartingCash());
                }
            }
            case LIMIT_SUPERWEAPONS -> {
                if (!isHost) {
                    return notAllowed("only the host changes the superweapon limit");
                }
                if (b.getKind() == LobbyKind.LOBBY_KIND_ONLINE) {
                    b.getOnlineBuilder().setLimitSuperweapons(query.getLimitSuperweapons());
                }
            }
            case GAME_SPEED_FPS -> {
                if (b.getKind() != LobbyKind.LOBBY_KIND_SKIRMISH) {
                    return notAllowed("only the skirmish screen has a game speed slider");
                }
                if (query.getGameSpeedFps() < 15 || query.getGameSpeedFps() > 61) {
                    return badRequest("game_speed_fps must be 15 to 61");
                }
            }
            case ACCEPT -> {
                if (isHost) {
                    return notAllowed("the host has no accept button: use start");
                }
                if (b.getSlots(local).getIsReady()) {
                    return notAllowed("the Accept button is disabled right now (already accepted, or the lobby is locked)");
                }
                b.getSlotsBuilder(local).setIsReady(true);
            }
            case START -> {
                if (!isHost) {
                    return notAllowed("only the host starts the game: a client accepts");
                }
                if (b.getKind() != LobbyKind.LOBBY_KIND_SKIRMISH) {
                    note = "pressed Start: the game starts once every human has accepted, otherwise the players are only told the host wants to start";
                }
            }
            default -> {
                return badRequest("no action set");
            }
        }

        // Anything but a press or a rule change makes the humans accept again, as the screens reset the accepted flags.
        if (slotAction || query.getActionCase() == LobbyControlQuery.ActionCase.MAP_NAME) {
            for (int i = 0; i < SLOTS; i++) {
                if (i != host && isHuman(b.getSlots(i))) {
                    b.getSlotsBuilder(i).setIsReady(false);
                }
            }
        }
        LobbyControlResult result = LobbyControlResult.newBuilder().setKind(b.getKind()).setRoute(route(b.getKind(), isHost)).setNote(note).build();
        return new Outcome(null, b.build(), result);
    }

    private static void setState(LobbyState.Builder b, int slot, LobbySlotState state) {
        LobbySlot.Builder s = b.getSlotsBuilder(slot);
        s.setState(state).setIsReady(false).setStartPosition(-1);
        switch (state) {
            case LOBBY_SLOT_AI_EASY, LOBBY_SLOT_AI_MEDIUM, LOBBY_SLOT_AI_BRUTAL -> {
                String name = state == LobbySlotState.LOBBY_SLOT_AI_EASY ? "Easy AI" : state == LobbySlotState.LOBBY_SLOT_AI_MEDIUM ? "Medium AI" : "Hard AI";
                s.setPlayer(MatchPlayer.newBuilder().setLobbySlot(slot).setPlayerIndex(-1).setName(name).setSide("Random").setIsSideRandom(true)
                    .setIsColorRandom(true).setTeam(-1).setIsHuman(false).setStartPosition(-1));
            }
            default -> s.clearPlayer();
        }
    }
}
