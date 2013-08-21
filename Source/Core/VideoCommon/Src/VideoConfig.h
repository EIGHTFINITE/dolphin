// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// IMPORTANT: UI etc should modify g_Config. Graphics code should read g_ActiveConfig.
// The reason for this is to get rid of race conditions etc when the configuration
// changes in the middle of a frame. This is done by copying g_Config to g_ActiveConfig
// at the start of every frame. Noone should ever change members of g_ActiveConfig 
// directly.

#ifndef _VIDEO_CONFIG_H_
#define _VIDEO_CONFIG_H_

#include "Common.h"
#include "VideoCommon.h"

#include <vector>
#include <string>

// Log in two categories, and save three other options in the same byte
#define CONF_LOG			1
#define CONF_PRIMLOG		2
#define CONF_SAVETARGETS	8
#define CONF_SAVESHADERS	16

enum AspectMode {
	ASPECT_AUTO = 0,
	ASPECT_FORCE_16_9 = 1,
	ASPECT_FORCE_4_3 = 2,
	ASPECT_STRETCH = 3,
};

enum EFBScale {
	SCALE_FORCE_INTEGRAL = -1,
	SCALE_AUTO,
	SCALE_AUTO_INTEGRAL,
	SCALE_1X,
	SCALE_1_5X,
	SCALE_2X,
	SCALE_2_5X,
	SCALE_3X,
	SCALE_4X,
};

class IniFile;

// NEVER inherit from this class.
struct VideoConfig
{
	VideoConfig();
	void Load(const char *ini_file);
	void GameIniLoad(const char *ini_file);
	void VerifyValidity();
	void Save(const char *ini_file);
	void GameIniSave(const char* default_ini, const char* game_ini);
	void UpdateProjectionHack();
	bool IsVSync();

	// General
	bool bVSync;

	bool bRunning;
	bool bWidescreenHack;
	int iAspectRatio;
	bool bCrop;   // Aspect ratio controls.
	bool bUseXFB;
	bool bUseRealXFB;

	// OpenCL/OpenMP
	bool bEnableOpenCL;
	bool bOMPDecoder;

	// Enhancements
	int iMultisampleMode;
	int iEFBScale;
	bool bForceFiltering;
	int iMaxAnisotropy;
	std::string sPostProcessingShader;

	// Information
	bool bShowFPS;
	bool bShowInputDisplay;
	bool bOverlayStats;
	bool bOverlayProjStats;
	bool bTexFmtOverlayEnable;
	bool bTexFmtOverlayCenter;
	bool bShowEFBCopyRegions;
	bool bLogFPSToFile;

	// Render
	bool bWireFrame;
	bool bDstAlphaPass;
	bool bDisableFog;

	// Utility
	bool bDumpTextures;
	bool bHiresTextures;
	bool bDumpEFBTarget;
	bool bDumpFrames;
	bool bUseFFV1;
	bool bFreeLook;
	bool bAnaglyphStereo;
	int iAnaglyphStereoSeparation;
	int iAnaglyphFocalAngle;
	bool b3DVision;

	// Hacks
	bool bEFBAccessEnable;
	bool bDlistCachingEnable;
	bool bPerfQueriesEnable;

	bool bEFBCopyEnable;
	bool bEFBCopyCacheEnable;
	bool bEFBEmulateFormatChanges;
	bool bCopyEFBToTexture;	
	bool bCopyEFBScaled;
	int iSafeTextureCache_ColorSamples;
	int iPhackvalue[4];
	std::string sPhackvalue[2];
	float fAspectRatioHackW, fAspectRatioHackH;
	bool bZTPSpeedHack; // The Legend of Zelda: Twilight Princess
	bool bUseBBox;
	bool bEnablePixelLighting;
	bool bHackedBufferUpload;
	bool bFastDepthCalc;

	int iLog; // CONF_ bits
	int iSaveTargetId; // TODO: Should be dropped

	//currently unused:
	int iCompileDLsLevel;

	// D3D only config, mostly to be merged into the above
	int iAdapter;

	// Debugging
	bool bEnableShaderDebugging;

	// Static config per API
	// TODO: Move this out of VideoConfig
	struct
	{
		API_TYPE APIType;

		std::vector<std::string> Adapters; // for D3D9 and D3D11
		std::vector<std::string> AAModes;
		std::vector<std::string> PPShaders; // post-processing shaders

		bool bUseRGBATextures; // used for D3D11 in TextureCache
		bool bUseMinimalMipCount;
		bool bSupports3DVision;
		bool bSupportsDualSourceBlend; // only supported by D3D11 and OpenGL
		bool bSupportsFormatReinterpretation;
		bool bSupportsPixelLighting;
		bool bSupportsPrimitiveRestart;
		bool bSupportsSeparateAlphaFunction;
		bool bSupportsGLSLUBO; // needed by PixelShaderGen, so must stay in VideoCommon
		bool bSupportsEarlyZ; // needed by PixelShaderGen, so must stay in VideoCommon
	} backend_info;

	// Utility
	bool RealXFBEnabled() const { return bUseXFB && bUseRealXFB; }
	bool VirtualXFBEnabled() const { return bUseXFB && !bUseRealXFB; }
	bool EFBCopiesToTextureEnabled() const { return bEFBCopyEnable && bCopyEFBToTexture; }
	bool EFBCopiesToRamEnabled() const { return bEFBCopyEnable && !bCopyEFBToTexture; }
};

extern VideoConfig g_Config;
extern VideoConfig g_ActiveConfig;

// Called every frame.
void UpdateActiveConfig();

#endif  // _VIDEO_CONFIG_H_
