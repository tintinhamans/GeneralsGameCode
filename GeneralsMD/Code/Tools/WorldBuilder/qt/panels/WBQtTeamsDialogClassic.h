// WBQtTeamsDialogClassic.h -- the original Team Builder, shown while the "New design" setting is off.
#ifndef WB_QT_TEAMS_DIALOG_CLASSIC_H
#define WB_QT_TEAMS_DIALOG_CLASSIC_H

#include <QDialog>

class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace Ui { class WBQtTeamsDialogClassic; }	// generated from WBQtTeamsDialogClassic.ui

class WBQtTeamsDialogClassic : public QDialog
{
	Q_OBJECT
public:
	explicit WBQtTeamsDialogClassic(QWidget *parent = 0);
	virtual ~WBQtTeamsDialogClassic();

private slots:
	void onPlayerRowChanged(int row);
	void onTeamRowChanged();
	void onTeamDoubleClicked(QTreeWidgetItem *item, int column);
	void onNewTeam();
	void onEditTeam();
	void onUndo();
	void onRedo();
	void onSaveNow();
	void onCopyTeam();
	void onDeleteTeam();
	void onSelectTeamMembers();
	void onMoveUpTeam();
	void onMoveDownTeam();
	void onExportTeams();
	void onImportTeams();
	void onFixMissingUnits();

private:
	void refreshAll();
	void refreshPlayers();
	void refreshTeamsTable();
	void refreshMissingUnits();
	void refreshButtons();
	void runTeamSheet();

	Ui::WBQtTeamsDialogClassic *m_ui;	// owns the static widget tree (WBQtTeamsDialogClassic.ui)

	bool m_updating;
	bool m_hasMissingUnits;		// cached by refreshMissingUnits; gates the Fix Missing button
	QListWidget *m_players;
	QTreeWidget *m_teams;
	QPushButton *m_newButton;
	QPushButton *m_copyButton;
	QPushButton *m_deleteButton;
	QPushButton *m_moveUpButton;
	QPushButton *m_moveDownButton;
};

#endif // WB_QT_TEAMS_DIALOG_CLASSIC_H
