// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma	once

#include <d3d11.h>
#include <MathUtil.h>

namespace DX11
{

namespace D3D
{
	// Font creation flags
	#define D3DFONT_BOLD        0x0001
	#define D3DFONT_ITALIC      0x0002

	// Font rendering flags
	#define D3DFONT_CENTERED    0x0001

	class CD3DFont
	{
		ID3D11ShaderResourceView* m_pTexture;
		ID3D11Buffer* m_pVB;
		ID3D11InputLayout* m_InputLayout;
		ID3D11PixelShader* m_pshader;
		ID3D11VertexShader* m_vshader;
		ID3D11BlendState* m_blendstate;
		ID3D11RasterizerState* m_raststate;
		const int m_dwTexWidth;
		const int m_dwTexHeight;
		unsigned int m_LineHeight;
		float m_fTexCoords[128-32][4];

	public:
		CD3DFont();
		// 2D text drawing function
		// Initializing and destroying device-dependent objects
		int Init();
		int Shutdown();
		int DrawTextScaled(float x, float y,
							float size,
							float spacing, u32 dwColor,
							const char* strText);
	};

	extern CD3DFont font;

	void InitUtils();
	void ShutdownUtils();

	void SetPointCopySampler();
	void SetLinearCopySampler();

	void drawShadedTexQuad(ID3D11ShaderResourceView* texture,
						const D3D11_RECT* rSource,
						int SourceWidth,
						int SourceHeight,
						ID3D11PixelShader* PShader,
						ID3D11VertexShader* VShader,
						ID3D11InputLayout* layout,
						float Gamma = 1.0f);
	void drawShadedTexSubQuad(ID3D11ShaderResourceView* texture,
							const MathUtil::Rectangle<float>* rSource,
							int SourceWidth,
							int SourceHeight,
							const MathUtil::Rectangle<float>* rDest,
							ID3D11PixelShader* PShader,
							ID3D11VertexShader* Vshader,
							ID3D11InputLayout* layout,
							float Gamma = 1.0f);
	void drawClearQuad(u32 Color, float z, ID3D11PixelShader* PShader, ID3D11VertexShader* Vshader, ID3D11InputLayout* layout);
	void drawColorQuad(u32 Color, float x1, float y1, float x2, float y2);
}

}