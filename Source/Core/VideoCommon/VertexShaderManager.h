// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "VideoCommon/ConstantManager.h"

class PointerWrap;

void UpdateProjectionHack(int iParams[], std::string sParams[]);

// The non-API dependent parts.
class VertexShaderManager
{
public:
	static void Init();
	static void Dirty();
	static void Shutdown();
	static void DoState(PointerWrap &p);

	// constant management
	static void SetConstants();

	static void InvalidateXFRange(int start, int end);
	static void SetTexMatrixChangedA(u32 value);
	static void SetTexMatrixChangedB(u32 value);
	static void SetViewportChanged();
	static void SetProjectionChanged();
	static void SetMaterialColorChanged(int index);

	static void TranslateView(float x, float y, float z = 0.0f);
	static void RotateView(float x, float y);
	static void ResetView();

	// data: 3 floats representing the X, Y and Z vertex model coordinates and the posmatrix index.
	// out:  4 floats which will be initialized with the corresponding clip space coordinates
	// NOTE: g_fProjectionMatrix must be up to date when this is called
	//       (i.e. VertexShaderManager::SetConstants needs to be called before using this!)
	static void TransformToClipSpace(const float* data, float* out, u32 mtxIdx);

	static VertexShaderConstants constants;
	static bool dirty;
};
