// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cmath>
#include <cstring>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/LightingShaderGen.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderGen.h"
#include "VideoCommon/VideoConfig.h"

template<class T>
static T GenerateVertexShader(API_TYPE api_type)
{
	T out;
	const u32 components = VertexLoaderManager::g_current_components;
	// Non-uid template parameters will write to the dummy data (=> gets optimized out)
	vertex_shader_uid_data dummy_data;
	vertex_shader_uid_data* uid_data = out.template GetUidData<vertex_shader_uid_data>();
	if (uid_data != nullptr)
		memset(uid_data, 0, sizeof(*uid_data));
	else
		uid_data = &dummy_data;

	_assert_(bpmem.genMode.numtexgens == xfmem.numTexGen.numTexGens);
	_assert_(bpmem.genMode.numcolchans == xfmem.numChan.numColorChans);

	out.Write("%s", s_lighting_struct);

	// uniforms
	if (api_type == API_OPENGL)
		out.Write("layout(std140%s) uniform VSBlock {\n", g_ActiveConfig.backend_info.bSupportsBindingLayout ? ", binding = 2" : "");
	else
		out.Write("cbuffer VSBlock {\n");
	out.Write(s_shader_uniforms);
	out.Write("};\n");

	out.Write("struct VS_OUTPUT {\n");
	GenerateVSOutputMembers<T>(out, api_type, "");
	out.Write("};\n");

	uid_data->numTexGens = xfmem.numTexGen.numTexGens;
	uid_data->components = components;
	uid_data->pixel_lighting = g_ActiveConfig.bEnablePixelLighting;

	if (api_type == API_OPENGL)
	{
		out.Write("in float4 rawpos; // ATTR%d,\n", SHADER_POSITION_ATTRIB);
		if (components & VB_HAS_POSMTXIDX)
			out.Write("in int posmtx; // ATTR%d,\n", SHADER_POSMTX_ATTRIB);
		if (components & VB_HAS_NRM0)
			out.Write("in float3 rawnorm0; // ATTR%d,\n", SHADER_NORM0_ATTRIB);
		if (components & VB_HAS_NRM1)
			out.Write("in float3 rawnorm1; // ATTR%d,\n", SHADER_NORM1_ATTRIB);
		if (components & VB_HAS_NRM2)
			out.Write("in float3 rawnorm2; // ATTR%d,\n", SHADER_NORM2_ATTRIB);

		if (components & VB_HAS_COL0)
			out.Write("in float4 color0; // ATTR%d,\n", SHADER_COLOR0_ATTRIB);
		if (components & VB_HAS_COL1)
			out.Write("in float4 color1; // ATTR%d,\n", SHADER_COLOR1_ATTRIB);

		for (int i = 0; i < 8; ++i)
		{
			u32 hastexmtx = (components & (VB_HAS_TEXMTXIDX0<<i));
			if ((components & (VB_HAS_UV0<<i)) || hastexmtx)
				out.Write("in float%d tex%d; // ATTR%d,\n", hastexmtx ? 3 : 2, i, SHADER_TEXTURE0_ATTRIB + i);
		}

		if (g_ActiveConfig.backend_info.bSupportsGeometryShaders)
		{
			out.Write("out VertexData {\n");
			GenerateVSOutputMembers<T>(out, api_type, GetInterpolationQualifier(true, false));
			out.Write("} vs;\n");
		}
		else
		{
			// Let's set up attributes
			for (u32 i = 0; i < 8; ++i)
			{
				if (i < xfmem.numTexGen.numTexGens)
				{
					out.Write("%s out float3 uv%u;\n", GetInterpolationQualifier(), i);
				}
			}
			out.Write("%s out float4 clipPos;\n", GetInterpolationQualifier());
			if (g_ActiveConfig.bEnablePixelLighting)
			{
				out.Write("%s out float3 Normal;\n", GetInterpolationQualifier());
				out.Write("%s out float3 WorldPos;\n", GetInterpolationQualifier());
			}
			out.Write("%s out float4 colors_0;\n", GetInterpolationQualifier());
			out.Write("%s out float4 colors_1;\n", GetInterpolationQualifier());
		}

		out.Write("void main()\n{\n");
	}
	else // D3D
	{
		out.Write("VS_OUTPUT main(\n");

		// inputs
		if (components & VB_HAS_NRM0)
			out.Write("  float3 rawnorm0 : NORMAL0,\n");
		if (components & VB_HAS_NRM1)
			out.Write("  float3 rawnorm1 : NORMAL1,\n");
		if (components & VB_HAS_NRM2)
			out.Write("  float3 rawnorm2 : NORMAL2,\n");
		if (components & VB_HAS_COL0)
			out.Write("  float4 color0 : COLOR0,\n");
		if (components & VB_HAS_COL1)
			out.Write("  float4 color1 : COLOR1,\n");
		for (int i = 0; i < 8; ++i)
		{
			u32 hastexmtx = (components & (VB_HAS_TEXMTXIDX0<<i));
			if ((components & (VB_HAS_UV0<<i)) || hastexmtx)
				out.Write("  float%d tex%d : TEXCOORD%d,\n", hastexmtx ? 3 : 2, i, i);
		}
		if (components & VB_HAS_POSMTXIDX)
			out.Write("  int posmtx : BLENDINDICES,\n");
		out.Write("  float4 rawpos : POSITION) {\n");
	}

	out.Write("VS_OUTPUT o;\n");

	// transforms
	if (components & VB_HAS_POSMTXIDX)
	{
		out.Write("float4 pos = float4(dot(" I_TRANSFORMMATRICES"[posmtx], rawpos), dot(" I_TRANSFORMMATRICES"[posmtx+1], rawpos), dot(" I_TRANSFORMMATRICES"[posmtx+2], rawpos), 1);\n");

		if (components & VB_HAS_NRMALL)
		{
			out.Write("int normidx = posmtx & 31;\n");
			out.Write("float3 N0 = " I_NORMALMATRICES"[normidx].xyz, N1 = " I_NORMALMATRICES"[normidx+1].xyz, N2 = " I_NORMALMATRICES"[normidx+2].xyz;\n");
		}

		if (components & VB_HAS_NRM0)
			out.Write("float3 _norm0 = normalize(float3(dot(N0, rawnorm0), dot(N1, rawnorm0), dot(N2, rawnorm0)));\n");
		if (components & VB_HAS_NRM1)
			out.Write("float3 _norm1 = float3(dot(N0, rawnorm1), dot(N1, rawnorm1), dot(N2, rawnorm1));\n");
		if (components & VB_HAS_NRM2)
			out.Write("float3 _norm2 = float3(dot(N0, rawnorm2), dot(N1, rawnorm2), dot(N2, rawnorm2));\n");
	}
	else
	{
		out.Write("float4 pos = float4(dot(" I_POSNORMALMATRIX"[0], rawpos), dot(" I_POSNORMALMATRIX"[1], rawpos), dot(" I_POSNORMALMATRIX"[2], rawpos), 1.0);\n");
		if (components & VB_HAS_NRM0)
			out.Write("float3 _norm0 = normalize(float3(dot(" I_POSNORMALMATRIX"[3].xyz, rawnorm0), dot(" I_POSNORMALMATRIX"[4].xyz, rawnorm0), dot(" I_POSNORMALMATRIX"[5].xyz, rawnorm0)));\n");
		if (components & VB_HAS_NRM1)
			out.Write("float3 _norm1 = float3(dot(" I_POSNORMALMATRIX"[3].xyz, rawnorm1), dot(" I_POSNORMALMATRIX"[4].xyz, rawnorm1), dot(" I_POSNORMALMATRIX"[5].xyz, rawnorm1));\n");
		if (components & VB_HAS_NRM2)
			out.Write("float3 _norm2 = float3(dot(" I_POSNORMALMATRIX"[3].xyz, rawnorm2), dot(" I_POSNORMALMATRIX"[4].xyz, rawnorm2), dot(" I_POSNORMALMATRIX"[5].xyz, rawnorm2));\n");
	}

	if (!(components & VB_HAS_NRM0))
		out.Write("float3 _norm0 = float3(0.0, 0.0, 0.0);\n");


	out.Write("o.pos = float4(dot(" I_PROJECTION"[0], pos), dot(" I_PROJECTION"[1], pos), dot(" I_PROJECTION"[2], pos), dot(" I_PROJECTION"[3], pos));\n");

	out.Write("int4 lacc;\n"
			"float3 ldir, h, cosAttn, distAttn;\n"
			"float dist, dist2, attn;\n");

	uid_data->numColorChans = xfmem.numChan.numColorChans;
	if (xfmem.numChan.numColorChans == 0)
	{
		if (components & VB_HAS_COL0)
			out.Write("o.colors_0 = color0;\n");
		else
			out.Write("o.colors_0 = float4(1.0, 1.0, 1.0, 1.0);\n");
	}

	GenerateLightingShader<T>(out, uid_data->lighting, components, "color", "o.colors_");

	if (xfmem.numChan.numColorChans < 2)
	{
		if (components & VB_HAS_COL1)
			out.Write("o.colors_1 = color1;\n");
		else
			out.Write("o.colors_1 = o.colors_0;\n");
	}

	// transform texcoords
	out.Write("float4 coord = float4(0.0, 0.0, 1.0, 1.0);\n");
	for (unsigned int i = 0; i < xfmem.numTexGen.numTexGens; ++i)
	{
		TexMtxInfo& texinfo = xfmem.texMtxInfo[i];

		out.Write("{\n");
		out.Write("coord = float4(0.0, 0.0, 1.0, 1.0);\n");
		uid_data->texMtxInfo[i].sourcerow = xfmem.texMtxInfo[i].sourcerow;
		switch (texinfo.sourcerow)
		{
		case XF_SRCGEOM_INROW:
			out.Write("coord.xyz = rawpos.xyz;\n");
			break;
		case XF_SRCNORMAL_INROW:
			if (components & VB_HAS_NRM0)
			{
				out.Write("coord.xyz = rawnorm0.xyz;\n");
			}
			break;
		case XF_SRCCOLORS_INROW:
			_assert_(texinfo.texgentype == XF_TEXGEN_COLOR_STRGBC0 || texinfo.texgentype == XF_TEXGEN_COLOR_STRGBC1);
			break;
		case XF_SRCBINORMAL_T_INROW:
			if (components & VB_HAS_NRM1)
			{
				out.Write("coord.xyz = rawnorm1.xyz;\n");
			}
			break;
		case XF_SRCBINORMAL_B_INROW:
			if (components & VB_HAS_NRM2)
			{
				out.Write("coord.xyz = rawnorm2.xyz;\n");
			}
			break;
		default:
			_assert_(texinfo.sourcerow <= XF_SRCTEX7_INROW);
			if (components & (VB_HAS_UV0 << (texinfo.sourcerow - XF_SRCTEX0_INROW)))
				out.Write("coord = float4(tex%d.x, tex%d.y, 1.0, 1.0);\n", texinfo.sourcerow - XF_SRCTEX0_INROW, texinfo.sourcerow - XF_SRCTEX0_INROW);
			break;
		}
		// Input form of AB11 sets z element to 1.0
		uid_data->texMtxInfo[i].inputform = xfmem.texMtxInfo[i].inputform;
		if (texinfo.inputform == XF_TEXINPUT_AB11)
			out.Write("coord.z = 1.0;\n");

		// first transformation
		uid_data->texMtxInfo[i].texgentype = xfmem.texMtxInfo[i].texgentype;
		switch (texinfo.texgentype)
		{
			case XF_TEXGEN_EMBOSS_MAP: // calculate tex coords into bump map

				if (components & (VB_HAS_NRM1|VB_HAS_NRM2))
				{
					// transform the light dir into tangent space
					uid_data->texMtxInfo[i].embosslightshift = xfmem.texMtxInfo[i].embosslightshift;
					uid_data->texMtxInfo[i].embosssourceshift = xfmem.texMtxInfo[i].embosssourceshift;
					out.Write("ldir = normalize(" LIGHT_POS".xyz - pos.xyz);\n", LIGHT_POS_PARAMS(texinfo.embosslightshift));
					out.Write("o.tex%d.xyz = o.tex%d.xyz + float3(dot(ldir, _norm1), dot(ldir, _norm2), 0.0);\n", i, texinfo.embosssourceshift);
				}
				else
				{
					// The following assert was triggered in House of the Dead Overkill and Star Wars Rogue Squadron 2
					//_assert_(0); // should have normals
					uid_data->texMtxInfo[i].embosssourceshift = xfmem.texMtxInfo[i].embosssourceshift;
					out.Write("o.tex%d.xyz = o.tex%d.xyz;\n", i, texinfo.embosssourceshift);
				}

				break;
			case XF_TEXGEN_COLOR_STRGBC0:
				out.Write("o.tex%d.xyz = float3(o.colors_0.x, o.colors_0.y, 1);\n", i);
				break;
			case XF_TEXGEN_COLOR_STRGBC1:
				out.Write("o.tex%d.xyz = float3(o.colors_1.x, o.colors_1.y, 1);\n", i);
				break;
			case XF_TEXGEN_REGULAR:
			default:
				uid_data->texMtxInfo_n_projection |= xfmem.texMtxInfo[i].projection << i;
				if (components & (VB_HAS_TEXMTXIDX0<<i))
				{
					out.Write("int tmp = int(tex%d.z);\n", i);
					if (texinfo.projection == XF_TEXPROJ_STQ)
						out.Write("o.tex%d.xyz = float3(dot(coord, " I_TRANSFORMMATRICES"[tmp]), dot(coord, " I_TRANSFORMMATRICES"[tmp+1]), dot(coord, " I_TRANSFORMMATRICES"[tmp+2]));\n", i);
					else
						out.Write("o.tex%d.xyz = float3(dot(coord, " I_TRANSFORMMATRICES"[tmp]), dot(coord, " I_TRANSFORMMATRICES"[tmp+1]), 1);\n", i);
				}
				else
				{
					if (texinfo.projection == XF_TEXPROJ_STQ)
						out.Write("o.tex%d.xyz = float3(dot(coord, " I_TEXMATRICES"[%d]), dot(coord, " I_TEXMATRICES"[%d]), dot(coord, " I_TEXMATRICES"[%d]));\n", i, 3*i, 3*i+1, 3*i+2);
					else
						out.Write("o.tex%d.xyz = float3(dot(coord, " I_TEXMATRICES"[%d]), dot(coord, " I_TEXMATRICES"[%d]), 1);\n", i, 3*i, 3*i+1);
				}
				break;
		}

		uid_data->dualTexTrans_enabled = xfmem.dualTexTrans.enabled;
		// CHECKME: does this only work for regular tex gen types?
		if (xfmem.dualTexTrans.enabled && texinfo.texgentype == XF_TEXGEN_REGULAR)
		{
			const PostMtxInfo& postInfo = xfmem.postMtxInfo[i];

			uid_data->postMtxInfo[i].index = xfmem.postMtxInfo[i].index;
			int postidx = postInfo.index;
			out.Write("float4 P0 = " I_POSTTRANSFORMMATRICES"[%d];\n"
				"float4 P1 = " I_POSTTRANSFORMMATRICES"[%d];\n"
				"float4 P2 = " I_POSTTRANSFORMMATRICES"[%d];\n",
				postidx & 0x3f, (postidx + 1) & 0x3f, (postidx + 2) & 0x3f);

			uid_data->postMtxInfo[i].normalize = xfmem.postMtxInfo[i].normalize;
			if (postInfo.normalize)
				out.Write("o.tex%d.xyz = normalize(o.tex%d.xyz);\n", i, i);

			// multiply by postmatrix
			out.Write("o.tex%d.xyz = float3(dot(P0.xyz, o.tex%d.xyz) + P0.w, dot(P1.xyz, o.tex%d.xyz) + P1.w, dot(P2.xyz, o.tex%d.xyz) + P2.w);\n", i, i, i, i);
		}

		out.Write("}\n");
	}

	// clipPos/w needs to be done in pixel shader, not here
	out.Write("o.clipPos = o.pos;\n");

	if (g_ActiveConfig.bEnablePixelLighting)
	{
		out.Write("o.Normal = _norm0;\n");
		out.Write("o.WorldPos = pos.xyz;\n");

		if (components & VB_HAS_COL0)
			out.Write("o.colors_0 = color0;\n");

		if (components & VB_HAS_COL1)
			out.Write("o.colors_1 = color1;\n");
	}

	//write the true depth value, if the game uses depth textures pixel shaders will override with the correct values
	//if not early z culling will improve speed
	if (g_ActiveConfig.backend_info.bSupportsClipControl)
	{
		out.Write("o.pos.z = -o.pos.z;\n");
	}
	else // OGL
	{
		// this results in a scale from -1..0 to -1..1 after perspective
		// divide
		out.Write("o.pos.z = o.pos.z * -2.0 - o.pos.w;\n");

		// the next steps of the OGL pipeline are:
		// (x_c,y_c,z_c,w_c) = o.pos  //switch to OGL spec terminology
		// clipping to -w_c <= (x_c,y_c,z_c) <= w_c
		// (x_d,y_d,z_d) = (x_c,y_c,z_c)/w_c//perspective divide
		// z_w = (f-n)/2*z_d + (n+f)/2
		// z_w now contains the value to go to the 0..1 depth buffer

		//trying to get the correct semantic while not using glDepthRange
		//seems to get rather complicated
	}

	// The console GPU places the pixel center at 7/12 in screen space unless
	// antialiasing is enabled, while D3D and OpenGL place it at 0.5. This results
	// in some primitives being placed one pixel too far to the bottom-right,
	// which in turn can be critical if it happens for clear quads.
	// Hence, we compensate for this pixel center difference so that primitives
	// get rasterized correctly.
	out.Write("o.pos.xy = o.pos.xy - o.pos.w * " I_PIXELCENTERCORRECTION".xy;\n");

	if (api_type == API_OPENGL)
	{
		if (g_ActiveConfig.backend_info.bSupportsGeometryShaders)
		{
			AssignVSOutputMembers(out, "vs", "o");
		}
		else
		{
			// TODO: Pass interface blocks between shader stages even if geometry shaders
			// are not supported, however that will require at least OpenGL 3.2 support.
			for (unsigned int i = 0; i < xfmem.numTexGen.numTexGens; ++i)
				out.Write("uv%d.xyz = o.tex%d;\n", i, i);
			out.Write("clipPos = o.clipPos;\n");
			if (g_ActiveConfig.bEnablePixelLighting)
			{
				out.Write("Normal = o.Normal;\n");
				out.Write("WorldPos = o.WorldPos;\n");
			}
			out.Write("colors_0 = o.colors_0;\n");
			out.Write("colors_1 = o.colors_1;\n");
		}

		out.Write("gl_Position = o.pos;\n");
	}
	else // D3D
	{
		out.Write("return o;\n");
	}
	out.Write("}\n");

	return out;
}

VertexShaderUid GetVertexShaderUid(API_TYPE api_type)
{
	return GenerateVertexShader<VertexShaderUid>(api_type);
}

ShaderCode GenerateVertexShaderCode(API_TYPE api_type)
{
	return GenerateVertexShader<ShaderCode>(api_type);
}
