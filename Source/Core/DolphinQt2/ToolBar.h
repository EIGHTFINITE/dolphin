// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <QAction>
#include <QLineEdit>
#include <QToolBar>

class ToolBar final : public QToolBar
{
	Q_OBJECT

public:
	explicit ToolBar(QWidget* parent = nullptr);

public slots:
	void EmulationStarted();
	void EmulationPaused();
	void EmulationStopped();

signals:
	void OpenPressed();
	void PlayPressed();
	void PausePressed();
	void StopPressed();
	void FullScreenPressed();
	void ScreenShotPressed();

	void PathsPressed();
	void SettingsPressed();
private:
	void MakeActions();
	void UpdateIcons();

	QAction* m_open_action;
	QAction* m_play_action;
	QAction* m_pause_action;
	QAction* m_stop_action;
	QAction* m_fullscreen_action;
	QAction* m_screenshot_action;
	QAction* m_paths_action;
	QAction* m_config_action;
	QAction* m_controllers_action;
};
