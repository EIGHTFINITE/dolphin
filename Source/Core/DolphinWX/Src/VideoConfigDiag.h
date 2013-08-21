
#ifndef _CONFIG_DIAG_H_
#define _CONFIG_DIAG_H_

#include <vector>
#include <string>
#include <map>

#include "ConfigManager.h"
#include "VideoConfig.h"
#include "Core.h"

#include <wx/wx.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/spinctrl.h>

#include "MsgHandler.h"
#include "WxUtils.h"

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

typedef IntegerSetting<u32> U32Setting;

class SettingChoice : public wxChoice
{
public:
	SettingChoice(wxWindow* parent, int &setting, const wxString& tooltip, int num = 0, const wxString choices[] = NULL, long style = 0);
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
		VideoBackend* new_backend = g_available_video_backends[ev.GetInt()];
		if (g_video_backend != new_backend)
		{
			bool do_switch = true;
			if (new_backend->GetName() == "Software Renderer")
			{
				do_switch = (wxYES == wxMessageBox(_("Software rendering is an order of magnitude slower than using the other backends.\nIt's only useful for debugging purposes.\nDo you really want to enable software rendering? If unsure, select 'No'."),
							_("Warning"), wxYES_NO | wxNO_DEFAULT | wxICON_EXCLAMATION, wxGetActiveWindow()));
			}

			if (do_switch)
			{
				// TODO: Only reopen the dialog if the software backend is
				// selected (make sure to reinitialize backend info)
				// reopen the dialog
				Close();

				g_video_backend = new_backend;
				SConfig::GetInstance().m_LocalCoreStartupParameter.m_strVideoBackend = g_video_backend->GetName();

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
		SConfig::GetInstance().m_LocalCoreStartupParameter.bProgressive = ev.IsChecked();

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

		// pixel lighting
		pixel_lighting->Enable(vconfig.backend_info.bSupportsPixelLighting);

		// 3D vision
		_3d_vision->Enable(vconfig.backend_info.bSupports3DVision);
		_3d_vision->Show(vconfig.backend_info.bSupports3DVision);

		// EFB copy
		efbcopy_texture->Enable(vconfig.bEFBCopyEnable);
		efbcopy_ram->Enable(vconfig.bEFBCopyEnable);
		cache_efb_copies->Enable(vconfig.bEFBCopyEnable && !vconfig.bCopyEFBToTexture);

		// EFB format change emulation
		emulate_efb_format_changes->Enable(vconfig.backend_info.bSupportsFormatReinterpretation);

		// XFB
		virtual_xfb->Enable(vconfig.bUseXFB);
		real_xfb->Enable(vconfig.bUseXFB);
		
		// OGL Hacked buffer
		hacked_buffer_upload->Enable(Core::GetState() == Core::CORE_UNINITIALIZED && vconfig.backend_info.APIType == API_OPENGL);
		hacked_buffer_upload->Show(vconfig.backend_info.APIType == API_OPENGL);

		ev.Skip();
	}

	// Creates controls and connects their enter/leave window events to Evt_Enter/LeaveControl
	SettingCheckBox* CreateCheckBox(wxWindow* parent, const wxString& label, const wxString& description, bool &setting, bool reverse = false, long style = 0);
	SettingChoice* CreateChoice(wxWindow* parent, int& setting, const wxString& description, int num = 0, const wxString choices[] = NULL, long style = 0);
	SettingRadioButton* CreateRadioButton(wxWindow* parent, const wxString& label, const wxString& description, bool &setting, bool reverse = false, long style = 0);

	// Same as above but only connects enter/leave window events
	wxControl* RegisterControl(wxControl* const control, const wxString& description);

	void Evt_EnterControl(wxMouseEvent& ev);
	void Evt_LeaveControl(wxMouseEvent& ev);
	void CreateDescriptionArea(wxPanel* const page, wxBoxSizer* const sizer);

	wxChoice* choice_backend;
	wxChoice* choice_display_resolution;
	wxStaticText* text_aamode;
	SettingChoice* choice_aamode;

	SettingCheckBox* pixel_lighting;

	SettingCheckBox* _3d_vision;

	SettingRadioButton* efbcopy_texture;
	SettingRadioButton* efbcopy_ram;
	SettingCheckBox* cache_efb_copies;
	SettingCheckBox* emulate_efb_format_changes;
	SettingCheckBox* hacked_buffer_upload;

	SettingRadioButton* virtual_xfb;
	SettingRadioButton* real_xfb;

	std::map<wxWindow*, wxString> ctrl_descs; // maps setting controls to their descriptions
	std::map<wxWindow*, wxStaticText*> desc_texts; // maps dialog tabs (which are the parents of the setting controls) to their description text objects

	VideoConfig &vconfig;
	std::string ininame;
};

#endif
