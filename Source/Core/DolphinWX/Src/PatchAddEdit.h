// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef __PATCH_ADDEDIT_h__
#define __PATCH_ADDEDIT_h__

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include "ISOProperties.h"

class CPatchAddEdit : public wxDialog
{
	public:
		CPatchAddEdit(int _selection, wxWindow* parent,
			wxWindowID id = 1,
			const wxString& title = _("Edit Patch"),
			const wxPoint& pos = wxDefaultPosition,
			const wxSize& size = wxDefaultSize,
			long style = wxDEFAULT_DIALOG_STYLE);
		virtual ~CPatchAddEdit();

	private:
		DECLARE_EVENT_TABLE();

		wxTextCtrl *EditPatchName;
		wxTextCtrl *EditPatchOffset;
		wxRadioBox *EditPatchType;
		wxTextCtrl *EditPatchValue;
		wxSpinButton *EntrySelection;
		wxButton *EntryRemove;
		wxStaticBoxSizer* sbEntry;

		enum {
			ID_EDITPATCH_NAME_TEXT = 4500,
			ID_EDITPATCH_NAME,
			ID_EDITPATCH_OFFSET_TEXT,
			ID_EDITPATCH_OFFSET,
			ID_ENTRY_SELECT,
			ID_EDITPATCH_TYPE,
			ID_EDITPATCH_VALUE_TEXT,
			ID_EDITPATCH_VALUE,
			ID_ENTRY_ADD,
			ID_ENTRY_REMOVE
		};

		void CreateGUIControls(int selection);
		void ChangeEntry(wxSpinEvent& event);
		void SavePatchData(wxCommandEvent& event);
		void AddRemoveEntry(wxCommandEvent& event);
		void UpdateEntryCtrls(PatchEngine::PatchEntry pE);
		bool UpdateTempEntryData(std::vector<PatchEngine::PatchEntry>::iterator iterEntry);

		int selection, currentItem;
		std::vector<PatchEngine::PatchEntry> tempEntries;
		std::vector<PatchEngine::PatchEntry>::iterator itCurEntry;

};
#endif // __PATCH_ADDEDIT_h__
