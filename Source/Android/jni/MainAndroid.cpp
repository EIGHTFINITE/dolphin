// Copyright 2003 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <EGL/egl.h>
#include <UICommon/GameFile.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <jni.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "Common/AndroidAnalytics.h"
#include "Common/Assert.h"
#include "Common/CPUDetect.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Event.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/LogManager.h"
#include "Common/MsgHandler.h"
#include "Common/ScopeGuard.h"
#include "Common/Version.h"
#include "Common/WindowSystemInfo.h"

#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/DolphinAnalytics.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteReal/WiimoteReal.h"
#include "Core/Host.h"
#include "Core/PowerPC/JitInterface.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/Profiler.h"
#include "Core/State.h"

#include "DiscIO/Blob.h"
#include "DiscIO/Enums.h"
#include "DiscIO/ScrubbedBlob.h"
#include "DiscIO/Volume.h"

#include "InputCommon/ControllerInterface/Android/Android.h"
#include "InputCommon/ControllerInterface/Touch/ButtonManager.h"
#include "InputCommon/GCAdapter.h"

#include "UICommon/UICommon.h"

#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/VideoBackendBase.h"

#include "../../Core/Common/WindowSystemInfo.h"
#include "jni/AndroidCommon/AndroidCommon.h"
#include "jni/AndroidCommon/IDCache.h"

namespace
{
constexpr char DOLPHIN_TAG[] = "DolphinEmuNative";

ANativeWindow* s_surf;

// The Core only supports using a single Host thread.
// If multiple threads want to call host functions then they need to queue
// sequentially for access.
std::mutex s_host_identity_lock;
Common::Event s_update_main_frame_event;

// This exists to prevent surfaces from being destroyed during the boot process,
// as that can lead to the boot process dereferencing nullptr.
std::mutex s_surface_lock;
bool s_need_nonblocking_alert_msg;

bool s_have_wm_user_stop = false;
bool s_game_metadata_is_valid = false;
}  // Anonymous namespace

void UpdatePointer()
{
  // Update touch pointer
  JNIEnv* env = IDCache::GetEnvForThread();
  env->CallStaticVoidMethod(IDCache::GetNativeLibraryClass(), IDCache::GetUpdateTouchPointer());
}

std::vector<std::string> Host_GetPreferredLocales()
{
  // We would like to call ConfigurationCompat.getLocales here, but this function gets called
  // during dynamic initialization, and it seems like that makes us unable to obtain a JNIEnv.
  return {};
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

void Host_Message(HostMessageID id)
{
  if (id == HostMessageID::WMUserJobDispatch)
  {
    s_update_main_frame_event.Set();
  }
  else if (id == HostMessageID::WMUserStop)
  {
    s_have_wm_user_stop = true;
    if (Core::IsRunning())
      Core::QueueHostJob(&Core::Stop);
  }
}

void Host_UpdateTitle(const std::string& title)
{
  __android_log_write(ANDROID_LOG_INFO, DOLPHIN_TAG, title.c_str());
}

void Host_UpdateDisasmDialog()
{
}

void Host_UpdateMainFrame()
{
}

void Host_RequestRenderWindowSize(int width, int height)
{
  std::thread jnicall(UpdatePointer);
  jnicall.join();
}

bool Host_RendererHasFocus()
{
  return true;
}

bool Host_RendererIsFullscreen()
{
  return false;
}

void Host_YieldToUI()
{
}

void Host_TitleChanged()
{
  s_game_metadata_is_valid = true;

  JNIEnv* env = IDCache::GetEnvForThread();
  env->CallStaticVoidMethod(IDCache::GetNativeLibraryClass(), IDCache::GetOnTitleChanged());
}

static bool MsgAlert(const char* caption, const char* text, bool yes_no, Common::MsgType style)
{
  JNIEnv* env = IDCache::GetEnvForThread();

  // Execute the Java method.
  jboolean result =
      env->CallStaticBooleanMethod(IDCache::GetNativeLibraryClass(), IDCache::GetDisplayAlertMsg(),
                                   ToJString(env, caption), ToJString(env, text), yes_no,
                                   style == Common::MsgType::Warning, s_need_nonblocking_alert_msg);

  return result != JNI_FALSE;
}

static void ReportSend(const std::string& endpoint, const std::string& report)
{
  JNIEnv* env = IDCache::GetEnvForThread();

  jbyteArray output_array = env->NewByteArray(report.size());
  jbyte* output = env->GetByteArrayElements(output_array, nullptr);
  memcpy(output, report.data(), report.size());
  env->ReleaseByteArrayElements(output_array, output, 0);
  env->CallStaticVoidMethod(IDCache::GetAnalyticsClass(), IDCache::GetSendAnalyticsReport(),
                            ToJString(env, endpoint), output_array);
}

static std::string GetAnalyticValue(const std::string& key)
{
  JNIEnv* env = IDCache::GetEnvForThread();

  auto value = reinterpret_cast<jstring>(env->CallStaticObjectMethod(
      IDCache::GetAnalyticsClass(), IDCache::GetAnalyticsValue(), ToJString(env, key)));

  std::string stdvalue = GetJString(env, value);

  return stdvalue;
}

extern "C" {

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_UnPauseEmulation(JNIEnv*,
                                                                                     jclass)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  Core::SetState(Core::State::Running);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_PauseEmulation(JNIEnv*, jclass)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  Core::SetState(Core::State::Paused);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_StopEmulation(JNIEnv*, jclass)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  Core::Stop();

  // Kick the waiting event
  s_update_main_frame_event.Set();
}

JNIEXPORT jboolean JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_IsRunning(JNIEnv*, jclass)
{
  return static_cast<jboolean>(Core::IsRunning());
}

JNIEXPORT jboolean JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_IsRunningAndStarted(JNIEnv*,
                                                                                            jclass)
{
  return static_cast<jboolean>(Core::IsRunningAndStarted());
}

JNIEXPORT jboolean JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_onGamePadEvent(
    JNIEnv* env, jclass, jstring jDevice, jint Button, jint Action)
{
  return ButtonManager::GamepadEvent(GetJString(env, jDevice), Button, Action);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_onGamePadMoveEvent(
    JNIEnv* env, jclass, jstring jDevice, jint Axis, jfloat Value)
{
  ButtonManager::GamepadAxisEvent(GetJString(env, jDevice), Axis, Value);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SetMotionSensorsEnabled(
    JNIEnv*, jclass, jboolean accelerometer_enabled, jboolean gyroscope_enabled)
{
  ciface::Android::SetMotionSensorsEnabled(accelerometer_enabled, gyroscope_enabled);
}

JNIEXPORT double JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetInputRadiusAtAngle(
    JNIEnv*, jclass, int emu_pad_id, int stick, double angle)
{
  const auto casted_stick = static_cast<ButtonManager::ButtonType>(stick);
  return ButtonManager::GetInputRadiusAtAngle(emu_pad_id, casted_stick, angle);
}

JNIEXPORT jstring JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetVersionString(JNIEnv* env,
                                                                                        jclass)
{
  return ToJString(env, Common::scm_rev_str);
}

JNIEXPORT jstring JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetGitRevision(JNIEnv* env,
                                                                                      jclass)
{
  return ToJString(env, Common::scm_rev_git_str);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SaveScreenShot(JNIEnv*, jclass)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  Core::SaveScreenShot();
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_eglBindAPI(JNIEnv*, jclass,
                                                                               jint api)
{
  eglBindAPI(api);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SaveState(JNIEnv*, jclass,
                                                                              jint slot,
                                                                              jboolean wait)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  State::Save(slot, wait);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SaveStateAs(JNIEnv* env, jclass,
                                                                                jstring path,
                                                                                jboolean wait)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  State::SaveAs(GetJString(env, path), wait);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_LoadState(JNIEnv*, jclass,
                                                                              jint slot)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  State::Load(slot);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_LoadStateAs(JNIEnv* env, jclass,
                                                                                jstring path)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  State::LoadAs(GetJString(env, path));
}

JNIEXPORT jlong JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_GetUnixTimeOfStateSlot(JNIEnv*, jclass, jint slot)
{
  return static_cast<jlong>(State::GetUnixTimeOfSlot(slot));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_utils_DirectoryInitialization_SetSysDirectory(
    JNIEnv* env, jclass, jstring jPath)
{
  const std::string path = GetJString(env, jPath);
  File::SetSysDirectory(path);
}

JNIEXPORT void JNICALL
Java_org_dolphinemu_dolphinemu_utils_DirectoryInitialization_CreateUserDirectories(JNIEnv*, jclass)
{
  UICommon::CreateDirectories();
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SetUserDirectory(
    JNIEnv* env, jclass, jstring jDirectory)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  UICommon::SetUserDirectory(GetJString(env, jDirectory));
}

JNIEXPORT jstring JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetUserDirectory(JNIEnv* env,
                                                                                        jclass)
{
  return ToJString(env, File::GetUserPath(D_USER_IDX));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SetCacheDirectory(
    JNIEnv* env, jclass, jstring jDirectory)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  File::SetUserPath(D_CACHE_IDX, GetJString(env, jDirectory) + DIR_SEP);
}

JNIEXPORT jint JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_DefaultCPUCore(JNIEnv*, jclass)
{
  return static_cast<jint>(PowerPC::DefaultCPUCore());
}

JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_GetDefaultGraphicsBackendName(JNIEnv* env, jclass)
{
  return ToJString(env, VideoBackendBase::GetDefaultBackendName());
}

JNIEXPORT jint JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetMaxLogLevel(JNIEnv*, jclass)
{
  return static_cast<jint>(MAX_LOGLEVEL);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SetProfiling(JNIEnv*, jclass,
                                                                                 jboolean enable)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  Core::SetState(Core::State::Paused);
  JitInterface::ClearCache();
  JitInterface::SetProfilingState(enable ? JitInterface::ProfilingState::Enabled :
                                           JitInterface::ProfilingState::Disabled);
  Core::SetState(Core::State::Running);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_WriteProfileResults(JNIEnv*,
                                                                                        jclass)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  std::string filename = File::GetUserPath(D_DUMP_IDX) + "Debug/profiler.txt";
  File::CreateFullPath(filename);
  JitInterface::WriteProfileResults(filename);
}

// Surface Handling
JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SurfaceChanged(JNIEnv* env,
                                                                                   jclass,
                                                                                   jobject surf)
{
  std::lock_guard<std::mutex> guard(s_surface_lock);

  s_surf = ANativeWindow_fromSurface(env, surf);
  if (s_surf == nullptr)
    __android_log_print(ANDROID_LOG_ERROR, DOLPHIN_TAG, "Error: Surface is null.");

  if (g_renderer)
    g_renderer->ChangeSurface(s_surf);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_SurfaceDestroyed(JNIEnv*,
                                                                                     jclass)
{
  std::lock_guard<std::mutex> guard(s_surface_lock);

  if (g_renderer)
    g_renderer->ChangeSurface(nullptr);

  if (s_surf)
  {
    ANativeWindow_release(s_surf);
    s_surf = nullptr;
  }
}

JNIEXPORT jfloat JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetGameAspectRatio(JNIEnv*,
                                                                                         jclass)
{
  return g_renderer->CalculateDrawAspectRatio();
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_RefreshWiimotes(JNIEnv*, jclass)
{
  std::lock_guard<std::mutex> guard(s_host_identity_lock);
  WiimoteReal::Refresh();
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_ReloadWiimoteConfig(JNIEnv*,
                                                                                        jclass)
{
  WiimoteReal::LoadSettings();
  Wiimote::LoadConfig();
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_ReloadConfig(JNIEnv*, jclass)
{
  SConfig::GetInstance().LoadSettings();
}

JNIEXPORT void JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_UpdateGCAdapterScanThread(JNIEnv*, jclass)
{
  if (GCAdapter::UseAdapter())
  {
    GCAdapter::StartScanThread();
  }
  else
  {
    GCAdapter::StopScanThread();
  }
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_Initialize(JNIEnv*, jclass)
{
  Common::RegisterMsgAlertHandler(&MsgAlert);
  Common::AndroidSetReportHandler(&ReportSend);
  DolphinAnalytics::AndroidSetGetValFunc(&GetAnalyticValue);
  UICommon::Init();
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_ReportStartToAnalytics(JNIEnv*,
                                                                                           jclass)
{
  DolphinAnalytics::Instance().ReportDolphinStart(GetAnalyticValue("DEVICE_TYPE"));
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GenerateNewStatisticsId(JNIEnv*,
                                                                                            jclass)
{
  DolphinAnalytics::Instance().GenerateNewIdentity();
}

// Returns the scale factor for imgui rendering.
// Based on the scaledDensity of the device's display metrics.
static float GetRenderSurfaceScale(JNIEnv* env)
{
  jclass native_library_class = env->FindClass("org/dolphinemu/dolphinemu/NativeLibrary");
  jmethodID get_render_surface_scale_method =
      env->GetStaticMethodID(native_library_class, "getRenderSurfaceScale", "()F");
  return env->CallStaticFloatMethod(native_library_class, get_render_surface_scale_method);
}

static void Run(JNIEnv* env, const std::vector<std::string>& paths,
                const std::optional<std::string>& savestate_path = {},
                bool delete_savestate = false)
{
  ASSERT(!paths.empty());
  __android_log_print(ANDROID_LOG_INFO, DOLPHIN_TAG, "Running : %s", paths[0].c_str());

  std::unique_lock<std::mutex> host_identity_guard(s_host_identity_lock);

  WiimoteReal::InitAdapterClass();

  s_have_wm_user_stop = false;

  std::unique_ptr<BootParameters> boot = BootParameters::GenerateFromFile(paths, savestate_path);
  if (boot)
    boot->delete_savestate = delete_savestate;

  WindowSystemInfo wsi(WindowSystemType::Android, nullptr, s_surf, s_surf);
  wsi.render_surface_scale = GetRenderSurfaceScale(env);

  s_need_nonblocking_alert_msg = true;
  std::unique_lock<std::mutex> surface_guard(s_surface_lock);

  bool successful_boot = BootManager::BootCore(std::move(boot), wsi);
  if (successful_boot)
  {
    ButtonManager::Init(SConfig::GetInstance().GetGameID());

    static constexpr int TIMEOUT = 10000;
    static constexpr int WAIT_STEP = 25;
    int time_waited = 0;
    // A Core::CORE_ERROR state would be helpful here.
    while (!Core::IsRunningAndStarted())
    {
      if (time_waited >= TIMEOUT || s_have_wm_user_stop)
      {
        successful_boot = false;
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP));
      time_waited += WAIT_STEP;
    }
  }

  s_need_nonblocking_alert_msg = false;
  surface_guard.unlock();

  if (successful_boot)
  {
    while (Core::IsRunningAndStarted())
    {
      host_identity_guard.unlock();
      s_update_main_frame_event.Wait();
      host_identity_guard.lock();
      Core::HostDispatchJobs();
    }
  }

  s_game_metadata_is_valid = false;
  Core::Shutdown();
  ButtonManager::Shutdown();
  host_identity_guard.unlock();

  env->CallStaticVoidMethod(IDCache::GetNativeLibraryClass(),
                            IDCache::GetFinishEmulationActivity());
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_Run___3Ljava_lang_String_2(
    JNIEnv* env, jclass, jobjectArray jPaths)
{
  Run(env, JStringArrayToVector(env, jPaths));
}

JNIEXPORT void JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_Run___3Ljava_lang_String_2Ljava_lang_String_2Z(
    JNIEnv* env, jclass, jobjectArray jPaths, jstring jSavestate, jboolean jDeleteSavestate)
{
  Run(env, JStringArrayToVector(env, jPaths), GetJString(env, jSavestate), jDeleteSavestate);
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_ChangeDisc(JNIEnv* env, jclass,
                                                                               jstring jFile)
{
  const std::string path = GetJString(env, jFile);
  __android_log_print(ANDROID_LOG_INFO, DOLPHIN_TAG, "Change Disc: %s", path.c_str());
  Core::RunAsCPUThread([&path] { DVDInterface::ChangeDisc(path); });
}

JNIEXPORT jobject JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_GetLogTypeNames(JNIEnv* env,
                                                                                       jclass)
{
  std::map<std::string, std::string> map = Common::Log::LogManager::GetInstance()->GetLogTypes();

  auto map_size = static_cast<jsize>(map.size());
  jobject linked_hash_map =
      env->NewObject(IDCache::GetLinkedHashMapClass(), IDCache::GetLinkedHashMapInit(), map_size);
  for (const auto& entry : map)
  {
    env->CallObjectMethod(linked_hash_map, IDCache::GetLinkedHashMapPut(),
                          ToJString(env, entry.first), ToJString(env, entry.second));
  }
  return linked_hash_map;
}

JNIEXPORT void JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_ReloadLoggerConfig(JNIEnv*,
                                                                                       jclass)
{
  Common::Log::LogManager::Init();
}

JNIEXPORT jboolean JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_ConvertDiscImage(
    JNIEnv* env, jclass, jstring jInPath, jstring jOutPath, jint jPlatform, jint jFormat,
    jint jBlockSize, jint jCompression, jint jCompressionLevel, jboolean jScrub, jobject jCallback)
{
  const std::string in_path = GetJString(env, jInPath);
  const std::string out_path = GetJString(env, jOutPath);
  const DiscIO::Platform platform = static_cast<DiscIO::Platform>(jPlatform);
  const DiscIO::BlobType format = static_cast<DiscIO::BlobType>(jFormat);
  const DiscIO::WIARVZCompressionType compression =
      static_cast<DiscIO::WIARVZCompressionType>(jCompression);
  const bool scrub = static_cast<bool>(jScrub);

  std::unique_ptr<DiscIO::BlobReader> blob_reader;
  if (scrub)
    blob_reader = DiscIO::ScrubbedBlob::Create(in_path);
  else
    blob_reader = DiscIO::CreateBlobReader(in_path);

  if (!blob_reader)
    return static_cast<jboolean>(false);

  jobject jCallbackGlobal = env->NewGlobalRef(jCallback);
  Common::ScopeGuard scope_guard([jCallbackGlobal, env] { env->DeleteGlobalRef(jCallbackGlobal); });

  const auto callback = [&jCallbackGlobal](const std::string& text, float completion) {
    JNIEnv* env = IDCache::GetEnvForThread();
    return static_cast<bool>(env->CallBooleanMethod(
        jCallbackGlobal, IDCache::GetCompressCallbackRun(), ToJString(env, text), completion));
  };

  bool success = false;

  switch (format)
  {
  case DiscIO::BlobType::PLAIN:
    success = DiscIO::ConvertToPlain(blob_reader.get(), in_path, out_path, callback);
    break;

  case DiscIO::BlobType::GCZ:
    success =
        DiscIO::ConvertToGCZ(blob_reader.get(), in_path, out_path,
                             platform == DiscIO::Platform::WiiDisc ? 1 : 0, jBlockSize, callback);
    break;

  case DiscIO::BlobType::WIA:
  case DiscIO::BlobType::RVZ:
    success = DiscIO::ConvertToWIAOrRVZ(blob_reader.get(), in_path, out_path,
                                        format == DiscIO::BlobType::RVZ, compression,
                                        jCompressionLevel, jBlockSize, callback);
    break;

  default:
    ASSERT(false);
    break;
  }

  return static_cast<jboolean>(success);
}

JNIEXPORT jstring JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_FormatSize(JNIEnv* env,
                                                                                  jclass,
                                                                                  jlong bytes,
                                                                                  jint decimals)
{
  return ToJString(env, UICommon::FormatSize(bytes, decimals));
}

JNIEXPORT void JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_SetObscuredPixelsLeft(JNIEnv*, jclass, jint width)
{
  OSD::SetObscuredPixelsLeft(width);
}

JNIEXPORT void JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_SetObscuredPixelsTop(JNIEnv*, jclass, jint height)
{
  OSD::SetObscuredPixelsTop(height);
}

JNIEXPORT jboolean JNICALL Java_org_dolphinemu_dolphinemu_NativeLibrary_IsGameMetadataValid(JNIEnv*,
                                                                                            jclass)
{
  return s_game_metadata_is_valid;
}

JNIEXPORT jboolean JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_IsEmulatingWiiUnchecked(JNIEnv*, jclass)
{
  return SConfig::GetInstance().bWii;
}

JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_GetCurrentGameIDUnchecked(JNIEnv* env, jclass)
{
  return ToJString(env, SConfig::GetInstance().GetGameID());
}

JNIEXPORT jstring JNICALL
Java_org_dolphinemu_dolphinemu_NativeLibrary_GetCurrentTitleDescriptionUnchecked(JNIEnv* env,
                                                                                 jclass)
{
  // Prefer showing just the name. If no name is available, show just the game ID.
  std::string description = SConfig::GetInstance().GetTitleName();
  if (description.empty())
    description = SConfig::GetInstance().GetTitleDescription();

  return ToJString(env, description);
}
}
