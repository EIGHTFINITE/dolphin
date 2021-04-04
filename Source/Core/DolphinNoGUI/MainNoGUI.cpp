// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinNoGUI/Platform.h"

#include <OptionParser.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <string>
#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#endif

#include "Common/StringUtil.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Core.h"
#include "Core/DolphinAnalytics.h"
#include "Core/Host.h"

#include "UICommon/CommandLineParse.h"
#ifdef USE_DISCORD_PRESENCE
#include "UICommon/DiscordPresence.h"
#endif
#include "UICommon/UICommon.h"

#include "VideoCommon/RenderBase.h"
#include "VideoCommon/VideoBackendBase.h"

static std::unique_ptr<Platform> s_platform;

static void signal_handler(int)
{
  const char message[] = "A signal was received. A second signal will force Dolphin to stop.\n";
#ifdef _WIN32
  puts(message);
#else
  if (write(STDERR_FILENO, message, sizeof(message)) < 0)
  {
  }
#endif

  s_platform->RequestShutdown();
}

void Host_NotifyMapLoaded()
{
}
void Host_RefreshDSPDebuggerWindow()
{
}

bool Host_UIBlocksControllerState()
{
  return false;
}

static Common::Event s_update_main_frame_event;
void Host_Message(HostMessageID id)
{
  if (id == HostMessageID::WMUserStop)
    s_platform->Stop();
}

void Host_UpdateTitle(const std::string& title)
{
  s_platform->SetTitle(title);
}

void Host_UpdateDisasmDialog()
{
}

void Host_UpdateMainFrame()
{
  s_update_main_frame_event.Set();
}

void Host_RequestRenderWindowSize(int width, int height)
{
}

bool Host_RendererHasFocus()
{
  return s_platform->IsWindowFocused();
}

bool Host_RendererIsFullscreen()
{
  return s_platform->IsWindowFullscreen();
}

void Host_YieldToUI()
{
}

void Host_TitleChanged()
{
#ifdef USE_DISCORD_PRESENCE
  Discord::UpdateDiscordPresence();
#endif
}

static std::unique_ptr<Platform> GetPlatform(const optparse::Values& options)
{
  std::string platform_name = static_cast<const char*>(options.get("platform"));

#if HAVE_X11
  if (platform_name == "x11" || platform_name.empty())
    return Platform::CreateX11Platform();
#endif

#ifdef __linux__
  if (platform_name == "fbdev" || platform_name.empty())
    return Platform::CreateFBDevPlatform();
#endif

#ifdef _WIN32
  if (platform_name == "win32" || platform_name.empty())
    return Platform::CreateWin32Platform();
#endif

  if (platform_name == "headless" || platform_name.empty())
    return Platform::CreateHeadlessPlatform();

  return nullptr;
}

int main(int argc, char* argv[])
{
  auto parser = CommandLineParse::CreateParser(CommandLineParse::ParserOptions::OmitGUIOptions);
  parser->add_option("-p", "--platform")
      .action("store")
      .help("Window platform to use [%choices]")
      .choices({
        "headless"
#ifdef __linux__
            ,
            "fbdev"
#endif
#if HAVE_X11
            ,
            "x11"
#endif
#ifdef _WIN32
            ,
            "win32"
#endif
      });

  optparse::Values& options = CommandLineParse::ParseArguments(parser.get(), argc, argv);
  std::vector<std::string> args = parser->args();

  std::optional<std::string> save_state_path;
  if (options.is_set("save_state"))
  {
    save_state_path = static_cast<const char*>(options.get("save_state"));
  }

  std::unique_ptr<BootParameters> boot;
  bool game_specified = false;
  if (options.is_set("exec"))
  {
    const std::list<std::string> paths_list = options.all("exec");
    const std::vector<std::string> paths{std::make_move_iterator(std::begin(paths_list)),
                                         std::make_move_iterator(std::end(paths_list))};
    boot = BootParameters::GenerateFromFile(paths, save_state_path);
    game_specified = true;
  }
  else if (options.is_set("nand_title"))
  {
    const std::string hex_string = static_cast<const char*>(options.get("nand_title"));
    if (hex_string.length() != 16)
    {
      fprintf(stderr, "Invalid title ID\n");
      parser->print_help();
      return 1;
    }
    const u64 title_id = std::stoull(hex_string, nullptr, 16);
    boot = std::make_unique<BootParameters>(BootParameters::NANDTitle{title_id});
  }
  else if (args.size())
  {
    boot = BootParameters::GenerateFromFile(args.front(), save_state_path);
    args.erase(args.begin());
    game_specified = true;
  }
  else
  {
    parser->print_help();
    return 0;
  }

  std::string user_directory;
  if (options.is_set("user"))
    user_directory = static_cast<const char*>(options.get("user"));

  UICommon::SetUserDirectory(user_directory);
  UICommon::Init();

  s_platform = GetPlatform(options);
  if (!s_platform || !s_platform->Init())
  {
    fprintf(stderr, "No platform found, or failed to initialize.\n");
    return 1;
  }

  if (save_state_path && !game_specified)
  {
    fprintf(stderr, "A save state cannot be loaded without specifying a game to launch.\n");
    return 1;
  }

  Core::SetOnStateChangedCallback([](Core::State state) {
    if (state == Core::State::Uninitialized)
      s_platform->Stop();
  });

#ifdef _WIN32
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
#else
  // Shut down cleanly on SIGINT and SIGTERM
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESETHAND;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#endif

  DolphinAnalytics::Instance().ReportDolphinStart("nogui");

  if (!BootManager::BootCore(std::move(boot), s_platform->GetWindowSystemInfo()))
  {
    fprintf(stderr, "Could not boot the specified file\n");
    return 1;
  }

#ifdef USE_DISCORD_PRESENCE
  Discord::UpdateDiscordPresence();
#endif

  s_platform->MainLoop();
  Core::Stop();

  Core::Shutdown();
  s_platform.reset();
  UICommon::Shutdown();

  return 0;
}
