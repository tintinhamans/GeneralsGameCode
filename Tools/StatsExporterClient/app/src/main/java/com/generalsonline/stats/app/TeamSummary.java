package com.generalsonline.stats.app;

import com.generalsonline.stats.PlayerView;

import javax.swing.BorderFactory;
import javax.swing.JLabel;
import javax.swing.JPanel;
import java.awt.BorderLayout;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/**
 * A card per team above the Players table: who is in it, and the totals that decide a game at a glance (army value with its
 * share of all armies, income, money, score). It shows nothing while there are no players.
 */
@SuppressWarnings("serial")
final class TeamSummary extends JPanel {
    private final JPanel flow = Ui.flow();
    private final List<TeamCard> cards = new ArrayList<>();
    private String signature = "";

    TeamSummary() {
        super(new BorderLayout());
        setOpaque(false);
        setBorder(BorderFactory.createEmptyBorder(0, 0, Ui.GAP, 0));
        add(flow, BorderLayout.CENTER);
        setVisible(false);
    }

    /** Takes the current players; cheap when only the numbers changed. */
    void refresh(List<PlayerView> players) {
        Map<Integer, List<PlayerView>> teams = new TreeMap<>();
        for (PlayerView player : players) {
            teams.computeIfAbsent(player.team(), k -> new ArrayList<>()).add(player);
        }
        StringBuilder key = new StringBuilder();
        for (Map.Entry<Integer, List<PlayerView>> team : teams.entrySet()) {
            key.append(team.getKey()).append(':');
            for (PlayerView p : team.getValue()) {
                key.append(p.name()).append(',').append(p.color()).append(';');
            }
        }
        if (!key.toString().equals(signature)) {
            signature = key.toString();
            cards.clear();
            flow.removeAll();
            for (Map.Entry<Integer, List<PlayerView>> team : teams.entrySet()) {
                TeamCard card = new TeamCard(team.getKey(), team.getValue());
                cards.add(card);
                flow.add(card);
            }
            flow.revalidate();
        }
        long all = 0;
        for (PlayerView p : players) {
            all += p.stats().getArmyValue();
        }
        for (TeamCard card : cards) {
            card.refresh(teams.get(card.team), all);
        }
        setVisible(!players.isEmpty());
    }

    private static final class TeamCard extends Ui.Surface {
        private final int team;
        private final Ui.Stat army = new Ui.Stat("Army value");
        private final Ui.Stat income = new Ui.Stat("Income / min");
        private final Ui.Stat money = new Ui.Stat("Money");
        private final Ui.Stat score = new Ui.Stat("Score");
        private final Bar bar = new Bar();

        TeamCard(int team, List<PlayerView> members) {
            super(Theme.RAISED, Theme.BORDER, 12);
            this.team = team;
            setLayout(new BorderLayout(0, 8));
            setBorder(BorderFactory.createEmptyBorder(10, 14, 12, 14));
            JLabel title = Ui.heading(team >= 0 ? "Team " + (team + 1) : "No team");
            StringBuilder names = new StringBuilder("<html><div style='width:250px'>");
            for (PlayerView p : members) {
                Color color = Theme.legible(Minimap.playerColor(p.color(), p.playerIndex()));
                names.append("<font color='").append(Theme.hex(color)).append("'>").append(PanelKit.escape(p.name())).append("</font>, ");
            }
            names.setLength(names.length() - 2);
            JLabel list = new Ui.WrapLabel();
            list.setText(names.append("</div></html>").toString());
            list.setFont(Ui.small());
            JPanel head = Ui.stack(2, title, list);
            JPanel numbers = Ui.line(army, income, money, score);
            add(head, BorderLayout.NORTH);
            add(numbers, BorderLayout.CENTER);
            add(bar, BorderLayout.SOUTH);
            width(310);
        }

        void refresh(List<PlayerView> members, long allArmies) {
            long armyValue = 0;
            long incomeValue = 0;
            long moneyValue = 0;
            long scoreValue = 0;
            for (PlayerView p : members) {
                armyValue += p.stats().getArmyValue();
                incomeValue += Integer.toUnsignedLong(p.stats().getMoneyPerMinute());
                moneyValue += Integer.toUnsignedLong(p.stats().getMoney());
                scoreValue += p.stats().getScore();
            }
            army.setValue(String.format(Locale.ROOT, "%,d", armyValue));
            income.setValue(String.format(Locale.ROOT, "%,d", incomeValue));
            money.setValue(String.format(Locale.ROOT, "%,d", moneyValue));
            score.setValue(String.format(Locale.ROOT, "%,d", scoreValue));
            bar.share = allArmies <= 0 ? 0 : armyValue / (double) allArmies;
            bar.repaint();
        }
    }

    /** The team's share of all armies as a thin bar. */
    private static final class Bar extends JPanel {
        private double share;

        Bar() {
            setOpaque(false);
            setPreferredSize(new Dimension(10, 6));
        }

        @Override
        protected void paintComponent(Graphics graphics) {
            Graphics2D g = (Graphics2D) graphics.create();
            try {
                g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
                g.setColor(Theme.mix(Theme.RAISED, Theme.TEXT, 0.08f));
                g.fillRoundRect(0, 0, getWidth(), getHeight(), 6, 6);
                g.setColor(Theme.ACCENT);
                g.fillRoundRect(0, 0, Math.max(share > 0 ? 6 : 0, (int) Math.round(getWidth() * share)), getHeight(), 6, 6);
            } finally {
                g.dispose();
            }
        }
    }
}
