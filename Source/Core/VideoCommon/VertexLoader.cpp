// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/VertexLoader.h"

#include "Common/Assert.h"
#include "Common/CommonTypes.h"

#include "VideoCommon/DataReader.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexLoaderUtils.h"
#include "VideoCommon/VertexLoader_Color.h"
#include "VideoCommon/VertexLoader_Normal.h"
#include "VideoCommon/VertexLoader_Position.h"
#include "VideoCommon/VertexLoader_TextCoord.h"
#include "VideoCommon/VideoCommon.h"

// This pointer is used as the source/dst for all fixed function loader calls
u8* g_video_buffer_read_ptr;
u8* g_vertex_manager_write_ptr;

static void PosMtx_ReadDirect_UByte(VertexLoader* loader)
{
  u32 posmtx = DataRead<u8>() & 0x3f;
  if (loader->m_counter < 3)
    VertexLoaderManager::position_matrix_index[loader->m_counter + 1] = posmtx;
  DataWrite<u32>(posmtx);
  PRIM_LOG("posmtx: {}, ", posmtx);
}

static void TexMtx_ReadDirect_UByte(VertexLoader* loader)
{
  loader->m_curtexmtx[loader->m_texmtxread] = DataRead<u8>() & 0x3f;

  PRIM_LOG("texmtx{}: {}, ", loader->m_texmtxread, loader->m_curtexmtx[loader->m_texmtxread]);
  loader->m_texmtxread++;
}

static void TexMtx_Write_Float(VertexLoader* loader)
{
  DataWrite(float(loader->m_curtexmtx[loader->m_texmtxwrite++]));
}

static void TexMtx_Write_Float2(VertexLoader* loader)
{
  DataWrite(0.f);
  DataWrite(float(loader->m_curtexmtx[loader->m_texmtxwrite++]));
}

static void TexMtx_Write_Float3(VertexLoader* loader)
{
  DataWrite(0.f);
  DataWrite(0.f);
  DataWrite(float(loader->m_curtexmtx[loader->m_texmtxwrite++]));
}

static void SkipVertex(VertexLoader* loader)
{
  if (loader->m_vertexSkip)
  {
    // reset the output buffer
    g_vertex_manager_write_ptr -= loader->m_native_vtx_decl.stride;

    loader->m_skippedVertices++;
  }
}

VertexLoader::VertexLoader(const TVtxDesc& vtx_desc, const VAT& vtx_attr)
    : VertexLoaderBase(vtx_desc, vtx_attr)
{
  CompileVertexTranslator();

  // generate frac factors
  m_posScale = 1.0f / (1U << m_VtxAttr.PosFrac);
  for (int i = 0; i < 8; i++)
    m_tcScale[i] = 1.0f / (1U << m_VtxAttr.texCoord[i].Frac);
}

void VertexLoader::CompileVertexTranslator()
{
  m_VertexSize = 0;
  const TVtxAttr& vtx_attr = m_VtxAttr;

  // Reset pipeline
  m_numPipelineStages = 0;

  u32 components = 0;

  // Position in pc vertex format.
  int nat_offset = 0;

  // Position Matrix Index
  if (m_VtxDesc.low.PosMatIdx)
  {
    WriteCall(PosMtx_ReadDirect_UByte);
    components |= VB_HAS_POSMTXIDX;
    m_native_vtx_decl.posmtx.components = 4;
    m_native_vtx_decl.posmtx.enable = true;
    m_native_vtx_decl.posmtx.offset = nat_offset;
    m_native_vtx_decl.posmtx.type = VAR_UNSIGNED_BYTE;
    m_native_vtx_decl.posmtx.integer = true;
    nat_offset += 4;
    m_VertexSize += 1;
  }

  if (m_VtxDesc.low.Tex0MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX0;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex1MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX1;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex2MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX2;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex3MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX3;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex4MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX4;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex5MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX5;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex6MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX6;
    WriteCall(TexMtx_ReadDirect_UByte);
  }
  if (m_VtxDesc.low.Tex7MatIdx)
  {
    m_VertexSize += 1;
    components |= VB_HAS_TEXMTXIDX7;
    WriteCall(TexMtx_ReadDirect_UByte);
  }

  // Write vertex position loader
  WriteCall(VertexLoader_Position::GetFunction(m_VtxDesc.low.Position, m_VtxAttr.PosFormat,
                                               m_VtxAttr.PosElements));

  m_VertexSize += VertexLoader_Position::GetSize(m_VtxDesc.low.Position, m_VtxAttr.PosFormat,
                                                 m_VtxAttr.PosElements);
  int pos_elements = m_VtxAttr.PosElements == CoordComponentCount::XY ? 2 : 3;
  m_native_vtx_decl.position.components = pos_elements;
  m_native_vtx_decl.position.enable = true;
  m_native_vtx_decl.position.offset = nat_offset;
  m_native_vtx_decl.position.type = VAR_FLOAT;
  m_native_vtx_decl.position.integer = false;
  nat_offset += pos_elements * sizeof(float);

  // Normals
  if (m_VtxDesc.low.Normal != VertexComponentFormat::NotPresent)
  {
    m_VertexSize += VertexLoader_Normal::GetSize(m_VtxDesc.low.Normal, m_VtxAttr.NormalFormat,
                                                 m_VtxAttr.NormalElements, m_VtxAttr.NormalIndex3);

    TPipelineFunction pFunc =
        VertexLoader_Normal::GetFunction(m_VtxDesc.low.Normal, m_VtxAttr.NormalFormat,
                                         m_VtxAttr.NormalElements, m_VtxAttr.NormalIndex3);

    if (pFunc == nullptr)
    {
      PanicAlertFmt("VertexLoader_Normal::GetFunction({} {} {} {}) returned zero!",
                    m_VtxDesc.low.Normal, m_VtxAttr.NormalFormat, m_VtxAttr.NormalElements,
                    m_VtxAttr.NormalIndex3);
    }
    WriteCall(pFunc);

    for (int i = 0; i < (vtx_attr.NormalElements == NormalComponentCount::NBT ? 3 : 1); i++)
    {
      m_native_vtx_decl.normals[i].components = 3;
      m_native_vtx_decl.normals[i].enable = true;
      m_native_vtx_decl.normals[i].offset = nat_offset;
      m_native_vtx_decl.normals[i].type = VAR_FLOAT;
      m_native_vtx_decl.normals[i].integer = false;
      nat_offset += 12;
    }

    components |= VB_HAS_NRM0;
    if (m_VtxAttr.NormalElements == NormalComponentCount::NBT)
      components |= VB_HAS_NRM1 | VB_HAS_NRM2;
  }

  for (size_t i = 0; i < m_VtxDesc.low.Color.Size(); i++)
  {
    m_native_vtx_decl.colors[i].components = 4;
    m_native_vtx_decl.colors[i].type = VAR_UNSIGNED_BYTE;
    m_native_vtx_decl.colors[i].integer = false;
    switch (m_VtxDesc.low.Color[i])
    {
    case VertexComponentFormat::NotPresent:
      break;
    case VertexComponentFormat::Direct:
      switch (m_VtxAttr.color[i].Comp)
      {
      case ColorFormat::RGB565:
        m_VertexSize += 2;
        WriteCall(Color_ReadDirect_16b_565);
        break;
      case ColorFormat::RGB888:
        m_VertexSize += 3;
        WriteCall(Color_ReadDirect_24b_888);
        break;
      case ColorFormat::RGB888x:
        m_VertexSize += 4;
        WriteCall(Color_ReadDirect_32b_888x);
        break;
      case ColorFormat::RGBA4444:
        m_VertexSize += 2;
        WriteCall(Color_ReadDirect_16b_4444);
        break;
      case ColorFormat::RGBA6666:
        m_VertexSize += 3;
        WriteCall(Color_ReadDirect_24b_6666);
        break;
      case ColorFormat::RGBA8888:
        m_VertexSize += 4;
        WriteCall(Color_ReadDirect_32b_8888);
        break;
      default:
        ASSERT(0);
        break;
      }
      break;
    case VertexComponentFormat::Index8:
      m_VertexSize += 1;
      switch (m_VtxAttr.color[i].Comp)
      {
      case ColorFormat::RGB565:
        WriteCall(Color_ReadIndex8_16b_565);
        break;
      case ColorFormat::RGB888:
        WriteCall(Color_ReadIndex8_24b_888);
        break;
      case ColorFormat::RGB888x:
        WriteCall(Color_ReadIndex8_32b_888x);
        break;
      case ColorFormat::RGBA4444:
        WriteCall(Color_ReadIndex8_16b_4444);
        break;
      case ColorFormat::RGBA6666:
        WriteCall(Color_ReadIndex8_24b_6666);
        break;
      case ColorFormat::RGBA8888:
        WriteCall(Color_ReadIndex8_32b_8888);
        break;
      default:
        ASSERT(0);
        break;
      }
      break;
    case VertexComponentFormat::Index16:
      m_VertexSize += 2;
      switch (m_VtxAttr.color[i].Comp)
      {
      case ColorFormat::RGB565:
        WriteCall(Color_ReadIndex16_16b_565);
        break;
      case ColorFormat::RGB888:
        WriteCall(Color_ReadIndex16_24b_888);
        break;
      case ColorFormat::RGB888x:
        WriteCall(Color_ReadIndex16_32b_888x);
        break;
      case ColorFormat::RGBA4444:
        WriteCall(Color_ReadIndex16_16b_4444);
        break;
      case ColorFormat::RGBA6666:
        WriteCall(Color_ReadIndex16_24b_6666);
        break;
      case ColorFormat::RGBA8888:
        WriteCall(Color_ReadIndex16_32b_8888);
        break;
      default:
        ASSERT(0);
        break;
      }
      break;
    }
    // Common for the three bottom cases
    if (m_VtxDesc.low.Color[i] != VertexComponentFormat::NotPresent)
    {
      components |= VB_HAS_COL0 << i;
      m_native_vtx_decl.colors[i].offset = nat_offset;
      m_native_vtx_decl.colors[i].enable = true;
      nat_offset += 4;
    }
  }

  // Texture matrix indices (remove if corresponding texture coordinate isn't enabled)
  for (size_t i = 0; i < m_VtxDesc.high.TexCoord.Size(); i++)
  {
    m_native_vtx_decl.texcoords[i].offset = nat_offset;
    m_native_vtx_decl.texcoords[i].type = VAR_FLOAT;
    m_native_vtx_decl.texcoords[i].integer = false;

    const auto tc = m_VtxDesc.high.TexCoord[i].Value();
    const auto format = m_VtxAttr.texCoord[i].Format;
    const auto elements = m_VtxAttr.texCoord[i].Elements;

    if (tc != VertexComponentFormat::NotPresent)
    {
      ASSERT_MSG(VIDEO, VertexComponentFormat::Direct <= tc && tc <= VertexComponentFormat::Index16,
                 "Invalid texture coordinates!\n(tc = %d)", (u32)tc);
      ASSERT_MSG(VIDEO, ComponentFormat::UByte <= format && format <= ComponentFormat::Float,
                 "Invalid texture coordinates format!\n(format = %d)", (u32)format);
      ASSERT_MSG(VIDEO, elements == TexComponentCount::S || elements == TexComponentCount::ST,
                 "Invalid number of texture coordinates elements!\n(elements = %d)", (u32)elements);

      components |= VB_HAS_UV0 << i;
      WriteCall(VertexLoader_TextCoord::GetFunction(tc, format, elements));
      m_VertexSize += VertexLoader_TextCoord::GetSize(tc, format, elements);
    }

    if (components & (VB_HAS_TEXMTXIDX0 << i))
    {
      m_native_vtx_decl.texcoords[i].enable = true;
      if (tc != VertexComponentFormat::NotPresent)
      {
        // if texmtx is included, texcoord will always be 3 floats, z will be the texmtx index
        m_native_vtx_decl.texcoords[i].components = 3;
        nat_offset += 12;
        WriteCall(m_VtxAttr.texCoord[i].Elements == TexComponentCount::ST ? TexMtx_Write_Float :
                                                                            TexMtx_Write_Float2);
      }
      else
      {
        m_native_vtx_decl.texcoords[i].components = 3;
        nat_offset += 12;
        WriteCall(TexMtx_Write_Float3);
      }
    }
    else
    {
      if (tc != VertexComponentFormat::NotPresent)
      {
        m_native_vtx_decl.texcoords[i].enable = true;
        m_native_vtx_decl.texcoords[i].components =
            vtx_attr.texCoord[i].Elements == TexComponentCount::ST ? 2 : 1;
        nat_offset += 4 * (vtx_attr.texCoord[i].Elements == TexComponentCount::ST ? 2 : 1);
      }
    }

    if (tc == VertexComponentFormat::NotPresent)
    {
      // if there's more tex coords later, have to write a dummy call
      size_t j = i + 1;
      for (; j < m_VtxDesc.high.TexCoord.Size(); ++j)
      {
        if (m_VtxDesc.high.TexCoord[j] != VertexComponentFormat::NotPresent)
        {
          WriteCall(VertexLoader_TextCoord::GetDummyFunction());  // important to get indices right!
          break;
        }
      }
      // tricky!
      if (j == 8 && !((components & VB_HAS_TEXMTXIDXALL) & (VB_HAS_TEXMTXIDXALL << (i + 1))))
      {
        // no more tex coords and tex matrices, so exit loop
        break;
      }
    }
  }

  // indexed position formats may skip the vertex
  if (IsIndexed(m_VtxDesc.low.Position))
  {
    WriteCall(SkipVertex);
  }

  m_native_components = components;
  m_native_vtx_decl.stride = nat_offset;
}

void VertexLoader::WriteCall(TPipelineFunction func)
{
  m_PipelineStages[m_numPipelineStages++] = func;
}

int VertexLoader::RunVertices(DataReader src, DataReader dst, int count)
{
  g_vertex_manager_write_ptr = dst.GetPointer();
  g_video_buffer_read_ptr = src.GetPointer();

  m_numLoadedVertices += count;
  m_skippedVertices = 0;

  for (m_counter = count - 1; m_counter >= 0; m_counter--)
  {
    m_tcIndex = 0;
    m_colIndex = 0;
    m_texmtxwrite = m_texmtxread = 0;
    for (int i = 0; i < m_numPipelineStages; i++)
      m_PipelineStages[i](this);
    PRIM_LOG("\n");
  }

  return count - m_skippedVertices;
}
