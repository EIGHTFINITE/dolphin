// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/srchctrl.h>
#include <wx/textctrl.h>

#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"
#include "Common/SymbolDB.h"
#include "Core/ConfigManager.h"
#include "Core/Debugger/PPCDebugInterface.h"
#include "Core/HW/DSP.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "DolphinWX/Globals.h"
#include "DolphinWX/WxUtils.h"
#include "DolphinWX/Debugger/MemoryView.h"
#include "DolphinWX/Debugger/MemoryWindow.h"

enum
{
	IDM_MEM_ADDRBOX,
	IDM_SYMBOLLIST,
	IDM_SETVALBUTTON,
	IDM_DUMP_MEMORY,
	IDM_DUMP_MEM2,
	IDM_DUMP_FAKEVMEM,
	IDM_VALBOX,
	IDM_U8,
	IDM_U16,
	IDM_U32,
	IDM_SEARCH,
	IDM_ASCII,
	IDM_HEX
};

BEGIN_EVENT_TABLE(CMemoryWindow, wxPanel)
	EVT_LISTBOX(IDM_SYMBOLLIST,     CMemoryWindow::OnSymbolListChange)
	EVT_HOST_COMMAND(wxID_ANY,      CMemoryWindow::OnHostMessage)
	EVT_BUTTON(IDM_SETVALBUTTON,    CMemoryWindow::SetMemoryValue)
	EVT_BUTTON(IDM_DUMP_MEMORY,     CMemoryWindow::OnDumpMemory)
	EVT_BUTTON(IDM_DUMP_MEM2,       CMemoryWindow::OnDumpMem2)
	EVT_BUTTON(IDM_DUMP_FAKEVMEM,   CMemoryWindow::OnDumpFakeVMEM)
	EVT_CHECKBOX(IDM_U8,            CMemoryWindow::U8)
	EVT_CHECKBOX(IDM_U16,           CMemoryWindow::U16)
	EVT_CHECKBOX(IDM_U32,           CMemoryWindow::U32)
	EVT_BUTTON(IDM_SEARCH,          CMemoryWindow::onSearch)
	EVT_CHECKBOX(IDM_ASCII,         CMemoryWindow::onAscii)
	EVT_CHECKBOX(IDM_HEX,           CMemoryWindow::onHex)
END_EVENT_TABLE()

CMemoryWindow::CMemoryWindow(wxWindow* parent, wxWindowID id,
		const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxPanel(parent, id, pos, size, style, name)
{
	DebugInterface* di = &PowerPC::debug_interface;

	memview = new CMemoryView(di, this);

	addrbox = new wxSearchCtrl(this, IDM_MEM_ADDRBOX);
	addrbox->Bind(wxEVT_TEXT, &CMemoryWindow::OnAddrBoxChange, this);
	addrbox->SetDescriptiveText(_("Search Address"));

	valbox = new wxTextCtrl(this, IDM_VALBOX, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	valbox->Bind(wxEVT_TEXT_ENTER, &CMemoryWindow::SetMemoryValueFromValBox, this);

	wxGridSizer* const search_sizer = new wxGridSizer(1);
	search_sizer->Add(addrbox);
	search_sizer->Add(valbox, 0, wxEXPAND);
	search_sizer->Add(new wxButton(this, IDM_SETVALBUTTON, _("Set Value")));

	wxGridSizer* const dump_sizer = new wxGridSizer(1);
	dump_sizer->Add(new wxButton(this, IDM_DUMP_MEMORY, _("Dump MRAM")), 0, wxEXPAND);
	dump_sizer->Add(new wxButton(this, IDM_DUMP_MEM2, _("Dump EXRAM")), 0, wxEXPAND);
	if (!SConfig::GetInstance().bMMU)
		dump_sizer->Add(new wxButton(this, IDM_DUMP_FAKEVMEM, _("Dump FakeVMEM")), 0, wxEXPAND);

	wxStaticBoxSizer* const sizerSearchType = new wxStaticBoxSizer(wxVERTICAL, this, _("Search"));
	sizerSearchType->Add(btnSearch = new wxButton(this, IDM_SEARCH, _("Search")));
	sizerSearchType->Add(chkAscii = new wxCheckBox(this, IDM_ASCII, "Ascii "));
	sizerSearchType->Add(chkHex = new wxCheckBox(this, IDM_HEX, _("Hex")));

	wxStaticBoxSizer* const sizerDataTypes = new wxStaticBoxSizer(wxVERTICAL, this, _("Data Type"));
	sizerDataTypes->SetMinSize(74, 40);
	sizerDataTypes->Add(chk8 = new wxCheckBox(this, IDM_U8, "U8"));
	sizerDataTypes->Add(chk16 = new wxCheckBox(this, IDM_U16, "U16"));
	sizerDataTypes->Add(chk32 = new wxCheckBox(this, IDM_U32, "U32"));

	wxBoxSizer* const sizerRight = new wxBoxSizer(wxVERTICAL);
	sizerRight->Add(search_sizer);
	sizerRight->AddSpacer(5);
	sizerRight->Add(dump_sizer);
	sizerRight->Add(sizerSearchType);
	sizerRight->Add(sizerDataTypes);

	wxBoxSizer* const sizerBig = new wxBoxSizer(wxHORIZONTAL);
	sizerBig->Add(memview, 20, wxEXPAND);
	sizerBig->Add(sizerRight, 0, wxEXPAND | wxALL, 3);

	SetSizer(sizerBig);
	chkHex->SetValue(1); //Set defaults
	chk8->SetValue(1);

	sizerRight->Fit(this);
	sizerBig->Fit(this);
}

void CMemoryWindow::Save(IniFile& ini) const
{
	// Prevent these bad values that can happen after a crash or hanging
	if (GetPosition().x != -32000 && GetPosition().y != -32000)
	{
		IniFile::Section* mem_window = ini.GetOrCreateSection("MemoryWindow");
		mem_window->Set("x", GetPosition().x);
		mem_window->Set("y", GetPosition().y);
		mem_window->Set("w", GetSize().GetWidth());
		mem_window->Set("h", GetSize().GetHeight());
	}
}

void CMemoryWindow::Load(IniFile& ini)
{
	int x, y, w, h;

	IniFile::Section* mem_window = ini.GetOrCreateSection("MemoryWindow");
	mem_window->Get("x", &x, GetPosition().x);
	mem_window->Get("y", &y, GetPosition().y);
	mem_window->Get("w", &w, GetSize().GetWidth());
	mem_window->Get("h", &h, GetSize().GetHeight());

	SetSize(x, y, w, h);
}

void CMemoryWindow::JumpToAddress(u32 _Address)
{
	memview->Center(_Address);
}

void CMemoryWindow::SetMemoryValueFromValBox(wxCommandEvent& event)
{
	SetMemoryValue(event);
	valbox->SetFocus();

}

void CMemoryWindow::SetMemoryValue(wxCommandEvent& event)
{
	if (!Memory::IsInitialized())
	{
		WxUtils::ShowErrorDialog(_("Cannot set uninitialized memory."));
		return;
	}

	std::string str_addr = WxStrToStr(addrbox->GetValue());
	std::string str_val = WxStrToStr(valbox->GetValue());
	u32 addr;
	u32 val;

	if (!TryParse(std::string("0x") + str_addr, &addr))
	{
		WxUtils::ShowErrorDialog(wxString::Format(_("Invalid address: %s"), str_addr.c_str()));
		return;
	}

	if (!TryParse(std::string("0x") + str_val, &val))
	{
		WxUtils::ShowErrorDialog(wxString::Format(_("Invalid value: %s"), str_val.c_str()));
		return;
	}

	PowerPC::HostWrite_U32(val, addr);
	memview->Refresh();
}

void CMemoryWindow::OnAddrBoxChange(wxCommandEvent& event)
{
	wxString txt = addrbox->GetValue();
	if (txt.size())
	{
		u32 addr;
		sscanf(WxStrToStr(txt).c_str(), "%08x", &addr);
		memview->Center(addr & ~3);
	}

	event.Skip();
}

void CMemoryWindow::Update()
{
	memview->Refresh();
	memview->Center(PC);
}

void CMemoryWindow::NotifyMapLoaded()
{
	symbols->Show(false); // hide it for faster filling
	symbols->Clear();
#if 0
	#ifdef _WIN32
	const FunctionDB::XFuncMap &syms = g_symbolDB.Symbols();
	for (FuntionDB::XFuncMap::iterator iter = syms.begin(); iter != syms.end(); ++iter)
	{
	int idx = symbols->Append(iter->second.name.c_str());
	symbols->SetClientData(idx, (void*)&iter->second);
	}
	#endif
#endif
	symbols->Show(true);
	Update();
}

void CMemoryWindow::OnSymbolListChange(wxCommandEvent& event)
{
	int index = symbols->GetSelection();
	if (index >= 0)
	{
		Symbol* pSymbol = static_cast<Symbol *>(symbols->GetClientData(index));
		if (pSymbol != nullptr)
		{
			memview->Center(pSymbol->address);
		}
	}
}

void CMemoryWindow::OnHostMessage(wxCommandEvent& event)
{
	switch (event.GetId())
	{
		case IDM_NOTIFY_MAP_LOADED:
			NotifyMapLoaded();
			break;
	}
}

static void DumpArray(const std::string& filename, const u8* data, size_t length)
{
	if (data)
	{
		File::IOFile f(filename, "wb");
		f.WriteBytes(data, length);
	}
}

// Write mram to file
void CMemoryWindow::OnDumpMemory( wxCommandEvent& event )
{
	DumpArray(File::GetUserPath(F_RAMDUMP_IDX), Memory::m_pRAM, Memory::REALRAM_SIZE);
}

// Write exram (aram or mem2) to file
void CMemoryWindow::OnDumpMem2( wxCommandEvent& event )
{
	if (SConfig::GetInstance().bWii)
	{
		DumpArray(File::GetUserPath(F_ARAMDUMP_IDX), Memory::m_pEXRAM, Memory::EXRAM_SIZE);
	}
	else
	{
		DumpArray(File::GetUserPath(F_ARAMDUMP_IDX), DSP::GetARAMPtr(), DSP::ARAM_SIZE);
	}
}

// Write fake vmem to file
void CMemoryWindow::OnDumpFakeVMEM( wxCommandEvent& event )
{
	DumpArray(File::GetUserPath(F_FAKEVMEMDUMP_IDX), Memory::m_pFakeVMEM, Memory::FAKEVMEM_SIZE);
}

void CMemoryWindow::U8(wxCommandEvent& event)
{
	chk16->SetValue(0);
	chk32->SetValue(0);
	memview->SetDataType(MemoryDataType::U8);
}

void CMemoryWindow::U16(wxCommandEvent& event)
{
	chk8->SetValue(0);
	chk32->SetValue(0);
	memview->SetDataType(MemoryDataType::U16);
}

void CMemoryWindow::U32(wxCommandEvent& event)
{
	chk16->SetValue(0);
	chk8->SetValue(0);
	memview->SetDataType(MemoryDataType::U32);
}

void CMemoryWindow::onSearch(wxCommandEvent& event)
{
	u8* TheRAM = nullptr;
	u32 szRAM = 0;
	switch (memview->GetMemoryType())
	{
	case 0:
	default:
		if (Memory::m_pRAM)
		{
			TheRAM = Memory::m_pRAM;
			szRAM = Memory::REALRAM_SIZE;
		}
		break;
	case 1:
		{
			u8* aram = DSP::GetARAMPtr();
			if (aram)
			{
				TheRAM = aram;
				szRAM = DSP::ARAM_SIZE;
			}
		}
		break;
	}
	//Now we have memory to look in
	//Are we looking for ASCII string, or hex?
	//memview->cu
	wxString rawData = valbox->GetValue();
	std::vector<u8> Dest; //May need a better name
	u32 size = 0;
	int pad = rawData.size()%2; //If it's uneven
	unsigned int i = 0;
	long count = 0;
	char copy[3] = {0};
	long newsize = 0;
	unsigned char *tmp2 = nullptr;
	char* tmpstr = nullptr;

	if (chkHex->GetValue())
	{
		//We are looking for hex
		//If it's uneven
		size = (rawData.size()/2) + pad;
		Dest.resize(size+32);
		newsize = rawData.size();

		if (pad)
		{
			tmpstr = new char[newsize + 2];
			memset(tmpstr, 0, newsize + 2);
			tmpstr[0] = '0';
		}
		else
		{
			tmpstr = new char[newsize + 1];
			memset(tmpstr, 0, newsize + 1);
		}
		strcat(tmpstr, WxStrToStr(rawData).c_str());
		tmp2 = &Dest.front();
		count = 0;
		for (i = 0; i < strlen(tmpstr); i++)
		{
			copy[0] = tmpstr[i];
			copy[1] = tmpstr[i+1];
			copy[2] = 0;
			int tmpint;
			sscanf(copy, "%02x", &tmpint);
			tmp2[count++] = tmpint;
			// Dest[count] should now be the hex of what the two chars were!
			// Also should add a check to make sure it's A-F only
			//sscanf(copy, "%02x", &tmp2[count++]);
			i += 1;
		}
		delete[] tmpstr;
	}
	else
	{
		//Looking for an ascii string
		size = rawData.size();
		Dest.resize(size+1);
		tmpstr = new char[size+1];

		tmp2 = &Dest.front();
		sprintf(tmpstr, "%s", WxStrToStr(rawData).c_str());

		for (i = 0; i < size; i++)
			tmp2[i] = tmpstr[i];

		delete[] tmpstr;
	}

	if (size)
	{
		unsigned char* pnt = &Dest.front();
		unsigned int k = 0;
		//grab
		wxString txt = addrbox->GetValue();
		u32 addr = 0;
		if (txt.size())
		{
			sscanf(WxStrToStr(txt).c_str(), "%08x", &addr);
		}
		i = addr+4;
		for ( ; i < szRAM; ++i)
		{
			for (k = 0; k < size; ++k)
			{
				if (i + k > szRAM) break;
				if (k > size) break;
				if (pnt[k] != TheRAM[i+k])
				{
					k = 0;
					break;
				}
			}
			if (k == size)
			{
				//Match was found
				wxMessageBox(_("A match was found. Placing viewer at the offset."));
				addrbox->SetValue(wxString::Format("%08x", i));
				//memview->curAddress = i;
				//memview->Refresh();
				OnAddrBoxChange(event);
				return;
			}
		}
		wxMessageBox(_("No match was found."));
	}
}

void CMemoryWindow::onAscii(wxCommandEvent& event)
{
	chkHex->SetValue(0);
}

void CMemoryWindow::onHex(wxCommandEvent& event)
{
	chkAscii->SetValue(0);
}
