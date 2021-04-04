// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Settings/GeneralPane.h"

#include <map>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include "Core/Config/MainSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/DolphinAnalytics.h"
#include "Core/PowerPC/PowerPC.h"

#include "DolphinQt/QtUtils/ModalMessageBox.h"
#include "DolphinQt/Settings.h"

#include "UICommon/AutoUpdate.h"
#ifdef USE_DISCORD_PRESENCE
#include "UICommon/DiscordPresence.h"
#endif

constexpr int AUTO_UPDATE_DISABLE_INDEX = 0;
constexpr int AUTO_UPDATE_STABLE_INDEX = 1;
constexpr int AUTO_UPDATE_BETA_INDEX = 2;
constexpr int AUTO_UPDATE_DEV_INDEX = 3;

constexpr const char* AUTO_UPDATE_DISABLE_STRING = "";
constexpr const char* AUTO_UPDATE_STABLE_STRING = "stable";
constexpr const char* AUTO_UPDATE_BETA_STRING = "beta";
constexpr const char* AUTO_UPDATE_DEV_STRING = "dev";

constexpr int FALLBACK_REGION_NTSCJ_INDEX = 0;
constexpr int FALLBACK_REGION_NTSCU_INDEX = 1;
constexpr int FALLBACK_REGION_PAL_INDEX = 2;
constexpr int FALLBACK_REGION_NTSCK_INDEX = 3;

GeneralPane::GeneralPane(QWidget* parent) : QWidget(parent)
{
  CreateLayout();
  LoadConfig();

  ConnectLayout();

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          &GeneralPane::OnEmulationStateChanged);

  OnEmulationStateChanged(Core::GetState());
}

void GeneralPane::CreateLayout()
{
  m_main_layout = new QVBoxLayout;
  // Create layout here
  CreateBasic();

  if (AutoUpdateChecker::SystemSupportsAutoUpdates())
    CreateAutoUpdate();

  CreateFallbackRegion();

#if defined(USE_ANALYTICS) && USE_ANALYTICS
  CreateAnalytics();
#endif

  m_main_layout->addStretch(1);
  setLayout(m_main_layout);
}

void GeneralPane::OnEmulationStateChanged(Core::State state)
{
  const bool running = state != Core::State::Uninitialized;

  m_checkbox_dualcore->setEnabled(!running);
  m_checkbox_cheats->setEnabled(!running);
  m_checkbox_override_region_settings->setEnabled(!running);
#ifdef USE_DISCORD_PRESENCE
  m_checkbox_discord_presence->setEnabled(!running);
#endif
  m_combobox_fallback_region->setEnabled(!running);
}

void GeneralPane::ConnectLayout()
{
  connect(m_checkbox_dualcore, &QCheckBox::toggled, this, &GeneralPane::OnSaveConfig);
  connect(m_checkbox_cheats, &QCheckBox::toggled, this, &GeneralPane::OnSaveConfig);
  connect(m_checkbox_override_region_settings, &QCheckBox::stateChanged, this,
          &GeneralPane::OnSaveConfig);
  connect(m_checkbox_auto_disc_change, &QCheckBox::toggled, this, &GeneralPane::OnSaveConfig);
#ifdef USE_DISCORD_PRESENCE
  connect(m_checkbox_discord_presence, &QCheckBox::toggled, this, &GeneralPane::OnSaveConfig);
#endif

  if (AutoUpdateChecker::SystemSupportsAutoUpdates())
  {
    connect(m_combobox_update_track, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GeneralPane::OnSaveConfig);
    connect(&Settings::Instance(), &Settings::AutoUpdateTrackChanged, this,
            &GeneralPane::LoadConfig);
  }

  // Advanced
  connect(m_combobox_speedlimit, qOverload<int>(&QComboBox::currentIndexChanged),
          [this]() { OnSaveConfig(); });

  connect(m_combobox_fallback_region, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &GeneralPane::OnSaveConfig);
  connect(&Settings::Instance(), &Settings::FallbackRegionChanged, this, &GeneralPane::LoadConfig);

#if defined(USE_ANALYTICS) && USE_ANALYTICS
  connect(&Settings::Instance(), &Settings::AnalyticsToggled, this, &GeneralPane::LoadConfig);
  connect(m_checkbox_enable_analytics, &QCheckBox::toggled, this, &GeneralPane::OnSaveConfig);
  connect(m_button_generate_new_identity, &QPushButton::clicked, this,
          &GeneralPane::GenerateNewIdentity);
#endif
}

void GeneralPane::CreateBasic()
{
  auto* basic_group = new QGroupBox(tr("Basic Settings"));
  auto* basic_group_layout = new QVBoxLayout;
  basic_group->setLayout(basic_group_layout);
  m_main_layout->addWidget(basic_group);

  m_checkbox_dualcore = new QCheckBox(tr("Enable Dual Core (speedup)"));
  basic_group_layout->addWidget(m_checkbox_dualcore);

  m_checkbox_cheats = new QCheckBox(tr("Enable Cheats"));
  basic_group_layout->addWidget(m_checkbox_cheats);

  m_checkbox_override_region_settings = new QCheckBox(tr("Allow Mismatched Region Settings"));
  basic_group_layout->addWidget(m_checkbox_override_region_settings);

  m_checkbox_auto_disc_change = new QCheckBox(tr("Change Discs Automatically"));
  basic_group_layout->addWidget(m_checkbox_auto_disc_change);

#ifdef USE_DISCORD_PRESENCE
  m_checkbox_discord_presence = new QCheckBox(tr("Show Current Game on Discord"));
  basic_group_layout->addWidget(m_checkbox_discord_presence);
#endif

  auto* speed_limit_layout = new QFormLayout;
  speed_limit_layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  speed_limit_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  basic_group_layout->addLayout(speed_limit_layout);

  m_combobox_speedlimit = new QComboBox();

  m_combobox_speedlimit->addItem(tr("Unlimited"));
  for (int i = 10; i <= 200; i += 10)  // from 10% to 200%
  {
    QString str;
    if (i != 100)
      str = QStringLiteral("%1%").arg(i);
    else
      str = tr("%1% (Normal Speed)").arg(i);

    m_combobox_speedlimit->addItem(str);
  }

  speed_limit_layout->addRow(tr("&Speed Limit:"), m_combobox_speedlimit);
}

void GeneralPane::CreateAutoUpdate()
{
  auto* auto_update_group = new QGroupBox(tr("Auto Update Settings"));
  auto* layout = new QFormLayout;
  auto_update_group->setLayout(layout);
  m_main_layout->addWidget(auto_update_group);

  layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  m_combobox_update_track = new QComboBox(this);

  layout->addRow(tr("&Auto Update:"), m_combobox_update_track);

  for (const QString& option : {tr("Don't Update"), tr("Stable (once a year)"),
                                tr("Beta (once a month)"), tr("Dev (multiple times a day)")})
    m_combobox_update_track->addItem(option);
}

void GeneralPane::CreateFallbackRegion()
{
  auto* fallback_region_group = new QGroupBox(tr("Fallback Region"));
  auto* layout = new QVBoxLayout;
  fallback_region_group->setLayout(layout);
  m_main_layout->addWidget(fallback_region_group);

  m_combobox_fallback_region = new QComboBox(this);

  auto* form_widget = new QWidget;
  auto* form_layout = new QFormLayout;
  form_widget->setLayout(form_layout);
  form_layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  form_layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form_layout->addRow(tr("Fallback Region:"), m_combobox_fallback_region);
  layout->addWidget(form_widget);

  auto* fallback_region_description =
      new QLabel(tr("Dolphin will use this for titles whose region cannot be determined "
                    "automatically."));
  fallback_region_description->setWordWrap(true);
  layout->addWidget(fallback_region_description);

  for (const QString& option : {tr("NTSC-J"), tr("NTSC-U"), tr("PAL"), tr("NTSC-K")})
    m_combobox_fallback_region->addItem(option);
}

#if defined(USE_ANALYTICS) && USE_ANALYTICS
void GeneralPane::CreateAnalytics()
{
  auto* analytics_group = new QGroupBox(tr("Usage Statistics Reporting Settings"));
  auto* analytics_group_layout = new QVBoxLayout;
  analytics_group->setLayout(analytics_group_layout);
  m_main_layout->addWidget(analytics_group);

  m_checkbox_enable_analytics = new QCheckBox(tr("Enable Usage Statistics Reporting"));
  m_button_generate_new_identity = new QPushButton(tr("Generate a New Statistics Identity"));
  analytics_group_layout->addWidget(m_checkbox_enable_analytics);
  analytics_group_layout->addWidget(m_button_generate_new_identity);
}
#endif

void GeneralPane::LoadConfig()
{
  if (AutoUpdateChecker::SystemSupportsAutoUpdates())
  {
    const auto track = Settings::Instance().GetAutoUpdateTrack().toStdString();

    if (track == AUTO_UPDATE_DISABLE_STRING)
      m_combobox_update_track->setCurrentIndex(AUTO_UPDATE_DISABLE_INDEX);
    else if (track == AUTO_UPDATE_STABLE_STRING)
      m_combobox_update_track->setCurrentIndex(AUTO_UPDATE_STABLE_INDEX);
    else if (track == AUTO_UPDATE_BETA_STRING)
      m_combobox_update_track->setCurrentIndex(AUTO_UPDATE_BETA_INDEX);
    else
      m_combobox_update_track->setCurrentIndex(AUTO_UPDATE_DEV_INDEX);
  }

#if defined(USE_ANALYTICS) && USE_ANALYTICS
  m_checkbox_enable_analytics->setChecked(Settings::Instance().IsAnalyticsEnabled());
#endif
  m_checkbox_dualcore->setChecked(SConfig::GetInstance().bCPUThread);
  m_checkbox_cheats->setChecked(Settings::Instance().GetCheatsEnabled());
  m_checkbox_override_region_settings->setChecked(SConfig::GetInstance().bOverrideRegionSettings);
  m_checkbox_auto_disc_change->setChecked(Config::Get(Config::MAIN_AUTO_DISC_CHANGE));
#ifdef USE_DISCORD_PRESENCE
  m_checkbox_discord_presence->setChecked(Config::Get(Config::MAIN_USE_DISCORD_PRESENCE));
#endif
  int selection = qRound(SConfig::GetInstance().m_EmulationSpeed * 10);
  if (selection < m_combobox_speedlimit->count())
    m_combobox_speedlimit->setCurrentIndex(selection);
  m_checkbox_dualcore->setChecked(SConfig::GetInstance().bCPUThread);

  const auto fallback = Settings::Instance().GetFallbackRegion();

  if (fallback == DiscIO::Region::NTSC_J)
    m_combobox_fallback_region->setCurrentIndex(FALLBACK_REGION_NTSCJ_INDEX);
  else if (fallback == DiscIO::Region::NTSC_U)
    m_combobox_fallback_region->setCurrentIndex(FALLBACK_REGION_NTSCU_INDEX);
  else if (fallback == DiscIO::Region::PAL)
    m_combobox_fallback_region->setCurrentIndex(FALLBACK_REGION_PAL_INDEX);
  else if (fallback == DiscIO::Region::NTSC_K)
    m_combobox_fallback_region->setCurrentIndex(FALLBACK_REGION_NTSCK_INDEX);
  else
    m_combobox_fallback_region->setCurrentIndex(FALLBACK_REGION_NTSCJ_INDEX);
}

static QString UpdateTrackFromIndex(int index)
{
  QString value;

  switch (index)
  {
  case AUTO_UPDATE_DISABLE_INDEX:
    value = QString::fromStdString(AUTO_UPDATE_DISABLE_STRING);
    break;
  case AUTO_UPDATE_STABLE_INDEX:
    value = QString::fromStdString(AUTO_UPDATE_STABLE_STRING);
    break;
  case AUTO_UPDATE_BETA_INDEX:
    value = QString::fromStdString(AUTO_UPDATE_BETA_STRING);
    break;
  case AUTO_UPDATE_DEV_INDEX:
    value = QString::fromStdString(AUTO_UPDATE_DEV_STRING);
    break;
  }

  return value;
}

static DiscIO::Region UpdateFallbackRegionFromIndex(int index)
{
  DiscIO::Region value = DiscIO::Region::Unknown;

  switch (index)
  {
  case FALLBACK_REGION_NTSCJ_INDEX:
    value = DiscIO::Region::NTSC_J;
    break;
  case FALLBACK_REGION_NTSCU_INDEX:
    value = DiscIO::Region::NTSC_U;
    break;
  case FALLBACK_REGION_PAL_INDEX:
    value = DiscIO::Region::PAL;
    break;
  case FALLBACK_REGION_NTSCK_INDEX:
    value = DiscIO::Region::NTSC_K;
    break;
  default:
    value = DiscIO::Region::NTSC_J;
  }

  return value;
}

void GeneralPane::OnSaveConfig()
{
  Config::ConfigChangeCallbackGuard config_guard;

  auto& settings = SConfig::GetInstance();
  if (AutoUpdateChecker::SystemSupportsAutoUpdates())
  {
    Settings::Instance().SetAutoUpdateTrack(
        UpdateTrackFromIndex(m_combobox_update_track->currentIndex()));
  }

#ifdef USE_DISCORD_PRESENCE
  Discord::SetDiscordPresenceEnabled(m_checkbox_discord_presence->isChecked());
#endif

#if defined(USE_ANALYTICS) && USE_ANALYTICS
  Settings::Instance().SetAnalyticsEnabled(m_checkbox_enable_analytics->isChecked());
  DolphinAnalytics::Instance().ReloadConfig();
#endif
  settings.bCPUThread = m_checkbox_dualcore->isChecked();
  Config::SetBaseOrCurrent(Config::MAIN_CPU_THREAD, m_checkbox_dualcore->isChecked());
  Settings::Instance().SetCheatsEnabled(m_checkbox_cheats->isChecked());
  settings.bOverrideRegionSettings = m_checkbox_override_region_settings->isChecked();
  Config::SetBaseOrCurrent(Config::MAIN_OVERRIDE_REGION_SETTINGS,
                           m_checkbox_override_region_settings->isChecked());
  Config::SetBase(Config::MAIN_AUTO_DISC_CHANGE, m_checkbox_auto_disc_change->isChecked());
  Config::SetBaseOrCurrent(Config::MAIN_ENABLE_CHEATS, m_checkbox_cheats->isChecked());
  settings.m_EmulationSpeed = m_combobox_speedlimit->currentIndex() * 0.1f;
  Settings::Instance().SetFallbackRegion(
      UpdateFallbackRegionFromIndex(m_combobox_fallback_region->currentIndex()));

  settings.SaveSettings();
}

#if defined(USE_ANALYTICS) && USE_ANALYTICS
void GeneralPane::GenerateNewIdentity()
{
  DolphinAnalytics::Instance().GenerateNewIdentity();
  DolphinAnalytics::Instance().ReloadConfig();
  ModalMessageBox message_box(this);
  message_box.setIcon(QMessageBox::Information);
  message_box.setWindowTitle(tr("Identity Generation"));
  message_box.setText(tr("New identity generated."));
  message_box.exec();
}
#endif
