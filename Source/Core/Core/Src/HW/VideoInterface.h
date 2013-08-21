// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _VIDEOINTERFACE_H
#define _VIDEOINTERFACE_H

#include "Common.h"
class PointerWrap;

namespace VideoInterface
{
// NTSC is 60 FPS, right?
// Wrong, it's about 59.94 FPS. The NTSC engineers had to slightly lower
// the field rate from 60 FPS when they added color to the standard.
// This was done to prevent analog interference between the video and
// audio signals. PAL has no similar reduction; it is exactly 50 FPS.
//#define NTSC_FIELD_RATE		(60.0f / 1.001f)
#define NTSC_FIELD_RATE		60
#define NTSC_LINE_COUNT		525
// These line numbers indicate the beginning of the "active video" in a frame.
// An NTSC frame has the lower field first followed by the upper field.
// TODO: Is this true for PAL-M? Is this true for EURGB60?
#define NTSC_LOWER_BEGIN	21
#define NTSC_UPPER_BEGIN	283

//#define PAL_FIELD_RATE		50.0f
#define PAL_FIELD_RATE		50
#define PAL_LINE_COUNT		625
// These line numbers indicate the beginning of the "active video" in a frame.
// A PAL frame has the upper field first followed by the lower field.
#define PAL_UPPER_BEGIN		23
#define PAL_LOWER_BEGIN		336

// VI Internal Hardware Addresses
enum
{
	VI_VERTICAL_TIMING					= 0x00,
	VI_CONTROL_REGISTER					= 0x02,
	VI_HORIZONTAL_TIMING_0_HI			= 0x04,
	VI_HORIZONTAL_TIMING_0_LO			= 0x06,
	VI_HORIZONTAL_TIMING_1_HI			= 0x08,
	VI_HORIZONTAL_TIMING_1_LO			= 0x0a,
	VI_VBLANK_TIMING_ODD_HI				= 0x0c,
	VI_VBLANK_TIMING_ODD_LO				= 0x0e,
	VI_VBLANK_TIMING_EVEN_HI			= 0x10,
	VI_VBLANK_TIMING_EVEN_LO			= 0x12,
	VI_BURST_BLANKING_ODD_HI			= 0x14,
	VI_BURST_BLANKING_ODD_LO			= 0x16,
	VI_BURST_BLANKING_EVEN_HI			= 0x18,
	VI_BURST_BLANKING_EVEN_LO			= 0x1a,
	VI_FB_LEFT_TOP_HI					= 0x1c, // FB_LEFT_TOP is first half of XFB info
	VI_FB_LEFT_TOP_LO					= 0x1e,
	VI_FB_RIGHT_TOP_HI					= 0x20, // FB_RIGHT_TOP is only used in 3D mode
	VI_FB_RIGHT_TOP_LO					= 0x22,
	VI_FB_LEFT_BOTTOM_HI				= 0x24, // FB_LEFT_BOTTOM is second half of XFB info
	VI_FB_LEFT_BOTTOM_LO				= 0x26,
	VI_FB_RIGHT_BOTTOM_HI				= 0x28, // FB_RIGHT_BOTTOM is only used in 3D mode
	VI_FB_RIGHT_BOTTOM_LO				= 0x2a,
	VI_VERTICAL_BEAM_POSITION			= 0x2c,
	VI_HORIZONTAL_BEAM_POSITION			= 0x2e,
	VI_PRERETRACE_HI					= 0x30,
	VI_PRERETRACE_LO					= 0x32,
	VI_POSTRETRACE_HI					= 0x34,
	VI_POSTRETRACE_LO					= 0x36,
	VI_DISPLAY_INTERRUPT_2_HI			= 0x38,
	VI_DISPLAY_INTERRUPT_2_LO			= 0x3a,
	VI_DISPLAY_INTERRUPT_3_HI			= 0x3c,
	VI_DISPLAY_INTERRUPT_3_LO			= 0x3e,
	VI_DISPLAY_LATCH_0_HI				= 0x40,
	VI_DISPLAY_LATCH_0_LO				= 0x42,
	VI_DISPLAY_LATCH_1_HI				= 0x44,
	VI_DISPLAY_LATCH_1_LO				= 0x46,
	VI_HSCALEW							= 0x48,
	VI_HSCALER							= 0x4a,
	VI_FILTER_COEF_0_HI					= 0x4c,
	VI_FILTER_COEF_0_LO					= 0x4e,
	VI_FILTER_COEF_1_HI					= 0x50,
	VI_FILTER_COEF_1_LO					= 0x52,
	VI_FILTER_COEF_2_HI					= 0x54,
	VI_FILTER_COEF_2_LO					= 0x56,
	VI_FILTER_COEF_3_HI					= 0x58,
	VI_FILTER_COEF_3_LO					= 0x5a,
	VI_FILTER_COEF_4_HI					= 0x5c,
	VI_FILTER_COEF_4_LO					= 0x5e,
	VI_FILTER_COEF_5_HI					= 0x60,
	VI_FILTER_COEF_5_LO					= 0x62,
	VI_FILTER_COEF_6_HI					= 0x64,
	VI_FILTER_COEF_6_LO					= 0x66,
	VI_UNK_AA_REG_HI					= 0x68,
	VI_UNK_AA_REG_LO					= 0x6a,
	VI_CLOCK							= 0x6c,
	VI_DTV_STATUS						= 0x6e,
	VI_FBWIDTH							= 0x70,
	VI_BORDER_BLANK_END					= 0x72, // Only used in debug video mode
	VI_BORDER_BLANK_START				= 0x74, // Only used in debug video mode
	//VI_INTERLACE						= 0x850, // ??? MYSTERY OLD CODE
};

union UVIVerticalTimingRegister
{
	u16 Hex;
	struct
	{
		u16 EQU	:	 4; // Equalization pulse in half lines
		u16 ACV	:	10; // Active video in lines per field (seems always zero)
		u16		:	 2;
	};
	UVIVerticalTimingRegister(u16 _hex) { Hex = _hex;}
	UVIVerticalTimingRegister() { Hex = 0;}
};

union UVIDisplayControlRegister
{
	u16 Hex;
	struct
	{
		u16 ENB	:	1; // Enables video timing generation and data request
		u16 RST	:	1; // Clears all data requests and puts VI into its idle state
		u16 NIN	:	1; // 0: Interlaced, 1: Non-Interlaced: top field drawn at field rate and bottom field is not displayed
		u16 DLR	:	1; // Selects 3D Display Mode
		u16 LE0	:	2; // Display Latch; 0: Off, 1: On for 1 field, 2: On for 2 fields, 3: Always on
		u16 LE1	:	2;
		u16 FMT	:	2; // 0: NTSC, 1: PAL, 2: MPAL, 3: Debug
		u16		:	6;
	};
	UVIDisplayControlRegister(u16 _hex) { Hex = _hex;}
	UVIDisplayControlRegister() { Hex = 0;}
};

union UVIHorizontalTiming0
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{		
		u32 HLW		:	9; // Halfline Width (W*16 = Width (720))
		u32			:	7;
		u32 HCE		:	7; // Horizontal Sync Start to Color Burst End
		u32			:	1;
		u32 HCS		:	7; // Horizontal Sync Start to Color Burst Start
		u32			:	1;
	};
};

union UVIHorizontalTiming1
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{		
		u32 HSY		:	 7; // Horizontal Sync Width
		u32 HBE640	:	 9; // Horizontal Sync Start to horizontal blank end
		u32			:	 1;
		u32 HBS640	:	 9; // Half line to horizontal blanking start
		u32			:	 6;
	};
};

// Exists for both odd and even fields
union UVIVBlankTimingRegister
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{		
		u32 PRB		:	10; // Pre-blanking in half lines
		u32			:	 6;
		u32 PSB		:	10; // Post blanking in half lines
		u32			:	 6;
	};
};

// Exists for both odd and even fields
union UVIBurstBlankingRegister
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{		
		u32 BS0		:	 5; // Field x start to burst blanking start in halflines
		u32 BE0		:	11; // Field x start to burst blanking end in halflines
		u32 BS2		:	 5; // Field x+2 start to burst blanking start in halflines
		u32 BE2		:	11; // Field x+2 start to burst blanking end in halflines
	};
};

union UVIFBInfoRegister
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{
		// TODO: mask out lower 9bits/align to 9bits???
		u32 FBB		:	24; // Base address of the framebuffer in external mem
		// POFF only seems to exist in the top reg. XOFF, unknown.
		u32 XOFF	:	 4; // Horizontal Offset of the left-most pixel within the first word of the fetched picture
		u32 POFF	:	 1; // Page offest: 1: fb address is (address>>5)
		u32 CLRPOFF	:	 3; // ? setting bit 31 clears POFF
	};
};

// VI Interrupt Register
union UVIInterruptRegister
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{		
		u32 HCT		:	11; // Horizontal Position
		u32			:	 5;
		u32 VCT		:	11; // Vertical Position
		u32			:	 1;
		u32 IR_MASK	:	 1; // Interrupt Mask Bit
		u32			:	 2;
		u32 IR_INT	:	 1; // Interrupt Status (1=Active, 0=Clear)
	};
};

union UVILatchRegister
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct  
	{		
		u32 HCT		:	11; // Horizontal Count
		u32			:	 5;
		u32 VCT		:	11; // Vertical Count
		u32			:	 4;
		u32 TRG		:	 1; // Trigger Flag
	};
};

union UVIHorizontalStepping
{
	u16 Hex;
	struct
	{
		u16 FbSteps		:	 8;
		u16 FieldSteps	:	 8;
	};
};

union UVIHorizontalScaling
{
	u16 Hex;
	struct
	{
		u16 STP		:	9; // Horizontal stepping size (U1.8 Scaler Value) (0x160 Works for 320)
		u16			:	3;
		u16 HS_EN	:	1; // Enable Horizontal Scaling
		u16			:	3;
	};
	UVIHorizontalScaling(u16 _hex) { Hex = _hex;}
	UVIHorizontalScaling() { Hex = 0;}
};

// Used for tables 0-2
union UVIFilterCoefTable3
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct
	{
		u32 Tap0	:	10;
		u32 Tap1	:	10;
		u32 Tap2	:	10;
		u32			:	 2;
	};
};

// Used for tables 3-6
union UVIFilterCoefTable4
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct
	{
		u32 Tap0	:	 8;
		u32 Tap1	:	 8;
		u32 Tap2	:	 8;
		u32 Tap3	:	 8;
	};
};

struct SVIFilterCoefTables
{
	UVIFilterCoefTable3 Tables02[3];
	UVIFilterCoefTable4 Tables36[4];
};

// Debug video mode only, probably never used in dolphin...
union UVIBorderBlankRegister
{
	u32 Hex;
	struct { u16 Lo, Hi; };
	struct
	{
		u32 HBE656	:	10; // Border Horizontal Blank End
		u32			:	11;
		u32 HBS656	:	10; // Border Horizontal Blank start
		u32 BRDR_EN	:	 1; // Border Enable
	};
};

// ntsc-j and component cable bits
union UVIDTVStatus
{
	u16 Hex;
	struct 
	{
		u16 component_plugged	: 1;
		u16 ntsc_j				: 1;
		u16						:14;
	};
};

	// urgh, ugly externs.
	extern u32 TargetRefreshRate;

	// For BS2 HLE
	void Preset(bool _bNTSC);

	void Init();
	void SetRegionReg(char region);
	void DoState(PointerWrap &p);

	void Read8(u8& _uReturnValue, const u32 _uAddress);
	void Read16(u16& _uReturnValue, const u32 _uAddress);
	void Read32(u32& _uReturnValue, const u32 _uAddress);

	void Write16(const u16 _uValue, const u32 _uAddress);
	void Write32(const u32 _uValue, const u32 _uAddress);

	// returns a pointer to the current visible xfb
	u8* GetXFBPointerTop();
	u8* GetXFBPointerBottom();

	// Update and draw framebuffer
	void Update();

	// UpdateInterrupts: check if we have to generate a new VI Interrupt
	void UpdateInterrupts();

	// Change values pertaining to video mode
	void UpdateParameters();

	unsigned int GetTicksPerLine();
	unsigned int GetTicksPerFrame();

	int GetNumFields();
};

#endif // _VIDEOINTERFACE_H
