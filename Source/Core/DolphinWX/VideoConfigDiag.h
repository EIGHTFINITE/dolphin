// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "Common/CommonTypes.h"
#include "Common/SysConf.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "DolphinWX/PostProcessingConfigDiag.h"
#include "DolphinWX/WxUtils.h"
#include "VideoCommon/PostProcessing.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

class wxBoxSizer;
class wxControl;
class wxPanel;

template <typename W>
class BoolSetting : public W
{
public:
	BoolSetting(wxWindow* parent, const wxString& label, const wxString& tooltip, bool &setting, bool reverse = false, long style = 0);

	void UpdateValue(wxCommandEvent& ev)
	{
		m_setting = (ev.GetInt() != 0) ^ m_reverse;
		ev.Skip();
	}
private:
	bool &m_setting;
	const bool m_reverse;
};

typedef BoolSetting<wxCheckBox> SettingCheckBox;
typedef BoolSetting<wxRadioButton> SettingRadioButton;

template <typename T>
class IntegerSetting : public wxSpinCtrl
{
public:
	IntegerSetting(wxWindow* parent, const wxString& label, T& setting, int minVal, int maxVal, long style = 0);

	void UpdateValue(wxCommandEvent& ev)
	{
		m_setting = ev.GetInt();
		ev.Skip();
	}
private:
	T& m_setting;
};

class SettingChoice : public wxChoice
{
public:
	SettingChoice(wxWindow* parent, int &setting, const wxString& tooltip, int num = 0, const wxString choices[] = nullptr, long style = 0);
	void UpdateValue(wxCommandEvent& ev);
private:
	int &m_setting;
};

class VideoConfigDiag : public wxDialog
{
public:
	VideoConfigDiag(wxWindow* parent, const std::string &title, const std::string& ininame);

protected:
	void Event_Backend(wxCommandEvent &ev)
	{
		auto& new_backend = g_available_video_backends[ev.GetInt()];

		if (g_video_backend != new_backend.get())
		{
			bool do_switch = !Core::IsRunning();
			if (new_backend->GetName() == "Software Renderer")
			{
				do_switch = (wxYES == wxMessageBox(_("Software rendering is an order of magnitude slower than using the other backends.\nIt's only useful for debugging purposes.\nDo you really want to enable software rendering? If unsure, select 'No'."),
							_("Warning"), wxYES_NO | wxNO_DEFAULT | wxICON_EXCLAMATION, wxWindow::FindFocus()));
			}

			if (do_switch)
			{
				// TODO: Only reopen the dialog if the software backend is
				// selected (make sure to reinitialize backend info)
				// reopen the dialog
				Close();

				g_video_backend = new_backend.get();
				SConfig::GetInstance().m_strVideoBackend = g_video_backend->GetName();

				g_video_backend->ShowConfig(GetParent());
			}
			else
			{
				// Select current backend again
				choice_backend->SetStringSelection(StrToWxStr(g_video_backend->GetName()));
			}
		}

		ev.Skip();
	}
	void Event_Adapter(wxCommandEvent &ev) { ev.Skip(); } // TODO

	void Event_DisplayResolution(wxCommandEvent &ev);

	void Event_ProgressiveScan(wxCommandEvent &ev)
	{
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.PGS", ev.GetInt());
		SConfig::GetInstance().bProgressive = ev.IsChecked();

		ev.Skip();
	}

	void Event_Stc(wxCommandEvent &ev)
	{
		int samples[] = { 0, 512, 128 };
		vconfig.iSafeTextureCache_ColorSamples = samples[ev.GetInt()];

		ev.Skip();
	}

	void Event_PPShader(wxCommandEvent &ev)
	{
		const int sel = ev.GetInt();
		if (sel)
			vconfig.sPostProcessingShader = WxStrToStr(ev.GetString());
		else
			vconfig.sPostProcessingShader.clear();

		// Should we enable the configuration button?
		PostProcessingShaderConfiguration postprocessing_shader;
		postprocessing_shader.LoadShader(vconfig.sPostProcessingShader);
		button_config_pp->Enable(postprocessing_shader.HasOptions());

		ev.Skip();
	}

	void Event_ConfigurePPShader(wxCommandEvent &ev)
	{
		PostProcessingConfigDiag dialog(this, vconfig.sPostProcessingShader);
		dialog.ShowModal();

		ev.Skip();
	}

	void Event_StereoDepth(wxCommandEvent &ev)
	{
		vconfig.iStereoDepth = ev.GetInt();

		ev.Skip();
	}

	void Event_StereoConvergence(wxCommandEvent &ev)
	{
		// Snap the slider
		int value = ev.GetInt();
		if (90 < value && value < 110)
			conv_slider->SetValue(100);

		vconfig.iStereoConvergencePercentage = conv_slider->GetValue();

		ev.Skip();
	}

	void Event_StereoMode(wxCommandEvent &ev)
	{
		if (vconfig.backend_info.bSupportsPostProcessing)
		{
			// Anaglyph overrides post-processing shaders
			choice_ppshader->Clear();
		}

		ev.Skip();
	}

	void Event_ClickClose(wxCommandEvent&);
	void Event_Close(wxCloseEvent&);

	// Enables/disables UI elements depending on current config
	void OnUpdateUI(wxUpdateUIEvent& ev)
	{
		// Anti-aliasing
		choice_aamode->Enable(vconfig.backend_info.AAModes.size() > 1);
		text_aamode->Enable(vconfig.backend_info.AAModes.size() > 1);


		// XFB
		virtual_xfb->Enable(vconfig.bUseXFB);
		real_xfb->Enable(vconfig.bUseXFB);

		// custom textures
		cache_hires_textures->Enable(vconfig.bHiresTextures);

		// Repopulating the post-processing shaders can't be done from an event
		if (choice_ppshader && choice_ppshader->IsEmpty())
			PopulatePostProcessingShaders();

		// Things which shouldn't be changed during emulation
		if (Core::IsRunning())
		{
			choice_backend->Disable();
			label_backend->Disable();

			// D3D only
			if (vconfig.backend_info.Adapters.size())
			{
				choice_adapter->Disable();
				label_adapter->Disable();
			}

#ifndef __APPLE__
			// This isn't supported on OS X.

			choice_display_resolution->Disable();
			label_display_resolution->Disable();
#endif

			progressive_scan_checkbox->Disable();
			render_to_main_checkbox->Disable();
		}
		ev.Skip();
	}

	// Creates controls and connects their enter/leave window events to Evt_Enter/LeaveControl
	SettingCheckBox* CreateCheckBox(wxWindow* parent, const wxString& label, const wxString& description, bool &setting, bool reverse = false, long style = 0);
	SettingChoice* CreateChoice(wxWindow* parent, int& setting, const wxString& description, int num = 0, const wxString choices[] = nullptr, long style = 0);
	SettingRadioButton* CreateRadioButton(wxWindow* parent, const wxString& label, const wxString& description, bool &setting, bool reverse = false, long style = 0);

	// Same as above but only connects enter/leave window events
	wxControl* RegisterControl(wxControl* const control, const wxString& description);

	void Evt_EnterControl(wxMouseEvent& ev);
	void Evt_LeaveControl(wxMouseEvent& ev);
	void CreateDescriptionArea(wxPanel* const page, wxBoxSizer* const sizer);
	void PopulatePostProcessingShaders();
	void PopulateAAList();
	void OnAAChanged(wxCommandEvent& ev);

	wxChoice* choice_backend;
	wxChoice* choice_adapter;
	wxChoice* choice_display_resolution;

	wxStaticText* label_backend;
	wxStaticText* label_adapter;

	wxStaticText* text_aamode;
	wxChoice* choice_aamode;
	wxSlider* conv_slider;

	wxStaticText* label_display_resolution;

	wxButton* button_config_pp;

	SettingCheckBox* borderless_fullscreen;
	SettingCheckBox* render_to_main_checkbox;

	SettingRadioButton* virtual_xfb;
	SettingRadioButton* real_xfb;

	SettingCheckBox* cache_hires_textures;

	wxCheckBox* progressive_scan_checkbox;

	wxChoice* choice_ppshader;

	std::map<wxWindow*, wxString> ctrl_descs; // maps setting controls to their descriptions
	std::map<wxWindow*, wxStaticText*> desc_texts; // maps dialog tabs (which are the parents of the setting controls) to their description text objects

	VideoConfig &vconfig;

	size_t m_msaa_modes;
};
