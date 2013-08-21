#include "VideoConfigDiag.h"

#include "FileUtil.h"
#include "TextureCacheBase.h"
#include "Core.h"
#include "Frame.h"

#include <wx/intl.h>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

extern CFrame* main_frame;

// template instantiation
template class BoolSetting<wxCheckBox>;
template class BoolSetting<wxRadioButton>;

template <>
SettingCheckBox::BoolSetting(wxWindow* parent, const wxString& label, const wxString& tooltip, bool &setting, bool reverse, long style)
	: wxCheckBox(parent, -1, label, wxDefaultPosition, wxDefaultSize, style)
	, m_setting(setting)
	, m_reverse(reverse)
{
	SetToolTip(tooltip);
	SetValue(m_setting ^ m_reverse);
	Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &SettingCheckBox::UpdateValue, this);
}

template <>
SettingRadioButton::BoolSetting(wxWindow* parent, const wxString& label, const wxString& tooltip, bool &setting, bool reverse, long style)
	: wxRadioButton(parent, -1, label, wxDefaultPosition, wxDefaultSize, style)
	, m_setting(setting)
	, m_reverse(reverse)
{
	SetToolTip(tooltip);
	SetValue(m_setting ^ m_reverse);
	Bind(wxEVT_COMMAND_RADIOBUTTON_SELECTED, &SettingRadioButton::UpdateValue, this);
}

SettingChoice::SettingChoice(wxWindow* parent, int &setting, const wxString& tooltip, int num, const wxString choices[], long style)
	: wxChoice(parent, -1, wxDefaultPosition, wxDefaultSize, num, choices)
	, m_setting(setting)
{
	SetToolTip(tooltip);
	Select(m_setting);
	Bind(wxEVT_COMMAND_CHOICE_SELECTED, &SettingChoice::UpdateValue, this);
}

void SettingChoice::UpdateValue(wxCommandEvent& ev)
{
	m_setting = ev.GetInt();
	ev.Skip();
}

void VideoConfigDiag::Event_ClickClose(wxCommandEvent&)
{
	Close();
}

void VideoConfigDiag::Event_Close(wxCloseEvent& ev)
{
	g_Config.Save((File::GetUserPath(D_CONFIG_IDX) + ininame + ".ini").c_str());

	EndModal(wxID_OK);
}

#if defined(_WIN32)
wxString backend_desc = wxTRANSLATE("Selects what graphics API to use internally.\nDirect3D 9 usually is the fastest one. OpenGL is more accurate though. Direct3D 11 is somewhere between the two.\nNote that the Direct3D backends are only available on Windows.\n\nIf unsure, use Direct3D 11.");
#else
wxString backend_desc = wxTRANSLATE("Selects what graphics API to use internally.\nDirect3D 9 usually is the fastest one. OpenGL is more accurate though. Direct3D 11 is somewhere between the two.\nNote that the Direct3D backends are only available on Windows.\n\nIf unsure, use OpenGL.");
#endif
wxString adapter_desc = wxTRANSLATE("Select a hardware adapter to use.\n\nIf unsure, use the first one.");
wxString display_res_desc = wxTRANSLATE("Selects the display resolution used in fullscreen mode.\nThis should always be bigger than or equal to the internal resolution. Performance impact is negligible.\n\nIf unsure, use your desktop resolution.\nIf still unsure, use the highest resolution which works for you.");
wxString use_fullscreen_desc = wxTRANSLATE("Enable this if you want the whole screen to be used for rendering.\nIf this is disabled, a render window will be created instead.\n\nIf unsure, leave this unchecked.");
wxString auto_window_size_desc = wxTRANSLATE("Automatically adjusts the window size to your internal resolution.\n\nIf unsure, leave this unchecked.");
wxString keep_window_on_top_desc = wxTRANSLATE("Keep the game window on top of all other windows.\n\nIf unsure, leave this unchecked.");
wxString hide_mouse_cursor_desc = wxTRANSLATE("Hides the mouse cursor if it's on top of the emulation window.\n\nIf unsure, leave this checked.");
wxString render_to_main_win_desc = wxTRANSLATE("Enable this if you want to use the main Dolphin window for rendering rather than a separate render window.\n\nIf unsure, leave this unchecked.");
wxString prog_scan_desc = wxTRANSLATE("Enables progressive scan if supported by the emulated software.\nMost games don't care about this.\n\nIf unsure, leave this unchecked.");
wxString ar_desc = wxTRANSLATE("Select what aspect ratio to use when rendering:\nAuto: Use the native aspect ratio\nForce 16:9: Stretch the picture to an aspect ratio of 16:9.\nForce 4:3: Stretch the picture to an aspect ratio of 4:3.\nStretch to Window: Stretch the picture to the window size.\n\nIf unsure, select Auto.");
wxString ws_hack_desc = wxTRANSLATE("Force the game to output graphics for widescreen resolutions.\nCauses graphical glitches is some games.\n\nIf unsure, leave this unchecked.");
wxString vsync_desc = wxTRANSLATE("Wait for vertical blanks in order to reduce tearing.\nDecreases performance if emulation speed is below 100%.\n\nIf unsure, leave this unchecked.");
wxString af_desc = wxTRANSLATE("Enable anisotropic filtering.\nEnhances visual quality of textures that are at oblique viewing angles.\nMight cause issues in a small number of games.\n\nIf unsure, select 1x.");
wxString aa_desc = wxTRANSLATE("Reduces the amount of aliasing caused by rasterizing 3D graphics.\nThis makes the rendered picture look less blocky.\nHeavily decreases emulation speed and sometimes causes issues.\n\nIf unsure, select None.");
wxString scaled_efb_copy_desc = wxTRANSLATE("Greatly increases quality of textures generated using render to texture effects.\nRaising the internal resolution will improve the effect of this setting.\nSlightly decreases performance and possibly causes issues (although unlikely).\n\nIf unsure, leave this checked.");
wxString pixel_lighting_desc = wxTRANSLATE("Calculate lighting of 3D graphics per-pixel rather than per vertex.\nDecreases emulation speed by some percent (depending on your GPU).\nThis usually is a safe enhancement, but might cause issues sometimes.\n\nIf unsure, leave this unchecked.");
wxString hacked_buffer_upload_desc = wxTRANSLATE("Speed up vertex streaming by using unsafe OpenGL code. Enabling this option might cause heavy glitches or even crash the emulator.\n\nIf unsure, leave this unchecked.");
wxString fast_depth_calc_desc = wxTRANSLATE("Use a less accurate algorithm to calculate depth values.\nCauses issues in a few games but might give a decent speedup.\n\nIf unsure, leave this checked.");
wxString force_filtering_desc = wxTRANSLATE("Force texture filtering even if the emulated game explicitly disabled it.\nImproves texture quality slightly but causes glitches in some games.\n\nIf unsure, leave this unchecked.");
wxString _3d_vision_desc = wxTRANSLATE("Enable 3D effects via stereoscopy using Nvidia 3D Vision technology if it's supported by your GPU.\nPossibly causes issues.\nRequires fullscreen to work.\n\nIf unsure, leave this unchecked.");
wxString internal_res_desc = wxTRANSLATE("Specifies the resolution used to render at. A high resolution will improve visual quality a lot but is also quite heavy on performance and might cause glitches in certain games.\n\"Multiple of 640x528\" is a bit slower than \"Window Size\" but yields less issues. Generally speaking, the lower the internal resolution is, the better your performance will be.\n\nIf unsure, select 640x528.");
wxString efb_access_desc = wxTRANSLATE("Ignore any requests of the CPU to read from or write to the EFB.\nImproves performance in some games, but might disable some gameplay-related features or graphical effects.\n\nIf unsure, leave this unchecked.");
wxString efb_emulate_format_changes_desc = wxTRANSLATE("Ignore any changes to the EFB format.\nImproves performance in many games without any negative effect. Causes graphical defects in a small number of other games though.\n\nIf unsure, leave this checked.");
wxString efb_copy_desc = wxTRANSLATE("Disable emulation of EFB copies.\nThese are often used for post-processing or render-to-texture effects, so while checking this setting gives a great speedup it almost always also causes issues.\n\nIf unsure, leave this unchecked.");
wxString efb_copy_texture_desc = wxTRANSLATE("Store EFB copies in GPU texture objects.\nThis is not so accurate, but it works well enough for most games and gives a great speedup over EFB to RAM.\n\nIf unsure, leave this checked.");
wxString efb_copy_ram_desc = wxTRANSLATE("Accurately emulate EFB copies.\nSome games depend on this for certain graphical effects or gameplay functionality.\n\nIf unsure, check EFB to Texture instead.");
wxString stc_desc = wxTRANSLATE("The safer you adjust this, the less likely the emulator will be missing any texture updates from RAM.\n\nIf unsure, use the rightmost value.");
wxString wireframe_desc = wxTRANSLATE("Render the scene as a wireframe.\n\nIf unsure, leave this unchecked.");
wxString disable_fog_desc = wxTRANSLATE("Makes distant objects more visible by removing fog, thus increasing the overall detail.\nDisabling fog will break some games which rely on proper fog emulation.\n\nIf unsure, leave this unchecked.");
wxString disable_dstalpha_desc = wxTRANSLATE("Disables emulation of a hardware feature called destination alpha, which is used in many games for various graphical effects.\n\nIf unsure, leave this unchecked.");
wxString show_fps_desc = wxTRANSLATE("Show the number of frames rendered per second as a measure of emulation speed.\n\nIf unsure, leave this unchecked.");
wxString log_fps_to_file_desc = wxTRANSLATE("Log the number of frames rendered per second to User/Logs/fps.txt. Use this feature when you want to measure the performance of Dolphin.\n\nIf unsure, leave this unchecked."); 
wxString show_input_display_desc = wxTRANSLATE("Display the inputs read by the emulator.\n\nIf unsure, leave this unchecked.");
wxString show_stats_desc = wxTRANSLATE("Show various statistics.\n\nIf unsure, leave this unchecked.");
wxString texfmt_desc = wxTRANSLATE("Modify textures to show the format they're encoded in. Needs an emulation reset in most cases.\n\nIf unsure, leave this unchecked.");
wxString efb_copy_regions_desc = wxTRANSLATE("[BROKEN]\nHighlight regions the EFB was copied from.\n\nIf unsure, leave this unchecked.");
wxString xfb_desc = wxTRANSLATE("Disable any XFB emulation.\nSpeeds up emulation a lot but causes heavy glitches in many games which rely on them (especially homebrew applications).\n\nIf unsure, leave this checked.");
wxString xfb_virtual_desc = wxTRANSLATE("Emulate XFBs using GPU texture objects.\nFixes many games which don't work without XFB emulation while not being as slow as real XFB emulation. However, it may still fail for a lot of other games (especially homebrew applications).\n\nIf unsure, leave this checked.");
wxString xfb_real_desc = wxTRANSLATE("Emulate XFBs accurately.\nSlows down emulation a lot and prohibits high-resolution rendering but is necessary to emulate a number of games properly.\n\nIf unsure, check virtual XFB emulation instead.");
wxString dump_textures_desc = wxTRANSLATE("Dump decoded game textures to User/Dump/Textures/<game_id>/\n\nIf unsure, leave this unchecked.");
wxString load_hires_textures_desc = wxTRANSLATE("Load custom textures from User/Load/Textures/<game_id>/\n\nIf unsure, leave this unchecked.");
wxString dump_efb_desc = wxTRANSLATE("Dump the contents of EFB copies to User/Dump/Textures/\n\nIf unsure, leave this unchecked.");
wxString dump_frames_desc = wxTRANSLATE("Dump all rendered frames to an AVI file in User/Dump/Frames/\n\nIf unsure, leave this unchecked.");
#if !defined WIN32 && defined HAVE_LIBAV
wxString use_ffv1_desc = wxTRANSLATE("Encode frame dumps using the FFV1 codec.\n\nIf unsure, leave this unchecked.");
#endif
wxString free_look_desc = wxTRANSLATE("This feature allows you to change the game's camera.\nMove the mouse while holding the right mouse button to pan and while holding the middle button to move.\nHold SHIFT and press one of the WASD keys to move the camera by a certain step distance (SHIFT+0 to move faster and SHIFT+9 to move slower). Press SHIFT+R to reset the camera.\n\nIf unsure, leave this unchecked.");
wxString crop_desc = wxTRANSLATE("Crop the picture from 4:3 to 5:4 or from 16:9 to 16:10.\n\nIf unsure, leave this unchecked.");
wxString opencl_desc = wxTRANSLATE("[EXPERIMENTAL]\nAims to speed up emulation by offloading texture decoding to the GPU using the OpenCL framework.\nHowever, right now it's known to cause texture defects in various games. Also it's slower than regular CPU texture decoding in most cases.\n\nIf unsure, leave this unchecked.");
wxString dlc_desc = wxTRANSLATE("[EXPERIMENTAL]\nSpeeds up emulation a bit by caching display lists.\nPossibly causes issues though.\n\nIf unsure, leave this unchecked.");
wxString omp_desc = wxTRANSLATE("Use multiple threads to decode textures.\nMight result in a speedup (especially on CPUs with more than two cores).\n\nIf unsure, leave this unchecked.");
wxString ppshader_desc = wxTRANSLATE("Apply a post-processing effect after finishing a frame.\n\nIf unsure, select (off).");
wxString cache_efb_copies_desc = wxTRANSLATE("Slightly speeds up EFB to RAM copies by sacrificing emulation accuracy.\nSometimes also increases visual quality.\nIf you're experiencing any issues, try raising texture cache accuracy or disable this option.\n\nIf unsure, leave this unchecked.");
wxString shader_errors_desc = wxTRANSLATE("Usually if shader compilation fails, an error message is displayed.\nHowever, one may skip the popups to allow interruption free gameplay by checking this option.\n\nIf unsure, leave this unchecked.");


// Search for available resolutions - TODO: Move to Common?
wxArrayString GetListOfResolutions()
{
	wxArrayString retlist;
	retlist.Add("Auto");
#ifdef _WIN32
	DWORD iModeNum = 0;
	DEVMODE dmi;
	ZeroMemory(&dmi, sizeof(dmi));
	dmi.dmSize = sizeof(dmi);
	std::vector<std::string> resos;

	while (EnumDisplaySettings(NULL, iModeNum++, &dmi) != 0)
	{
		char res[100];
		sprintf(res, "%dx%d", dmi.dmPelsWidth, dmi.dmPelsHeight);
		std::string strRes(res);
		// Only add unique resolutions
		if (std::find(resos.begin(), resos.end(), strRes) == resos.end())
		{
			resos.push_back(strRes);
			retlist.Add(StrToWxStr(res));
		}
		ZeroMemory(&dmi, sizeof(dmi));
	}
#elif defined(HAVE_XRANDR) && HAVE_XRANDR
	main_frame->m_XRRConfig->AddResolutions(retlist);
#elif defined(__APPLE__)
	CFArrayRef modes = CGDisplayAvailableModes(CGMainDisplayID());
	for (CFIndex i = 0; i < CFArrayGetCount(modes); i++)
	{
		std::stringstream res;
		CFDictionaryRef mode;
		CFNumberRef ref;
		int w, h, d;

		mode = (CFDictionaryRef)CFArrayGetValueAtIndex(modes, i);
		ref = (CFNumberRef)CFDictionaryGetValue(mode, kCGDisplayWidth);
		CFNumberGetValue(ref, kCFNumberIntType, &w);
		ref = (CFNumberRef)CFDictionaryGetValue(mode, kCGDisplayHeight);
		CFNumberGetValue(ref, kCFNumberIntType, &h);
		ref = (CFNumberRef)CFDictionaryGetValue(mode,
			kCGDisplayBitsPerPixel);
		CFNumberGetValue(ref, kCFNumberIntType, &d);

		if (CFDictionaryContainsKey(mode, kCGDisplayModeIsStretched))
			continue;
		if (d != 32)
			continue;

		res << w << "x" << h;

		retlist.Add(res.str());
	}
#endif
	return retlist;
}

VideoConfigDiag::VideoConfigDiag(wxWindow* parent, const std::string &title, const std::string& _ininame)
	: wxDialog(parent, -1,
		wxString::Format(_("Dolphin %s Graphics Configuration"), wxGetTranslation(StrToWxStr(title))),
		wxDefaultPosition, wxDefaultSize)
	, vconfig(g_Config)
	, ininame(_ininame)
{
	vconfig.Load((File::GetUserPath(D_CONFIG_IDX) + ininame + ".ini").c_str());

	Bind(wxEVT_UPDATE_UI, &VideoConfigDiag::OnUpdateUI, this);

	wxNotebook* const notebook = new wxNotebook(this, -1, wxDefaultPosition, wxDefaultSize);

	// -- GENERAL --
	{
	wxPanel* const page_general = new wxPanel(notebook, -1, wxDefaultPosition);
	notebook->AddPage(page_general, _("General"));
	wxBoxSizer* const szr_general = new wxBoxSizer(wxVERTICAL);

	// - basic
	{
	wxFlexGridSizer* const szr_basic = new wxFlexGridSizer(2, 5, 5);

	// backend
	{
	wxStaticText* const label_backend = new wxStaticText(page_general, wxID_ANY, _("Backend:"));
	choice_backend = new wxChoice(page_general, wxID_ANY, wxDefaultPosition);
	RegisterControl(choice_backend, wxGetTranslation(backend_desc));

	std::vector<VideoBackend*>::const_iterator
			it = g_available_video_backends.begin(),
			itend = g_available_video_backends.end();
	for (; it != itend; ++it)
		choice_backend->AppendString(wxGetTranslation(StrToWxStr((*it)->GetDisplayName())));

	choice_backend->SetStringSelection(wxGetTranslation(StrToWxStr(g_video_backend->GetDisplayName())));
	choice_backend->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &VideoConfigDiag::Event_Backend, this);

	szr_basic->Add(label_backend, 1, wxALIGN_CENTER_VERTICAL, 5);
	szr_basic->Add(choice_backend, 1, 0, 0);

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
	{
		label_backend->Disable();
		choice_backend->Disable();
	}
	}

	// adapter (D3D only)
	if (vconfig.backend_info.Adapters.size())
	{
		wxChoice* const choice_adapter = CreateChoice(page_general, vconfig.iAdapter, wxGetTranslation(adapter_desc));

		std::vector<std::string>::const_iterator
			it = vconfig.backend_info.Adapters.begin(),
			itend = vconfig.backend_info.Adapters.end();
		for (; it != itend; ++it)
			choice_adapter->AppendString(StrToWxStr(*it));

		choice_adapter->Select(vconfig.iAdapter);

		szr_basic->Add(new wxStaticText(page_general, -1, _("Adapter:")), 1, wxALIGN_CENTER_VERTICAL, 5);
		szr_basic->Add(choice_adapter, 1, 0, 0);
	}


	// - display
	wxFlexGridSizer* const szr_display = new wxFlexGridSizer(2, 5, 5);

	{

#if !defined(__APPLE__)
	// display resolution
	{
		wxArrayString res_list = GetListOfResolutions();
		if (res_list.empty())
			res_list.Add(_("<No resolutions found>"));
		wxStaticText* const label_display_resolution = new wxStaticText(page_general, wxID_ANY, _("Fullscreen resolution:"));
		choice_display_resolution = new wxChoice(page_general, wxID_ANY, wxDefaultPosition, wxDefaultSize, res_list);
		RegisterControl(choice_display_resolution, wxGetTranslation(display_res_desc));
		choice_display_resolution->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &VideoConfigDiag::Event_DisplayResolution, this);

		choice_display_resolution->SetStringSelection(StrToWxStr(SConfig::GetInstance().m_LocalCoreStartupParameter.strFullscreenResolution));

		szr_display->Add(label_display_resolution, 1, wxALIGN_CENTER_VERTICAL, 0);
		szr_display->Add(choice_display_resolution);

		if (Core::GetState() != Core::CORE_UNINITIALIZED)
		{
			label_display_resolution->Disable();
			choice_display_resolution->Disable();
		}
	}
#endif

	// aspect-ratio
	{
	const wxString ar_choices[] = { _("Auto"), _("Force 16:9"), _("Force 4:3"), _("Stretch to Window") };

	szr_display->Add(new wxStaticText(page_general, -1, _("Aspect Ratio:")), 1, wxALIGN_CENTER_VERTICAL, 0);
	wxChoice* const choice_aspect = CreateChoice(page_general, vconfig.iAspectRatio, wxGetTranslation(ar_desc),
														sizeof(ar_choices)/sizeof(*ar_choices), ar_choices);
	szr_display->Add(choice_aspect, 1, 0, 0);
	}

	// various other display options
	{
	szr_display->Add(CreateCheckBox(page_general, _("V-Sync"), wxGetTranslation(vsync_desc), vconfig.bVSync));
	szr_display->Add(CreateCheckBox(page_general, _("Use Fullscreen"), wxGetTranslation(use_fullscreen_desc), SConfig::GetInstance().m_LocalCoreStartupParameter.bFullscreen));
	}
	}

	// - other
	wxFlexGridSizer* const szr_other = new wxFlexGridSizer(2, 5, 5);

	{
	SettingCheckBox* render_to_main_cb;
	szr_other->Add(CreateCheckBox(page_general, _("Show FPS"), wxGetTranslation(show_fps_desc), vconfig.bShowFPS));
	szr_other->Add(CreateCheckBox(page_general, _("Log FPS to file"), wxGetTranslation(log_fps_to_file_desc), vconfig.bLogFPSToFile));
	szr_other->Add(CreateCheckBox(page_general, _("Auto adjust Window Size"), wxGetTranslation(auto_window_size_desc), SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderWindowAutoSize));
	szr_other->Add(CreateCheckBox(page_general, _("Keep window on top"), wxGetTranslation(keep_window_on_top_desc), SConfig::GetInstance().m_LocalCoreStartupParameter.bKeepWindowOnTop));
	szr_other->Add(CreateCheckBox(page_general, _("Hide Mouse Cursor"), wxGetTranslation(hide_mouse_cursor_desc), SConfig::GetInstance().m_LocalCoreStartupParameter.bHideCursor));
	szr_other->Add(render_to_main_cb = CreateCheckBox(page_general, _("Render to Main Window"), wxGetTranslation(render_to_main_win_desc), SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain));

	if (Core::GetState() != Core::CORE_UNINITIALIZED)
		render_to_main_cb->Disable();

	}


	wxStaticBoxSizer* const group_basic = new wxStaticBoxSizer(wxVERTICAL, page_general, _("Basic"));
	group_basic->Add(szr_basic, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	szr_general->Add(group_basic, 0, wxEXPAND | wxALL, 5);

	wxStaticBoxSizer* const group_display = new wxStaticBoxSizer(wxVERTICAL, page_general, _("Display"));
	group_display->Add(szr_display, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	szr_general->Add(group_display, 0, wxEXPAND | wxALL, 5);

	wxStaticBoxSizer* const group_other = new wxStaticBoxSizer(wxVERTICAL, page_general, _("Other"));
	group_other->Add(szr_other, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	szr_general->Add(group_other, 0, wxEXPAND | wxALL, 5);
	}

	szr_general->AddStretchSpacer();
	CreateDescriptionArea(page_general, szr_general);
	page_general->SetSizerAndFit(szr_general);
	}

	// -- ENHANCEMENTS --
	{
	wxPanel* const page_enh = new wxPanel(notebook, -1, wxDefaultPosition);
	notebook->AddPage(page_enh, _("Enhancements"));
	wxBoxSizer* const szr_enh_main = new wxBoxSizer(wxVERTICAL);

	// - enhancements
	wxFlexGridSizer* const szr_enh = new wxFlexGridSizer(2, 5, 5);

	// Internal resolution
	{
	const wxString efbscale_choices[] = { _("Auto (Window Size)"), _("Auto (Multiple of 640x528)"),
		_("1x Native (640x528)"), _("1.5x Native (960x792)"), _("2x Native (1280x1056)"), 
		_("2.5x Native (1600x1320)"), _("3x Native (1920x1584)"), _("4x Native (2560x2112)") };

	wxChoice *const choice_efbscale = CreateChoice(page_enh,
		vconfig.iEFBScale, wxGetTranslation(internal_res_desc), sizeof(efbscale_choices)/sizeof(*efbscale_choices), efbscale_choices);

	szr_enh->Add(new wxStaticText(page_enh, wxID_ANY, _("Internal Resolution:")), 1, wxALIGN_CENTER_VERTICAL, 0);
	szr_enh->Add(choice_efbscale);
	}

	// AA
	{
	text_aamode = new wxStaticText(page_enh, -1, _("Anti-Aliasing:"));
	choice_aamode = CreateChoice(page_enh, vconfig.iMultisampleMode, wxGetTranslation(aa_desc));

	std::vector<std::string>::const_iterator
		it = vconfig.backend_info.AAModes.begin(),
		itend = vconfig.backend_info.AAModes.end();
	for (; it != itend; ++it)
		choice_aamode->AppendString(wxGetTranslation(StrToWxStr(*it)));

	choice_aamode->Select(vconfig.iMultisampleMode);
	szr_enh->Add(text_aamode, 1, wxALIGN_CENTER_VERTICAL, 0);
	szr_enh->Add(choice_aamode);
	}

	// AF
	{
	const wxString af_choices[] = {wxT("1x"), wxT("2x"), wxT("4x"), wxT("8x"), wxT("16x")};
	szr_enh->Add(new wxStaticText(page_enh, -1, _("Anisotropic Filtering:")), 1, wxALIGN_CENTER_VERTICAL, 0);
	szr_enh->Add(CreateChoice(page_enh, vconfig.iMaxAnisotropy, wxGetTranslation(af_desc), 5, af_choices));
	}

	// postproc shader
	if (vconfig.backend_info.PPShaders.size())
	{
		wxChoice *const choice_ppshader = new wxChoice(page_enh, -1, wxDefaultPosition);
		RegisterControl(choice_ppshader, wxGetTranslation(ppshader_desc));
		choice_ppshader->AppendString(_("(off)"));

		std::vector<std::string>::const_iterator
			it = vconfig.backend_info.PPShaders.begin(),
			itend = vconfig.backend_info.PPShaders.end();
		for (; it != itend; ++it)
			choice_ppshader->AppendString(StrToWxStr(*it));

		if (vconfig.sPostProcessingShader.empty())
			choice_ppshader->Select(0);
		else
			choice_ppshader->SetStringSelection(StrToWxStr(vconfig.sPostProcessingShader));

		choice_ppshader->Bind(wxEVT_COMMAND_CHOICE_SELECTED, &VideoConfigDiag::Event_PPShader, this);

		szr_enh->Add(new wxStaticText(page_enh, -1, _("Post-Processing Effect:")), 1, wxALIGN_CENTER_VERTICAL, 0);
		szr_enh->Add(choice_ppshader);
	}

	// Scaled copy, PL, Bilinear filter, 3D Vision
	szr_enh->Add(CreateCheckBox(page_enh, _("Scaled EFB Copy"), wxGetTranslation(scaled_efb_copy_desc), vconfig.bCopyEFBScaled));
	szr_enh->Add(pixel_lighting = CreateCheckBox(page_enh, _("Per-Pixel Lighting"), wxGetTranslation(pixel_lighting_desc), vconfig.bEnablePixelLighting));
	szr_enh->Add(CreateCheckBox(page_enh, _("Force Texture Filtering"), wxGetTranslation(force_filtering_desc), vconfig.bForceFiltering));

	szr_enh->Add(CreateCheckBox(page_enh, _("Widescreen Hack"), wxGetTranslation(ws_hack_desc), vconfig.bWidescreenHack));
	szr_enh->Add(CreateCheckBox(page_enh, _("Disable Fog"), wxGetTranslation(disable_fog_desc), vconfig.bDisableFog));

	// 3D Vision
	_3d_vision = CreateCheckBox(page_enh, _("3D Vision"), wxGetTranslation(_3d_vision_desc), vconfig.b3DVision);
	_3d_vision->Show(vconfig.backend_info.bSupports3DVision);
	szr_enh->Add(_3d_vision);
	// TODO: Add anaglyph 3d here

	wxStaticBoxSizer* const group_enh = new wxStaticBoxSizer(wxVERTICAL, page_enh, _("Enhancements"));
	group_enh->Add(szr_enh, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	szr_enh_main->Add(group_enh, 0, wxEXPAND | wxALL, 5);


	szr_enh_main->AddStretchSpacer();
	CreateDescriptionArea(page_enh, szr_enh_main);
	page_enh->SetSizerAndFit(szr_enh_main);
	}


	// -- SPEED HACKS --
	{
	wxPanel* const page_hacks = new wxPanel(notebook, -1, wxDefaultPosition);
	notebook->AddPage(page_hacks, _("Hacks"));
	wxBoxSizer* const szr_hacks = new wxBoxSizer(wxVERTICAL);

	// - EFB hacks
	wxStaticBoxSizer* const szr_efb = new wxStaticBoxSizer(wxVERTICAL, page_hacks, _("Embedded Frame Buffer"));

	// format change emulation
	emulate_efb_format_changes = CreateCheckBox(page_hacks, _("Ignore Format Changes"), wxGetTranslation(efb_emulate_format_changes_desc), vconfig.bEFBEmulateFormatChanges, true);

	// EFB copies
	wxStaticBoxSizer* const group_efbcopy = new wxStaticBoxSizer(wxHORIZONTAL, page_hacks, _("EFB Copies"));

	SettingCheckBox* efbcopy_disable = CreateCheckBox(page_hacks, _("Disable"), wxGetTranslation(efb_copy_desc), vconfig.bEFBCopyEnable, true);
	efbcopy_texture = CreateRadioButton(page_hacks, _("Texture"), wxGetTranslation(efb_copy_texture_desc), vconfig.bCopyEFBToTexture, false, wxRB_GROUP);
	efbcopy_ram = CreateRadioButton(page_hacks, _("RAM"), wxGetTranslation(efb_copy_ram_desc), vconfig.bCopyEFBToTexture, true);
	cache_efb_copies = CreateCheckBox(page_hacks, _("Enable Cache"), wxGetTranslation(cache_efb_copies_desc), vconfig.bEFBCopyCacheEnable);

	group_efbcopy->Add(efbcopy_disable, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
	group_efbcopy->AddStretchSpacer(1);
	group_efbcopy->Add(efbcopy_texture, 0, wxRIGHT, 5);
	group_efbcopy->Add(efbcopy_ram, 0, wxRIGHT, 5);
	group_efbcopy->Add(cache_efb_copies, 0, wxRIGHT, 5);

	szr_efb->Add(CreateCheckBox(page_hacks, _("Skip EFB Access from CPU"), wxGetTranslation(efb_access_desc), vconfig.bEFBAccessEnable, true), 0, wxBOTTOM | wxLEFT, 5);
	szr_efb->Add(emulate_efb_format_changes, 0, wxBOTTOM | wxLEFT, 5);
	szr_efb->Add(group_efbcopy, 0, wxEXPAND | wxALL, 5);
	szr_hacks->Add(szr_efb, 0, wxEXPAND | wxALL, 5);

	// Texture cache
	{
	wxStaticBoxSizer* const szr_safetex = new wxStaticBoxSizer(wxHORIZONTAL, page_hacks, _("Texture Cache"));

	// TODO: Use wxSL_MIN_MAX_LABELS or wxSL_VALUE_LABEL with wx 2.9.1
	wxSlider* const stc_slider = new wxSlider(page_hacks, wxID_ANY, 0, 0, 2, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL|wxSL_BOTTOM);
	stc_slider->Bind(wxEVT_COMMAND_SLIDER_UPDATED, &VideoConfigDiag::Event_Stc, this);
	RegisterControl(stc_slider, wxGetTranslation(stc_desc));

	if (vconfig.iSafeTextureCache_ColorSamples == 0) stc_slider->SetValue(0);
	else if (vconfig.iSafeTextureCache_ColorSamples == 512) stc_slider->SetValue(1);
	else if (vconfig.iSafeTextureCache_ColorSamples == 128) stc_slider->SetValue(2);
	else stc_slider->Disable(); // Using custom number of samples; TODO: Inform the user why this is disabled..

	szr_safetex->Add(new wxStaticText(page_hacks, wxID_ANY, _("Accuracy:")), 0, wxALL, 5);
	szr_safetex->AddStretchSpacer(1);
	szr_safetex->Add(new wxStaticText(page_hacks, wxID_ANY, _("Safe")), 0, wxLEFT|wxTOP|wxBOTTOM, 5);
	szr_safetex->Add(stc_slider, 2, wxRIGHT, 0);
	szr_safetex->Add(new wxStaticText(page_hacks, wxID_ANY, _("Fast")), 0, wxRIGHT|wxTOP|wxBOTTOM, 5);
	szr_hacks->Add(szr_safetex, 0, wxEXPAND | wxALL, 5);
	}

	// - XFB
	{
	wxStaticBoxSizer* const group_xfb = new wxStaticBoxSizer(wxHORIZONTAL, page_hacks, _("External Frame Buffer"));

	SettingCheckBox* disable_xfb = CreateCheckBox(page_hacks, _("Disable"), wxGetTranslation(xfb_desc), vconfig.bUseXFB, true);
	virtual_xfb = CreateRadioButton(page_hacks, _("Virtual"), wxGetTranslation(xfb_virtual_desc), vconfig.bUseRealXFB, true, wxRB_GROUP);
	real_xfb = CreateRadioButton(page_hacks, _("Real"), wxGetTranslation(xfb_real_desc), vconfig.bUseRealXFB);

	group_xfb->Add(disable_xfb, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
	group_xfb->AddStretchSpacer(1);
	group_xfb->Add(virtual_xfb, 0, wxRIGHT, 5);
	group_xfb->Add(real_xfb, 0, wxRIGHT, 5);
	szr_hacks->Add(group_xfb, 0, wxEXPAND | wxALL, 5);
	}	// xfb

	// - other hacks
	{
	wxGridSizer* const szr_other = new wxGridSizer(2, 5, 5);
	szr_other->Add(CreateCheckBox(page_hacks, _("Cache Display Lists"), wxGetTranslation(dlc_desc), vconfig.bDlistCachingEnable));
	szr_other->Add(CreateCheckBox(page_hacks, _("Disable Destination Alpha"), wxGetTranslation(disable_dstalpha_desc), vconfig.bDstAlphaPass));
	szr_other->Add(CreateCheckBox(page_hacks, _("OpenCL Texture Decoder"), wxGetTranslation(opencl_desc), vconfig.bEnableOpenCL));
	szr_other->Add(CreateCheckBox(page_hacks, _("OpenMP Texture Decoder"), wxGetTranslation(omp_desc), vconfig.bOMPDecoder));
	szr_other->Add(CreateCheckBox(page_hacks, _("Fast Depth Calculation"), wxGetTranslation(fast_depth_calc_desc), vconfig.bFastDepthCalc));
	szr_other->Add(hacked_buffer_upload = CreateCheckBox(page_hacks, _("Vertex Streaming Hack"), wxGetTranslation(hacked_buffer_upload_desc), vconfig.bHackedBufferUpload));
	
	wxStaticBoxSizer* const group_other = new wxStaticBoxSizer(wxVERTICAL, page_hacks, _("Other"));
	group_other->Add(szr_other, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	szr_hacks->Add(group_other, 0, wxEXPAND | wxALL, 5);
	}

	szr_hacks->AddStretchSpacer();
	CreateDescriptionArea(page_hacks, szr_hacks);
	page_hacks->SetSizerAndFit(szr_hacks);
	}

	// -- ADVANCED --
	{
	wxPanel* const page_advanced = new wxPanel(notebook, -1, wxDefaultPosition);
	notebook->AddPage(page_advanced, _("Advanced"));
	wxBoxSizer* const szr_advanced = new wxBoxSizer(wxVERTICAL);

	// - debug
	{
	wxGridSizer* const szr_debug = new wxGridSizer(2, 5, 5);

	szr_debug->Add(CreateCheckBox(page_advanced, _("Enable Wireframe"), wxGetTranslation(wireframe_desc), vconfig.bWireFrame));
	szr_debug->Add(CreateCheckBox(page_advanced, _("Show EFB Copy Regions"), wxGetTranslation(efb_copy_regions_desc), vconfig.bShowEFBCopyRegions));
	szr_debug->Add(CreateCheckBox(page_advanced, _("Show Statistics"), wxGetTranslation(show_stats_desc), vconfig.bOverlayStats));
	szr_debug->Add(CreateCheckBox(page_advanced, _("Texture Format Overlay"), wxGetTranslation(texfmt_desc), vconfig.bTexFmtOverlayEnable));

	wxStaticBoxSizer* const group_debug = new wxStaticBoxSizer(wxVERTICAL, page_advanced, _("Debugging"));
	szr_advanced->Add(group_debug, 0, wxEXPAND | wxALL, 5);
	group_debug->Add(szr_debug, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	}

	// - utility
	{
	wxGridSizer* const szr_utility = new wxGridSizer(2, 5, 5);

	szr_utility->Add(CreateCheckBox(page_advanced, _("Dump Textures"), wxGetTranslation(dump_textures_desc), vconfig.bDumpTextures));
	szr_utility->Add(CreateCheckBox(page_advanced, _("Load Custom Textures"), wxGetTranslation(load_hires_textures_desc), vconfig.bHiresTextures));
	szr_utility->Add(CreateCheckBox(page_advanced, _("Dump EFB Target"), wxGetTranslation(dump_efb_desc), vconfig.bDumpEFBTarget));
	szr_utility->Add(CreateCheckBox(page_advanced, _("Dump Frames"), wxGetTranslation(dump_frames_desc), vconfig.bDumpFrames));
	szr_utility->Add(CreateCheckBox(page_advanced, _("Free Look"), wxGetTranslation(free_look_desc), vconfig.bFreeLook));
#if !defined WIN32 && defined HAVE_LIBAV
	szr_utility->Add(CreateCheckBox(page_advanced, _("Frame Dumps use FFV1"), wxGetTranslation(use_ffv1_desc), vconfig.bUseFFV1));
#endif

	wxStaticBoxSizer* const group_utility = new wxStaticBoxSizer(wxVERTICAL, page_advanced, _("Utility"));
	szr_advanced->Add(group_utility, 0, wxEXPAND | wxALL, 5);
	group_utility->Add(szr_utility, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	}

	// - misc
	{
	wxGridSizer* const szr_misc = new wxGridSizer(2, 5, 5);

	szr_misc->Add(CreateCheckBox(page_advanced, _("Show Input Display"), wxGetTranslation(show_input_display_desc), vconfig.bShowInputDisplay));
	szr_misc->Add(CreateCheckBox(page_advanced, _("Crop"), wxGetTranslation(crop_desc), vconfig.bCrop));

	// Progressive Scan
	{
	wxCheckBox* const cb_prog_scan = new wxCheckBox(page_advanced, wxID_ANY, _("Enable Progressive Scan"));
	RegisterControl(cb_prog_scan, wxGetTranslation(prog_scan_desc));
	cb_prog_scan->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &VideoConfigDiag::Event_ProgressiveScan, this);
	if (Core::GetState() != Core::CORE_UNINITIALIZED)
		cb_prog_scan->Disable();

	cb_prog_scan->SetValue(SConfig::GetInstance().m_LocalCoreStartupParameter.bProgressive);
	// A bit strange behavior, but this needs to stay in sync with the main progressive boolean; TODO: Is this still necessary?
	SConfig::GetInstance().m_SYSCONF->SetData("IPL.PGS", SConfig::GetInstance().m_LocalCoreStartupParameter.bProgressive);

	szr_misc->Add(cb_prog_scan);
	}

	wxStaticBoxSizer* const group_misc = new wxStaticBoxSizer(wxVERTICAL, page_advanced, _("Misc"));
	szr_advanced->Add(group_misc, 0, wxEXPAND | wxALL, 5);
	group_misc->Add(szr_misc, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	}

	szr_advanced->AddStretchSpacer();
	CreateDescriptionArea(page_advanced, szr_advanced);
	page_advanced->SetSizerAndFit(szr_advanced);
	}

	wxButton* const btn_close = new wxButton(this, wxID_OK, _("Close"), wxDefaultPosition);
	btn_close->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &VideoConfigDiag::Event_ClickClose, this);

	Bind(wxEVT_CLOSE_WINDOW, &VideoConfigDiag::Event_Close, this);

	wxBoxSizer* const szr_main = new wxBoxSizer(wxVERTICAL);
	szr_main->Add(notebook, 1, wxEXPAND | wxALL, 5);
	szr_main->Add(btn_close, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 5);

	SetSizerAndFit(szr_main);
	Center();
	SetFocus();

	UpdateWindowUI();
}

void VideoConfigDiag::Event_DisplayResolution(wxCommandEvent &ev)
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.strFullscreenResolution =
		WxStrToStr(choice_display_resolution->GetStringSelection());
#if defined(HAVE_XRANDR) && HAVE_XRANDR
	main_frame->m_XRRConfig->Update();
#endif
	ev.Skip();
}

SettingCheckBox* VideoConfigDiag::CreateCheckBox(wxWindow* parent, const wxString& label, const wxString& description, bool &setting, bool reverse, long style)
{
	SettingCheckBox* const cb = new SettingCheckBox(parent, label, wxString(), setting, reverse, style);
	RegisterControl(cb, description);
	return cb;
}

SettingChoice* VideoConfigDiag::CreateChoice(wxWindow* parent, int& setting, const wxString& description, int num, const wxString choices[], long style)
{
	SettingChoice* const ch = new SettingChoice(parent, setting, wxString(), num, choices, style);
	RegisterControl(ch, description);
	return ch;
}

SettingRadioButton* VideoConfigDiag::CreateRadioButton(wxWindow* parent, const wxString& label, const wxString& description, bool &setting, bool reverse, long style)
{
	SettingRadioButton* const rb = new SettingRadioButton(parent, label, wxString(), setting, reverse, style);
	RegisterControl(rb, description);
	return rb;
}

/* Use this to register descriptions for controls which have NOT been created using the Create* functions from above */
wxControl* VideoConfigDiag::RegisterControl(wxControl* const control, const wxString& description)
{
	ctrl_descs.insert(std::pair<wxWindow*,wxString>(control, description));
	control->Bind(wxEVT_ENTER_WINDOW, &VideoConfigDiag::Evt_EnterControl, this);
	control->Bind(wxEVT_LEAVE_WINDOW, &VideoConfigDiag::Evt_LeaveControl, this);
	return control;
}

void VideoConfigDiag::Evt_EnterControl(wxMouseEvent& ev)
{
	// TODO: Re-Fit the sizer if necessary!

	// Get settings control object from event
	wxWindow* ctrl = (wxWindow*)ev.GetEventObject();
	if (!ctrl) return;

	// look up description text object from the control's parent (which is the wxPanel of the current tab)
	wxStaticText* descr_text = desc_texts[ctrl->GetParent()];
	if (!descr_text) return;

	// look up the description of the selected control and assign it to the current description text object's label
	descr_text->SetLabel(ctrl_descs[ctrl]);
	descr_text->Wrap(descr_text->GetContainingSizer()->GetSize().x - 20);

	ev.Skip();
}

// TODO: Don't hardcode the size of the description area via line breaks
#define DEFAULT_DESC_TEXT _("Move the mouse pointer over an option to display a detailed description.\n\n\n\n\n\n\n")
void VideoConfigDiag::Evt_LeaveControl(wxMouseEvent& ev)
{
	// look up description text control and reset its label
	wxWindow* ctrl = (wxWindow*)ev.GetEventObject();
	if (!ctrl) return;
	wxStaticText* descr_text = desc_texts[ctrl->GetParent()];
	if (!descr_text) return;

	descr_text->SetLabel(DEFAULT_DESC_TEXT);
	descr_text->Wrap(descr_text->GetContainingSizer()->GetSize().x - 20);
	ev.Skip();
}

void VideoConfigDiag::CreateDescriptionArea(wxPanel* const page, wxBoxSizer* const sizer)
{
	// Create description frame
	wxStaticBoxSizer* const desc_sizer = new wxStaticBoxSizer(wxVERTICAL, page, _("Description"));
	sizer->Add(desc_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Need to call SetSizerAndFit here, since we don't want the description texts to change the dialog width
	page->SetSizerAndFit(sizer);

	// Create description text
	wxStaticText* const desc_text = new wxStaticText(page, wxID_ANY, DEFAULT_DESC_TEXT);
	desc_text->Wrap(desc_sizer->GetSize().x - 20);
	desc_sizer->Add(desc_text, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Store description text object for later lookup
	desc_texts.insert(std::pair<wxWindow*,wxStaticText*>(page, desc_text));
}
