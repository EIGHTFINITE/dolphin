// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "InputCommon/ControllerInterface/ControllerInterface.h"

#include <algorithm>

#include "Common/Logging/Log.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"

#ifdef CIFACE_USE_WIN32
#include "InputCommon/ControllerInterface/Win32/Win32.h"
#endif
#ifdef CIFACE_USE_XLIB
#include "InputCommon/ControllerInterface/Xlib/XInput2.h"
#endif
#ifdef CIFACE_USE_OSX
#include "InputCommon/ControllerInterface/OSX/OSX.h"
#include "InputCommon/ControllerInterface/Quartz/Quartz.h"
#endif
#ifdef CIFACE_USE_SDL
#include "InputCommon/ControllerInterface/SDL/SDL.h"
#endif
#ifdef CIFACE_USE_ANDROID
#include "InputCommon/ControllerInterface/Android/Android.h"
#endif
#ifdef CIFACE_USE_EVDEV
#include "InputCommon/ControllerInterface/evdev/evdev.h"
#endif
#ifdef CIFACE_USE_PIPES
#include "InputCommon/ControllerInterface/Pipes/Pipes.h"
#endif
#ifdef CIFACE_USE_DUALSHOCKUDPCLIENT
#include "InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPClient.h"
#endif

ControllerInterface g_controller_interface;

static thread_local ciface::InputChannel tls_input_channel = ciface::InputChannel::Host;

void ControllerInterface::Initialize(const WindowSystemInfo& wsi)
{
  if (m_is_init)
    return;

  m_wsi = wsi;

  // Allow backends to add devices as soon as they are initialized.
  m_is_init = true;

  m_is_populating_devices = true;

#ifdef CIFACE_USE_WIN32
  ciface::Win32::Init(wsi.render_window);
#endif
#ifdef CIFACE_USE_XLIB
// nothing needed
#endif
#ifdef CIFACE_USE_OSX
  if (m_wsi.type == WindowSystemType::MacOS)
    ciface::OSX::Init(wsi.render_window);
// nothing needed for Quartz
#endif
#ifdef CIFACE_USE_SDL
  ciface::SDL::Init();
#endif
#ifdef CIFACE_USE_ANDROID
// nothing needed
#endif
#ifdef CIFACE_USE_EVDEV
  ciface::evdev::Init();
#endif
#ifdef CIFACE_USE_PIPES
// nothing needed
#endif
#ifdef CIFACE_USE_DUALSHOCKUDPCLIENT
  ciface::DualShockUDPClient::Init();
#endif

  RefreshDevices();
}

void ControllerInterface::ChangeWindow(void* hwnd)
{
  if (!m_is_init)
    return;

  // This shouldn't use render_surface so no need to update it.
  m_wsi.render_window = hwnd;
  RefreshDevices();
}

void ControllerInterface::RefreshDevices()
{
  if (!m_is_init)
    return;

  {
    std::lock_guard lk(m_devices_mutex);
    m_devices.clear();
  }

  m_is_populating_devices = true;

  // Make sure shared_ptr<Device> objects are released before repopulating.
  InvokeDevicesChangedCallbacks();

#ifdef CIFACE_USE_WIN32
  ciface::Win32::PopulateDevices(m_wsi.render_window);
#endif
#ifdef CIFACE_USE_XLIB
  if (m_wsi.type == WindowSystemType::X11)
    ciface::XInput2::PopulateDevices(m_wsi.render_window);
#endif
#ifdef CIFACE_USE_OSX
  if (m_wsi.type == WindowSystemType::MacOS)
  {
    ciface::OSX::PopulateDevices(m_wsi.render_window);
    ciface::Quartz::PopulateDevices(m_wsi.render_window);
  }
#endif
#ifdef CIFACE_USE_SDL
  ciface::SDL::PopulateDevices();
#endif
#ifdef CIFACE_USE_ANDROID
  ciface::Android::PopulateDevices();
#endif
#ifdef CIFACE_USE_EVDEV
  ciface::evdev::PopulateDevices();
#endif
#ifdef CIFACE_USE_PIPES
  ciface::Pipes::PopulateDevices();
#endif
#ifdef CIFACE_USE_DUALSHOCKUDPCLIENT
  ciface::DualShockUDPClient::PopulateDevices();
#endif

  WiimoteReal::ProcessWiimotePool();

  m_is_populating_devices = false;
  InvokeDevicesChangedCallbacks();
}

void ControllerInterface::PlatformPopulateDevices(std::function<void()> callback)
{
  if (!m_is_init)
    return;

  m_is_populating_devices = true;

  callback();

  m_is_populating_devices = false;
  InvokeDevicesChangedCallbacks();
}

// Remove all devices and call library cleanup functions
void ControllerInterface::Shutdown()
{
  if (!m_is_init)
    return;

  // Prevent additional devices from being added during shutdown.
  m_is_init = false;

  {
    std::lock_guard lk(m_devices_mutex);

    for (const auto& d : m_devices)
    {
      // Set outputs to ZERO before destroying device
      for (ciface::Core::Device::Output* o : d->Outputs())
        o->SetState(0);
    }

    m_devices.clear();
  }

  // This will update control references so shared_ptr<Device>s are freed up
  // BEFORE we shutdown the backends.
  InvokeDevicesChangedCallbacks();

#ifdef CIFACE_USE_WIN32
  ciface::Win32::DeInit();
#endif
#ifdef CIFACE_USE_XLIB
// nothing needed
#endif
#ifdef CIFACE_USE_OSX
  ciface::OSX::DeInit();
  ciface::Quartz::DeInit();
#endif
#ifdef CIFACE_USE_SDL
  ciface::SDL::DeInit();
#endif
#ifdef CIFACE_USE_ANDROID
// nothing needed
#endif
#ifdef CIFACE_USE_EVDEV
  ciface::evdev::Shutdown();
#endif
#ifdef CIFACE_USE_DUALSHOCKUDPCLIENT
  ciface::DualShockUDPClient::DeInit();
#endif
}

void ControllerInterface::AddDevice(std::shared_ptr<ciface::Core::Device> device)
{
  // If we are shutdown (or in process of shutting down) ignore this request:
  if (!m_is_init)
    return;

  {
    std::lock_guard lk(m_devices_mutex);

    const auto is_id_in_use = [&device, this](int id) {
      return std::any_of(m_devices.begin(), m_devices.end(), [&device, &id](const auto& d) {
        return d->GetSource() == device->GetSource() && d->GetName() == device->GetName() &&
               d->GetId() == id;
      });
    };

    const auto preferred_id = device->GetPreferredId();
    if (preferred_id.has_value() && !is_id_in_use(*preferred_id))
    {
      // Use the device's preferred ID if available.
      device->SetId(*preferred_id);
    }
    else
    {
      // Find the first available ID to use.
      int id = 0;
      while (is_id_in_use(id))
        ++id;

      device->SetId(id);
    }

    NOTICE_LOG_FMT(SERIALINTERFACE, "Added device: {}", device->GetQualifiedName());
    m_devices.emplace_back(std::move(device));
  }

  if (!m_is_populating_devices)
    InvokeDevicesChangedCallbacks();
}

void ControllerInterface::RemoveDevice(std::function<bool(const ciface::Core::Device*)> callback)
{
  {
    std::lock_guard lk(m_devices_mutex);
    auto it = std::remove_if(m_devices.begin(), m_devices.end(), [&callback](const auto& dev) {
      if (callback(dev.get()))
      {
        NOTICE_LOG_FMT(SERIALINTERFACE, "Removed device: {}", dev->GetQualifiedName());
        return true;
      }
      return false;
    });
    m_devices.erase(it, m_devices.end());
  }

  if (!m_is_populating_devices)
    InvokeDevicesChangedCallbacks();
}

// Update input for all devices if lock can be acquired without waiting.
void ControllerInterface::UpdateInput()
{
  // Don't block the UI or CPU thread (to avoid a short but noticeable frame drop)
  if (m_devices_mutex.try_lock())
  {
    std::lock_guard lk(m_devices_mutex, std::adopt_lock);
    for (const auto& d : m_devices)
      d->UpdateInput();
  }
}

void ControllerInterface::SetCurrentInputChannel(ciface::InputChannel input_channel)
{
  tls_input_channel = input_channel;
}

ciface::InputChannel ControllerInterface::GetCurrentInputChannel()
{
  return tls_input_channel;
}

void ControllerInterface::SetAspectRatioAdjustment(float value)
{
  m_aspect_ratio_adjustment = value;
}

Common::Vec2 ControllerInterface::GetWindowInputScale() const
{
  const auto ar = m_aspect_ratio_adjustment.load();

  if (ar > 1)
    return {1.f, ar};
  else
    return {1 / ar, 1.f};
}

// Register a callback to be called when a device is added or removed (as from the input backends'
// hotplug thread), or when devices are refreshed
// Returns a handle for later removing the callback.
ControllerInterface::HotplugCallbackHandle
ControllerInterface::RegisterDevicesChangedCallback(std::function<void()> callback)
{
  std::lock_guard<std::mutex> lk(m_callbacks_mutex);
  m_devices_changed_callbacks.emplace_back(std::move(callback));
  return std::prev(m_devices_changed_callbacks.end());
}

// Unregister a device callback.
void ControllerInterface::UnregisterDevicesChangedCallback(const HotplugCallbackHandle& handle)
{
  std::lock_guard<std::mutex> lk(m_callbacks_mutex);
  m_devices_changed_callbacks.erase(handle);
}

// Invoke all callbacks that were registered
void ControllerInterface::InvokeDevicesChangedCallbacks() const
{
  std::lock_guard<std::mutex> lk(m_callbacks_mutex);
  for (const auto& callback : m_devices_changed_callbacks)
    callback();
}
