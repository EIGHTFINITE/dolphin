// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// ---------------------------------------------------------------------------------------------
// GC graphics pipeline
// ---------------------------------------------------------------------------------------------
// 3d commands are issued through the fifo. The gpu draws to the 2MB EFB.
// The efb can be copied back into ram in two forms: as textures or as XFB.
// The XFB is the region in RAM that the VI chip scans out to the television.
// So, after all rendering to EFB is done, the image is copied into one of two XFBs in RAM.
// Next frame, that one is scanned out and the other one gets the copy. = double buffering.
// ---------------------------------------------------------------------------------------------


#include "RenderBase.h"
#include "Atomic.h"
#include "BPMemory.h"
#include "CommandProcessor.h"
#include "CPMemory.h"
#include "MainBase.h"
#include "VideoConfig.h"
#include "FramebufferManagerBase.h"
#include "TextureCacheBase.h"
#include "Fifo.h"
#include "OpcodeDecoding.h"
#include "Timer.h"
#include "StringUtil.h"
#include "Host.h"
#include "XFMemory.h"
#include "FifoPlayer/FifoRecorder.h"
#include "AVIDump.h"
#include "VertexShaderManager.h"

#include <cmath>
#include <string>

// TODO: Move these out of here.
int frameCount;
int OSDChoice, OSDTime;

Renderer *g_renderer = NULL;

std::mutex Renderer::s_criticalScreenshot;
std::string Renderer::s_sScreenshotName;

volatile bool Renderer::s_bScreenshot;

// The framebuffer size
int Renderer::s_target_width;
int Renderer::s_target_height;

// TODO: Add functionality to reinit all the render targets when the window is resized.
int Renderer::s_backbuffer_width;
int Renderer::s_backbuffer_height;

TargetRectangle Renderer::target_rc;

int Renderer::s_LastEFBScale;

bool Renderer::s_skipSwap;
bool Renderer::XFBWrited;
bool Renderer::s_EnableDLCachingAfterRecording;

unsigned int Renderer::prev_efb_format = (unsigned int)-1;
unsigned int Renderer::efb_scale_numeratorX = 1;
unsigned int Renderer::efb_scale_numeratorY = 1;
unsigned int Renderer::efb_scale_denominatorX = 1;
unsigned int Renderer::efb_scale_denominatorY = 1;
unsigned int Renderer::ssaa_multiplier = 1;


Renderer::Renderer()
	: frame_data()
	, bLastFrameDumped(false)
{
	UpdateActiveConfig();
	TextureCache::OnConfigChanged(g_ActiveConfig);

#if defined _WIN32 || defined HAVE_LIBAV
	bAVIDumping = false;
#endif

	OSDChoice = 0;
	OSDTime = 0;
}

Renderer::~Renderer()
{
	// invalidate previous efb format
	prev_efb_format = (unsigned int)-1;

	efb_scale_numeratorX = efb_scale_numeratorY = efb_scale_denominatorX = efb_scale_denominatorY = ssaa_multiplier = 1;

#if defined _WIN32 || defined HAVE_LIBAV
	if (g_ActiveConfig.bDumpFrames && bLastFrameDumped && bAVIDumping)
		AVIDump::Stop();
#else
	if (pFrameDump.IsOpen())
		pFrameDump.Close();
#endif
}

void Renderer::RenderToXFB(u32 xfbAddr, u32 fbWidth, u32 fbHeight, const EFBRectangle& sourceRc, float Gamma)
{
	CheckFifoRecording();

	if (!fbWidth || !fbHeight)
		return;

	s_skipSwap = g_bSkipCurrentFrame;

	VideoFifo_CheckEFBAccess();
	VideoFifo_CheckSwapRequestAt(xfbAddr, fbWidth, fbHeight);
	XFBWrited = true;

	if (g_ActiveConfig.bUseXFB)
	{
		FramebufferManagerBase::CopyToXFB(xfbAddr, fbWidth, fbHeight, sourceRc,Gamma);
	}
	else
	{
		// XXX: Without the VI, how would we know what kind of field this is? So
		// just use progressive.
		g_renderer->Swap(xfbAddr, FIELD_PROGRESSIVE, fbWidth, fbHeight,sourceRc,Gamma);
		Common::AtomicStoreRelease(s_swapRequested, false);
	}
}

int Renderer::EFBToScaledX(int x)
{
	switch (g_ActiveConfig.iEFBScale)
	{
	case SCALE_AUTO: // fractional
			return (int)ssaa_multiplier * FramebufferManagerBase::ScaleToVirtualXfbWidth(x, s_backbuffer_width);

		default:
			return x * (int)ssaa_multiplier * (int)efb_scale_numeratorX / (int)efb_scale_denominatorX;
	};
}

int Renderer::EFBToScaledY(int y)
{
	switch (g_ActiveConfig.iEFBScale)
	{
		case SCALE_AUTO: // fractional
			return (int)ssaa_multiplier * FramebufferManagerBase::ScaleToVirtualXfbHeight(y, s_backbuffer_height);

		default:
			return y * (int)ssaa_multiplier * (int)efb_scale_numeratorY / (int)efb_scale_denominatorY;
	};
}

void Renderer::CalculateTargetScale(int x, int y, int &scaledX, int &scaledY)
{
	if (g_ActiveConfig.iEFBScale == SCALE_AUTO || g_ActiveConfig.iEFBScale == SCALE_AUTO_INTEGRAL)
	{
		scaledX = x;
		scaledY = y;
	}
	else
	{
		scaledX = x * (int)efb_scale_numeratorX / (int)efb_scale_denominatorX;
		scaledY = y * (int)efb_scale_numeratorY / (int)efb_scale_denominatorY;
	}
}

// return true if target size changed
bool Renderer::CalculateTargetSize(unsigned int framebuffer_width, unsigned int framebuffer_height, int multiplier)
{
	int newEFBWidth, newEFBHeight;

	// TODO: Ugly. Clean up
	switch (s_LastEFBScale)
	{
		case 2: // 1x
			efb_scale_numeratorX = efb_scale_numeratorY = 1;
			efb_scale_denominatorX = efb_scale_denominatorY = 1;
			break;

		case 3: // 1.5x
			efb_scale_numeratorX = efb_scale_numeratorY = 3;
			efb_scale_denominatorX = efb_scale_denominatorY = 2;
			break;

		case 4: // 2x
			efb_scale_numeratorX = efb_scale_numeratorY = 2;
			efb_scale_denominatorX = efb_scale_denominatorY = 1;
			break;

		case 5: // 2.5x
			efb_scale_numeratorX = efb_scale_numeratorY = 5;
			efb_scale_denominatorX = efb_scale_denominatorY = 2;
			break;

		case 6: // 3x
			efb_scale_numeratorX = efb_scale_numeratorY = 3;
			efb_scale_denominatorX = efb_scale_denominatorY = 1;
			break;

		case 7: // 4x
			efb_scale_numeratorX = efb_scale_numeratorY = 4;
			efb_scale_denominatorX = efb_scale_denominatorY = 1;
			break;

		default: // fractional & integral handled later
			break;
	}

	switch (s_LastEFBScale)
	{
		case 0: // fractional
		case 1: // integral
			newEFBWidth = FramebufferManagerBase::ScaleToVirtualXfbWidth(EFB_WIDTH, framebuffer_width);
			newEFBHeight = FramebufferManagerBase::ScaleToVirtualXfbHeight(EFB_HEIGHT, framebuffer_height);

			if (s_LastEFBScale == 1)
			{
				newEFBWidth = ((newEFBWidth-1) / EFB_WIDTH + 1) * EFB_WIDTH;
				newEFBHeight = ((newEFBHeight-1) / EFB_HEIGHT + 1) * EFB_HEIGHT;
			}
			efb_scale_numeratorX = newEFBWidth;
			efb_scale_denominatorX = EFB_WIDTH;
			efb_scale_numeratorY = newEFBHeight;
			efb_scale_denominatorY = EFB_HEIGHT;
			break;

		default:
			CalculateTargetScale(EFB_WIDTH, EFB_HEIGHT, newEFBWidth, newEFBHeight);
			break;
	}

	newEFBWidth *= multiplier;
	newEFBHeight *= multiplier;
	ssaa_multiplier = multiplier;

	if (newEFBWidth != s_target_width || newEFBHeight != s_target_height)
	{
		s_target_width  = newEFBWidth;
		s_target_height = newEFBHeight;
		VertexShaderManager::SetViewportChanged();
		return true;
	}
	return false;
}

void Renderer::SetScreenshot(const char *filename)
{
	std::lock_guard<std::mutex> lk(s_criticalScreenshot);
	s_sScreenshotName = filename;
	s_bScreenshot = true;
}

// Create On-Screen-Messages
void Renderer::DrawDebugText()
{
	// OSD Menu messages
	if (OSDChoice > 0)
	{
		OSDTime = Common::Timer::GetTimeMs() + 3000;
		OSDChoice = -OSDChoice;
	}

	if ((u32)OSDTime <= Common::Timer::GetTimeMs())
		return;

	const char* res_text = "";
	switch (g_ActiveConfig.iEFBScale)
	{
	case SCALE_AUTO:
		res_text = "Auto (fractional)";
		break;
	case SCALE_AUTO_INTEGRAL:
		res_text = "Auto (integral)";
		break;
	case SCALE_1X:
		res_text = "Native";
		break;
	case SCALE_1_5X:
		res_text = "1.5x";
		break;
	case SCALE_2X:
		res_text = "2x";
		break;
	case SCALE_2_5X:
		res_text = "2.5x";
		break;
	case SCALE_3X:
		res_text = "3x";
		break;
	case SCALE_4X:
		res_text = "4x";
		break;
	}

	const char* ar_text = "";
	switch(g_ActiveConfig.iAspectRatio)
	{
	case ASPECT_AUTO:
		ar_text = "Auto";
		break;
	case ASPECT_FORCE_16_9:
		ar_text = "16:9";
		break;
	case ASPECT_FORCE_4_3:
		ar_text = "4:3";
		break;
	case ASPECT_STRETCH:
		ar_text = "Stretch";
		break;
	}

	const char* const efbcopy_text = g_ActiveConfig.bEFBCopyEnable ?
		(g_ActiveConfig.bCopyEFBToTexture ? "to Texture" : "to RAM") : "Disabled";

	// The rows
	const std::string lines[] =
	{
		std::string("3: Internal Resolution: ") + res_text,
		std::string("4: Aspect Ratio: ") + ar_text + (g_ActiveConfig.bCrop ? " (crop)" : ""),
		std::string("5: Copy EFB: ") + efbcopy_text,
		std::string("6: Fog: ") + (g_ActiveConfig.bDisableFog ? "Disabled" : "Enabled"),
	};

	enum { lines_count = sizeof(lines)/sizeof(*lines) };

	std::string final_yellow, final_cyan;

	// If there is more text than this we will have a collision
	if (g_ActiveConfig.bShowFPS)
	{
		final_yellow = final_cyan = "\n\n";
	}

	// The latest changed setting in yellow
	for (int i = 0; i != lines_count; ++i)
	{
		if (OSDChoice == -i - 1)
			final_yellow += lines[i];
		final_yellow += '\n';
	}

	// The other settings in cyan
	for (int i = 0; i != lines_count; ++i)
	{
		if (OSDChoice != -i - 1)
			final_cyan += lines[i];
		final_cyan += '\n';
	}

	// Render a shadow
	g_renderer->RenderText(final_cyan.c_str(), 21, 21, 0xDD000000);
	g_renderer->RenderText(final_yellow.c_str(), 21, 21, 0xDD000000);
	//and then the text
	g_renderer->RenderText(final_cyan.c_str(), 20, 20, 0xFF00FFFF);
	g_renderer->RenderText(final_yellow.c_str(), 20, 20, 0xFFFFFF00);
}

// TODO: remove
extern bool g_aspect_wide;

void Renderer::UpdateDrawRectangle(int backbuffer_width, int backbuffer_height)
{
	float FloatGLWidth = (float)backbuffer_width;
	float FloatGLHeight = (float)backbuffer_height;
	float FloatXOffset = 0;
	float FloatYOffset = 0;

	// The rendering window size
	const float WinWidth = FloatGLWidth;
	const float WinHeight = FloatGLHeight;

	// Handle aspect ratio.
	// Default to auto.
	bool use16_9 = g_aspect_wide;

	// Update aspect ratio hack values
	// Won't take effect until next frame
	// Don't know if there is a better place for this code so there isn't a 1 frame delay
	if ( g_ActiveConfig.bWidescreenHack )
	{
		float source_aspect = use16_9 ? (16.0f / 9.0f) : (4.0f / 3.0f);
		float target_aspect;

		switch ( g_ActiveConfig.iAspectRatio )
		{
		case ASPECT_FORCE_16_9 :
			target_aspect = 16.0f / 9.0f;
			break;
		case ASPECT_FORCE_4_3 :
			target_aspect = 4.0f / 3.0f;
			break;
		case ASPECT_STRETCH :
			target_aspect = WinWidth / WinHeight;
			break;
		default :
			// ASPECT_AUTO == no hacking
			target_aspect = source_aspect;
			break;
		}

		float adjust = source_aspect / target_aspect;
		if ( adjust > 1 )
		{
			// Vert+
			g_Config.fAspectRatioHackW = 1;
			g_Config.fAspectRatioHackH = 1/adjust;
		}
		else
		{
			// Hor+
			g_Config.fAspectRatioHackW = adjust;
			g_Config.fAspectRatioHackH = 1;
		}
	}
	else
	{
		// Hack is disabled
		g_Config.fAspectRatioHackW = 1;
		g_Config.fAspectRatioHackH = 1;
	}

	// Check for force-settings and override.
	if (g_ActiveConfig.iAspectRatio == ASPECT_FORCE_16_9)
		use16_9 = true;
	else if (g_ActiveConfig.iAspectRatio == ASPECT_FORCE_4_3)
		use16_9 = false;

	if (g_ActiveConfig.iAspectRatio != ASPECT_STRETCH)
	{
		// The rendering window aspect ratio as a proportion of the 4:3 or 16:9 ratio
		float Ratio = (WinWidth / WinHeight) / (!use16_9 ? (4.0f / 3.0f) : (16.0f / 9.0f));
		// Check if height or width is the limiting factor. If ratio > 1 the picture is too wide and have to limit the width.
		if (Ratio > 1.0f)
		{
			// Scale down and center in the X direction.
			FloatGLWidth /= Ratio;
			FloatXOffset = (WinWidth - FloatGLWidth) / 2.0f;
		}
		// The window is too high, we have to limit the height
		else
		{
			// Scale down and center in the Y direction.
			FloatGLHeight *= Ratio;
			FloatYOffset = FloatYOffset + (WinHeight - FloatGLHeight) / 2.0f;
		}
	}

	// -----------------------------------------------------------------------
	// Crop the picture from 4:3 to 5:4 or from 16:9 to 16:10.
	//		Output: FloatGLWidth, FloatGLHeight, FloatXOffset, FloatYOffset
	// ------------------
	if (g_ActiveConfig.iAspectRatio != ASPECT_STRETCH && g_ActiveConfig.bCrop)
	{
		float Ratio = !use16_9 ? ((4.0f / 3.0f) / (5.0f / 4.0f)) : (((16.0f / 9.0f) / (16.0f / 10.0f)));
		// The width and height we will add (calculate this before FloatGLWidth and FloatGLHeight is adjusted)
		float IncreasedWidth = (Ratio - 1.0f) * FloatGLWidth;
		float IncreasedHeight = (Ratio - 1.0f) * FloatGLHeight;
		// The new width and height
		FloatGLWidth = FloatGLWidth * Ratio;
		FloatGLHeight = FloatGLHeight * Ratio;
		// Adjust the X and Y offset
		FloatXOffset = FloatXOffset - (IncreasedWidth * 0.5f);
		FloatYOffset = FloatYOffset - (IncreasedHeight * 0.5f);
	}

	int XOffset = (int)(FloatXOffset + 0.5f);
	int YOffset = (int)(FloatYOffset + 0.5f);
	int iWhidth = (int)ceil(FloatGLWidth);
	int iHeight = (int)ceil(FloatGLHeight);
	iWhidth -= iWhidth % 4; // ensure divisibility by 4 to make it compatible with all the video encoders
	iHeight -= iHeight % 4;

	target_rc.left = XOffset;
	target_rc.top = YOffset;
	target_rc.right = XOffset + iWhidth;
	target_rc.bottom = YOffset + iHeight;
}

void Renderer::SetWindowSize(int width, int height)
{
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;

	// Scale the window size by the EFB scale.
	CalculateTargetScale(width, height, width, height);

	Host_RequestRenderWindowSize(width, height);
}

void Renderer::CheckFifoRecording()
{
	bool wasRecording = g_bRecordFifoData;
	g_bRecordFifoData = FifoRecorder::GetInstance().IsRecording();

	if (g_bRecordFifoData)
	{
		if (!wasRecording)
		{
			// Disable display list caching because the recorder does not handle it
			s_EnableDLCachingAfterRecording = g_ActiveConfig.bDlistCachingEnable;
			g_ActiveConfig.bDlistCachingEnable = false;

			RecordVideoMemory();
		}

		FifoRecorder::GetInstance().EndFrame(CommandProcessor::fifo.CPBase, CommandProcessor::fifo.CPEnd);
	}
	else if (wasRecording)
	{
		g_ActiveConfig.bDlistCachingEnable = s_EnableDLCachingAfterRecording;
	}
}

void Renderer::RecordVideoMemory()
{
	u32 *bpMem = (u32*)&bpmem;
	u32 cpMem[256];
	u32 *xfMem = (u32*)xfmem;
	u32 *xfRegs = (u32*)&xfregs;

	memset(cpMem, 0, 256 * 4);
	FillCPMemoryArray(cpMem);

	FifoRecorder::GetInstance().SetVideoMemory(bpMem, cpMem, xfMem, xfRegs, sizeof(XFRegisters) / 4);
}

void UpdateViewport(Matrix44& vpCorrection)
{
	if (xfregs.viewport.wd != 0 && xfregs.viewport.ht != 0)
		g_renderer->UpdateViewport(vpCorrection);
}
