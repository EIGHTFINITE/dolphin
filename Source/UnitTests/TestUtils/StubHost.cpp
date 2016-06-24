// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// Stub implementation of the Host_* callbacks for tests. These implementations
// do nothing except return default values when required.

#include <memory>
#include <string>

#include "Common/GL/GLInterfaceBase.h"
#include "Core/Host.h"

void Host_NotifyMapLoaded() {}
void Host_RefreshDSPDebuggerWindow() {}
void Host_Message(int) {}
void* Host_GetRenderHandle() { return nullptr; }
void Host_UpdateTitle(const std::string&) {}
void Host_UpdateDisasmDialog() {}
void Host_UpdateMainFrame() {}
void Host_RequestRenderWindowSize(int, int) {}
void Host_RequestFullscreen(bool) {}
void Host_SetStartupDebuggingParameters() {}
bool Host_UIHasFocus() { return false; }
bool Host_RendererHasFocus() { return false; }
bool Host_RendererIsFullscreen() { return false; }
void Host_ConnectWiimote(int, bool) {}
void Host_SetWiiMoteConnectionState(int) {}
void Host_ShowVideoConfig(void*, const std::string&, const std::string&) {}
std::unique_ptr<cInterfaceBase> HostGL_CreateGLInterface() { return nullptr; }
