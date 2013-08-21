// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _TELEVISION_H
#define _TELEVISION_H

#include "VideoCommon.h"

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11PixelShader;

namespace DX11
{

class Television
{

public:

	Television();

	void Init();
	void Shutdown();

	// Submit video data to be drawn. This will change the current state of the
	// TV. xfbAddr points to YUYV data stored in GameCube/Wii RAM, but the XFB
	// may be virtualized when rendering so the RAM may not actually be read.
	void Submit(u32 xfbAddr, u32 width, u32 height);

	// Render the current state of the TV.
	void Render();

private:

	// Properties of last Submit call
	u32 m_curAddr;
	u32 m_curWidth;
	u32 m_curHeight;

	// Used for real XFB mode

	ID3D11Texture2D* m_yuyvTexture;
	ID3D11ShaderResourceView* m_yuyvTextureSRV;
	ID3D11PixelShader* m_pShader;

};

}

#endif
