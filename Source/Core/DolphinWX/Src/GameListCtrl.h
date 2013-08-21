// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef __GAMELIST_CTRL_H_
#define __GAMELIST_CTRL_H_

#include <memory>
#include <vector>

#include <wx/listctrl.h>
#include <wx/tipwin.h>

#include "ISOFile.h"
#include "MemoryCards/WiiSaveCrypted.h"

class wxEmuStateTip : public wxTipWindow
{
public:
	wxEmuStateTip(wxWindow* parent, const wxString& text, wxEmuStateTip** windowPtr)
		: wxTipWindow(parent, text, 70, (wxTipWindow**)windowPtr) {}
	// wxTipWindow doesn't correctly handle KeyEvents and crashes... we must overload that.
	void OnKeyDown(wxKeyEvent& event) { event.StopPropagation(); Close(); }
private:
	DECLARE_EVENT_TABLE()
};

class CGameListCtrl : public wxListCtrl
{
public:

	CGameListCtrl(wxWindow* parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style);
	~CGameListCtrl();

	void Update();

	void BrowseForDirectory();
	const GameListItem *GetSelectedISO();
	const GameListItem *GetISO(size_t index) const;

	enum
	{
		COLUMN_DUMMY = 0,
		COLUMN_PLATFORM,
		COLUMN_BANNER,
		COLUMN_TITLE,
		COLUMN_NOTES,
		COLUMN_COUNTRY,
		COLUMN_SIZE,
		COLUMN_EMULATION_STATE,
		NUMBER_OF_COLUMN
	};

private:

	std::vector<int> m_FlagImageIndex;
	std::vector<int> m_PlatformImageIndex;
	std::vector<int> m_EmuStateImageIndex;
	std::vector<GameListItem*> m_ISOFiles;

	void ClearIsoFiles()
	{
		while (!m_ISOFiles.empty())	// so lazy
		{
			delete m_ISOFiles.back();
			m_ISOFiles.pop_back();
		}
	}

	int last_column;
	int last_sort;
	wxSize lastpos;
	wxEmuStateTip *toolTip;
	void InitBitmaps();
	void InsertItemInReportView(long _Index);
	void SetBackgroundColor();
	void ScanForISOs();
	
	DECLARE_EVENT_TABLE()

	// events
	void OnLeftClick(wxMouseEvent& event);
	void OnRightClick(wxMouseEvent& event);
	void OnMouseMotion(wxMouseEvent& event);
	void OnColumnClick(wxListEvent& event);
	void OnColBeginDrag(wxListEvent& event);
	void OnKeyPress(wxListEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnProperties(wxCommandEvent& event);
	void OnWiki(wxCommandEvent& event);
	void OnOpenContainingFolder(wxCommandEvent& event);
	void OnOpenSaveFolder(wxCommandEvent& event);
	void OnExportSave(wxCommandEvent& event);
	void OnSetDefaultGCM(wxCommandEvent& event);
	void OnDeleteGCM(wxCommandEvent& event);
	void OnCompressGCM(wxCommandEvent& event);
	void OnMultiCompressGCM(wxCommandEvent& event);
	void OnMultiDecompressGCM(wxCommandEvent& event);
	void OnInstallWAD(wxCommandEvent& event);
	void OnDropFiles(wxDropFilesEvent& event);

	void CompressSelection(bool _compress);
	void AutomaticColumnWidth();
	void UnselectAll();

	static size_t m_currentItem;
	static std::string m_currentFilename;
	static size_t m_numberItem;
	static void CompressCB(const char* text, float percent, void* arg);
	static void MultiCompressCB(const char* text, float percent, void* arg);
};

#endif

