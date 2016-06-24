// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <QDesktopServices>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QUrl>

#include "DolphinQt2/Settings.h"
#include "DolphinQt2/GameList/GameList.h"
#include "DolphinQt2/GameList/ListProxyModel.h"
#include "DolphinQt2/GameList/TableDelegate.h"

GameList::GameList(QWidget* parent): QStackedWidget(parent)
{
	m_model = new GameListModel(this);
	m_table_proxy = new QSortFilterProxyModel(this);
	m_table_proxy->setSourceModel(m_model);
	m_list_proxy = new ListProxyModel(this);
	m_list_proxy->setSourceModel(m_model);

	m_delegate = new TableDelegate(this);

	MakeTableView();
	MakeListView();
	MakeEmptyView();

	connect(m_table, &QTableView::doubleClicked, this, &GameList::GameSelected);
	connect(m_list, &QListView::doubleClicked, this, &GameList::GameSelected);
	connect(this, &GameList::DirectoryAdded, m_model, &GameListModel::DirectoryAdded);
	connect(this, &GameList::DirectoryRemoved, m_model, &GameListModel::DirectoryRemoved);
	connect(m_model, &QAbstractItemModel::rowsInserted, this, &GameList::ConsiderViewChange);
	connect(m_model, &QAbstractItemModel::rowsRemoved, this, &GameList::ConsiderViewChange);

	addWidget(m_table);
	addWidget(m_list);
	addWidget(m_empty);
	m_prefer_table = Settings().GetPreferredView();
	ConsiderViewChange();
}

void GameList::MakeTableView()
{
	m_table = new QTableView(this);
	m_table->setModel(m_table_proxy);
	m_table->setItemDelegate(m_delegate);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setAlternatingRowColors(true);
	m_table->setShowGrid(false);
	m_table->setSortingEnabled(true);
	m_table->setCurrentIndex(QModelIndex());
	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_table, &QTableView::customContextMenuRequested, this, &GameList::ShowContextMenu);

	// TODO load from config
	m_table->setColumnHidden(GameListModel::COL_PLATFORM, false);
	m_table->setColumnHidden(GameListModel::COL_ID, true);
	m_table->setColumnHidden(GameListModel::COL_BANNER, false);
	m_table->setColumnHidden(GameListModel::COL_TITLE, false);
	m_table->setColumnHidden(GameListModel::COL_DESCRIPTION, true);
	m_table->setColumnHidden(GameListModel::COL_MAKER, false);
	m_table->setColumnHidden(GameListModel::COL_SIZE, false);
	m_table->setColumnHidden(GameListModel::COL_COUNTRY, false);
	m_table->setColumnHidden(GameListModel::COL_RATING, false);

	QHeaderView* hor_header = m_table->horizontalHeader();
	hor_header->setSectionResizeMode(GameListModel::COL_PLATFORM, QHeaderView::ResizeToContents);
	hor_header->setSectionResizeMode(GameListModel::COL_COUNTRY, QHeaderView::ResizeToContents);
	hor_header->setSectionResizeMode(GameListModel::COL_ID, QHeaderView::ResizeToContents);
	hor_header->setSectionResizeMode(GameListModel::COL_BANNER, QHeaderView::ResizeToContents);
	hor_header->setSectionResizeMode(GameListModel::COL_TITLE, QHeaderView::Stretch);
	hor_header->setSectionResizeMode(GameListModel::COL_MAKER, QHeaderView::Stretch);
	hor_header->setSectionResizeMode(GameListModel::COL_SIZE, QHeaderView::ResizeToContents);
	hor_header->setSectionResizeMode(GameListModel::COL_DESCRIPTION, QHeaderView::Stretch);
	hor_header->setSectionResizeMode(GameListModel::COL_RATING, QHeaderView::ResizeToContents);

	QHeaderView* ver_header = m_table->verticalHeader();
	ver_header->setSectionResizeMode(QHeaderView::ResizeToContents);
}

void GameList::MakeEmptyView()
{
	m_empty = new QLabel(this);
	m_empty->setText(tr("Dolphin did not find any game files.\n"
	                    "Open the Paths dialog to add game folders."));
	m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
}

void GameList::MakeListView()
{
	m_list = new QListView(this);
	m_list->setModel(m_list_proxy);
	m_list->setViewMode(QListView::IconMode);
	m_list->setResizeMode(QListView::Adjust);
	m_list->setUniformItemSizes(true);
	m_list->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_list, &QTableView::customContextMenuRequested, this, &GameList::ShowContextMenu);
}

void GameList::ShowContextMenu(const QPoint&)
{
	QMenu* menu = new QMenu(this);
	menu->addAction(tr("Properties"));
	menu->addAction(tr("Open Wiki Page"), this, SLOT(OpenWiki()));
	menu->addAction(tr("Set as Default ISO"), this, SLOT(SetDefaultISO()));
	menu->exec(QCursor::pos());
}

void GameList::OpenWiki()
{
	QString game_id = GameFile(GetSelectedGame()).GetUniqueID();
	QString url = QStringLiteral("https://wiki.dolphin-emu.org/index.php?title=").append(game_id);
	QDesktopServices::openUrl(QUrl(url));
}

void GameList::SetDefaultISO()
{
	Settings().SetDefaultGame(GetSelectedGame());
}

QString GameList::GetSelectedGame() const
{
	QAbstractItemView* view;
	QSortFilterProxyModel* proxy;
	if (currentWidget() == m_table)
	{
		view = m_table;
		proxy = m_table_proxy;
	}
	else
	{
		view = m_list;
		proxy = m_list_proxy;
	}
	QItemSelectionModel* sel_model = view->selectionModel();
	if (sel_model->hasSelection())
	{
		QModelIndex model_index = proxy->mapToSource(sel_model->selectedIndexes()[0]);
		return m_model->GetPath(model_index.row());
	}
	return QStringLiteral("");
}

void GameList::SetPreferredView(bool table)
{
	m_prefer_table = table;
	Settings().SetPreferredView(table);
	ConsiderViewChange();
}

void GameList::ConsiderViewChange()
{
	if (m_model->rowCount(QModelIndex()) > 0)
	{
		if (m_prefer_table)
			setCurrentWidget(m_table);
		else
			setCurrentWidget(m_list);
	}
	else
	{
		setCurrentWidget(m_empty);
	}
}
void GameList::keyReleaseEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Return)
		emit GameSelected();
	else
		QStackedWidget::keyReleaseEvent(event);
}
