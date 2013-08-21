// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef __REGISTERVIEW_h__
#define __REGISTERVIEW_h__

#include <wx/grid.h>

#include "Common.h"

// New register view:
// R0  0x8000000    F0   0.0000       F0_PS1 0.0000
// R1  0x8000000    F1   0.0000       F1_PS1 0.0000
// R31  0x8000000   F31  0.0000      F31_PS1 0.0000
// PC  (specials)
// LR
// CTR
// CR0-7
// FPSCR
// MSR
// SRR0
// SRR1
// Exceptions
// Interrupt Mask (PI)
// Interrupt Cause(PI)

class CRegTable : public wxGridTableBase
{
	enum {
		NUM_SPECIALS = 11,
	};

public:
	CRegTable() {
		memset(m_CachedRegs, 0, sizeof(m_CachedRegs));
		memset(m_CachedSpecialRegs, 0, sizeof(m_CachedSpecialRegs));
		memset(m_CachedFRegs, 0, sizeof(m_CachedFRegs));
		memset(m_CachedRegHasChanged, 0, sizeof(m_CachedRegHasChanged));
		memset(m_CachedSpecialRegHasChanged, 0, sizeof(m_CachedSpecialRegHasChanged));
		memset(m_CachedFRegHasChanged, 0, sizeof(m_CachedFRegHasChanged));
	}
    int GetNumberCols(void) {return 5;}
    int GetNumberRows(void) {return 32 + NUM_SPECIALS;}
	bool IsEmptyCell(int row, int col) {return row > 31 && col > 2;}
    wxString GetValue(int row, int col);
    void SetValue(int row, int col, const wxString &);
	wxGridCellAttr *GetAttr(int, int, wxGridCellAttr::wxAttrKind);
	void UpdateCachedRegs();

private:
	u32 m_CachedRegs[32];
	u32 m_CachedSpecialRegs[NUM_SPECIALS];
	u64 m_CachedFRegs[32][2];
	bool m_CachedRegHasChanged[32];
	bool m_CachedSpecialRegHasChanged[NUM_SPECIALS];
	bool m_CachedFRegHasChanged[32][2];

	DECLARE_NO_COPY_CLASS(CRegTable);
};

class CRegisterView : public wxGrid
{
public:
	CRegisterView(wxWindow* parent, wxWindowID id);
	void Update();
};

#endif
