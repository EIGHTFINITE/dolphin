// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoCommon/XFStructs.h"

#include "Common/BitUtils.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"

#include "Core/HW/Memmap.h"

#include "VideoCommon/CPMemory.h"
#include "VideoCommon/DataReader.h"
#include "VideoCommon/Fifo.h"
#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/XFMemory.h"

static void XFMemWritten(u32 transferSize, u32 baseAddress)
{
  g_vertex_manager->Flush();
  VertexShaderManager::InvalidateXFRange(baseAddress, baseAddress + transferSize);
}

static void XFRegWritten(int transferSize, u32 baseAddress, DataReader src)
{
  u32 address = baseAddress;
  u32 dataIndex = 0;

  while (transferSize > 0 && address < XFMEM_REGISTERS_END)
  {
    u32 newValue = src.Peek<u32>(dataIndex * sizeof(u32));
    u32 nextAddress = address + 1;

    switch (address)
    {
    case XFMEM_ERROR:
    case XFMEM_DIAG:
    case XFMEM_STATE0:  // internal state 0
    case XFMEM_STATE1:  // internal state 1
    case XFMEM_CLOCK:
    case XFMEM_SETGPMETRIC:
      nextAddress = 0x1007;
      break;

    case XFMEM_CLIPDISABLE:
      // if (data & 1) {} // disable clipping detection
      // if (data & 2) {} // disable trivial rejection
      // if (data & 4) {} // disable cpoly clipping acceleration
      break;

    case XFMEM_VTXSPECS:  //__GXXfVtxSpecs, wrote 0004
      break;

    case XFMEM_SETNUMCHAN:
      if (xfmem.numChan.numColorChans != (newValue & 3))
        g_vertex_manager->Flush();
      VertexShaderManager::SetLightingConfigChanged();
      break;

    case XFMEM_SETCHAN0_AMBCOLOR:  // Channel Ambient Color
    case XFMEM_SETCHAN1_AMBCOLOR:
    {
      u8 chan = address - XFMEM_SETCHAN0_AMBCOLOR;
      if (xfmem.ambColor[chan] != newValue)
      {
        g_vertex_manager->Flush();
        VertexShaderManager::SetMaterialColorChanged(chan);
      }
      break;
    }

    case XFMEM_SETCHAN0_MATCOLOR:  // Channel Material Color
    case XFMEM_SETCHAN1_MATCOLOR:
    {
      u8 chan = address - XFMEM_SETCHAN0_MATCOLOR;
      if (xfmem.matColor[chan] != newValue)
      {
        g_vertex_manager->Flush();
        VertexShaderManager::SetMaterialColorChanged(chan + 2);
      }
      break;
    }

    case XFMEM_SETCHAN0_COLOR:  // Channel Color
    case XFMEM_SETCHAN1_COLOR:
    case XFMEM_SETCHAN0_ALPHA:  // Channel Alpha
    case XFMEM_SETCHAN1_ALPHA:
      if (((u32*)&xfmem)[address] != (newValue & 0x7fff))
        g_vertex_manager->Flush();
      VertexShaderManager::SetLightingConfigChanged();
      break;

    case XFMEM_DUALTEX:
      if (xfmem.dualTexTrans.enabled != bool(newValue & 1))
        g_vertex_manager->Flush();
      VertexShaderManager::SetTexMatrixInfoChanged(-1);
      break;

    case XFMEM_SETMATRIXINDA:
      VertexShaderManager::SetTexMatrixChangedA(newValue);
      break;
    case XFMEM_SETMATRIXINDB:
      VertexShaderManager::SetTexMatrixChangedB(newValue);
      break;

    case XFMEM_SETVIEWPORT:
    case XFMEM_SETVIEWPORT + 1:
    case XFMEM_SETVIEWPORT + 2:
    case XFMEM_SETVIEWPORT + 3:
    case XFMEM_SETVIEWPORT + 4:
    case XFMEM_SETVIEWPORT + 5:
      g_vertex_manager->Flush();
      VertexShaderManager::SetViewportChanged();
      PixelShaderManager::SetViewportChanged();
      GeometryShaderManager::SetViewportChanged();

      nextAddress = XFMEM_SETVIEWPORT + 6;
      break;

    case XFMEM_SETPROJECTION:
    case XFMEM_SETPROJECTION + 1:
    case XFMEM_SETPROJECTION + 2:
    case XFMEM_SETPROJECTION + 3:
    case XFMEM_SETPROJECTION + 4:
    case XFMEM_SETPROJECTION + 5:
    case XFMEM_SETPROJECTION + 6:
      g_vertex_manager->Flush();
      VertexShaderManager::SetProjectionChanged();
      GeometryShaderManager::SetProjectionChanged();

      nextAddress = XFMEM_SETPROJECTION + 7;
      break;

    case XFMEM_SETNUMTEXGENS:  // GXSetNumTexGens
      if (xfmem.numTexGen.numTexGens != (newValue & 15))
        g_vertex_manager->Flush();
      break;

    case XFMEM_SETTEXMTXINFO:
    case XFMEM_SETTEXMTXINFO + 1:
    case XFMEM_SETTEXMTXINFO + 2:
    case XFMEM_SETTEXMTXINFO + 3:
    case XFMEM_SETTEXMTXINFO + 4:
    case XFMEM_SETTEXMTXINFO + 5:
    case XFMEM_SETTEXMTXINFO + 6:
    case XFMEM_SETTEXMTXINFO + 7:
      g_vertex_manager->Flush();
      VertexShaderManager::SetTexMatrixInfoChanged(address - XFMEM_SETTEXMTXINFO);

      nextAddress = XFMEM_SETTEXMTXINFO + 8;
      break;

    case XFMEM_SETPOSTMTXINFO:
    case XFMEM_SETPOSTMTXINFO + 1:
    case XFMEM_SETPOSTMTXINFO + 2:
    case XFMEM_SETPOSTMTXINFO + 3:
    case XFMEM_SETPOSTMTXINFO + 4:
    case XFMEM_SETPOSTMTXINFO + 5:
    case XFMEM_SETPOSTMTXINFO + 6:
    case XFMEM_SETPOSTMTXINFO + 7:
      g_vertex_manager->Flush();
      VertexShaderManager::SetTexMatrixInfoChanged(address - XFMEM_SETPOSTMTXINFO);

      nextAddress = XFMEM_SETPOSTMTXINFO + 8;
      break;

    // --------------
    // Unknown Regs
    // --------------

    // Maybe these are for Normals?
    case 0x1048:  // xfmem.texcoords[0].nrmmtxinfo.hex = data; break; ??
    case 0x1049:
    case 0x104a:
    case 0x104b:
    case 0x104c:
    case 0x104d:
    case 0x104e:
    case 0x104f:
      DEBUG_LOG_FMT(VIDEO, "Possible Normal Mtx XF reg?: {:x}={:x}", address, newValue);
      break;

    case 0x1013:
    case 0x1014:
    case 0x1015:
    case 0x1016:
    case 0x1017:

    default:
      if (newValue != 0)  // Ignore writes of zero.
        WARN_LOG_FMT(VIDEO, "Unknown XF Reg: {:x}={:x}", address, newValue);
      break;
    }

    int transferred = nextAddress - address;
    address = nextAddress;

    transferSize -= transferred;
    dataIndex += transferred;
  }
}

void LoadXFReg(u32 transferSize, u32 baseAddress, DataReader src)
{
  // do not allow writes past registers
  if (baseAddress + transferSize > XFMEM_REGISTERS_END)
  {
    WARN_LOG_FMT(VIDEO, "XF load exceeds address space: {:x} {} bytes", baseAddress, transferSize);

    if (baseAddress >= XFMEM_REGISTERS_END)
      transferSize = 0;
    else
      transferSize = XFMEM_REGISTERS_END - baseAddress;
  }

  // write to XF mem
  if (baseAddress < XFMEM_REGISTERS_START && transferSize > 0)
  {
    u32 end = baseAddress + transferSize;

    u32 xfMemBase = baseAddress;
    u32 xfMemTransferSize = transferSize;

    if (end >= XFMEM_REGISTERS_START)
    {
      xfMemTransferSize = XFMEM_REGISTERS_START - baseAddress;

      baseAddress = XFMEM_REGISTERS_START;
      transferSize = end - XFMEM_REGISTERS_START;
    }
    else
    {
      transferSize = 0;
    }

    XFMemWritten(xfMemTransferSize, xfMemBase);
    for (u32 i = 0; i < xfMemTransferSize; i++)
    {
      ((u32*)&xfmem)[xfMemBase + i] = src.Read<u32>();
    }
  }

  // write to XF regs
  if (transferSize > 0)
  {
    XFRegWritten(transferSize, baseAddress, src);
    for (u32 i = 0; i < transferSize; i++)
    {
      ((u32*)&xfmem)[baseAddress + i] = src.Read<u32>();
    }
  }
}

// TODO - verify that it is correct. Seems to work, though.
void LoadIndexedXF(u32 val, int refarray)
{
  int index = val >> 16;
  int address = val & 0xFFF;  // check mask
  int size = ((val >> 12) & 0xF) + 1;
  // load stuff from array to address in xf mem

  u32* currData = (u32*)(&xfmem) + address;
  u32* newData;
  if (Fifo::UseDeterministicGPUThread())
  {
    newData = (u32*)Fifo::PopFifoAuxBuffer(size * sizeof(u32));
  }
  else
  {
    newData = (u32*)Memory::GetPointer(g_main_cp_state.array_bases[refarray] +
                                       g_main_cp_state.array_strides[refarray] * index);
  }
  bool changed = false;
  for (int i = 0; i < size; ++i)
  {
    if (currData[i] != Common::swap32(newData[i]))
    {
      changed = true;
      XFMemWritten(size, address);
      break;
    }
  }
  if (changed)
  {
    for (int i = 0; i < size; ++i)
      currData[i] = Common::swap32(newData[i]);
  }
}

void PreprocessIndexedXF(u32 val, int refarray)
{
  const u32 index = val >> 16;
  const u32 size = ((val >> 12) & 0xF) + 1;

  const u8* new_data = Memory::GetPointer(g_preprocess_cp_state.array_bases[refarray] +
                                          g_preprocess_cp_state.array_strides[refarray] * index);

  const size_t buf_size = size * sizeof(u32);
  Fifo::PushFifoAuxBuffer(new_data, buf_size);
}

std::pair<std::string, std::string> GetXFRegInfo(u32 address, u32 value)
{
// Macro to set the register name and make sure it was written correctly via compile time assertion
#define RegName(reg) ((void)(reg), #reg)
#define DescriptionlessReg(reg) std::make_pair(RegName(reg), "");

  switch (address)
  {
  case XFMEM_ERROR:
    return DescriptionlessReg(XFMEM_ERROR);
  case XFMEM_DIAG:
    return DescriptionlessReg(XFMEM_DIAG);
  case XFMEM_STATE0:  // internal state 0
    return std::make_pair(RegName(XFMEM_STATE0), "internal state 0");
  case XFMEM_STATE1:  // internal state 1
    return std::make_pair(RegName(XFMEM_STATE1), "internal state 1");
  case XFMEM_CLOCK:
    return DescriptionlessReg(XFMEM_CLOCK);
  case XFMEM_SETGPMETRIC:
    return DescriptionlessReg(XFMEM_SETGPMETRIC);

  case XFMEM_CLIPDISABLE:
    return std::make_pair(RegName(XFMEM_CLIPDISABLE), fmt::to_string(ClipDisable{.hex = value}));

  case XFMEM_VTXSPECS:
    return std::make_pair(RegName(XFMEM_VTXSPECS), fmt::to_string(INVTXSPEC{.hex = value}));

  case XFMEM_SETNUMCHAN:
    return std::make_pair(RegName(XFMEM_SETNUMCHAN),
                          fmt::format("Number of color channels: {}", value & 3));
    break;

  case XFMEM_SETCHAN0_AMBCOLOR:
    return std::make_pair(RegName(XFMEM_SETCHAN0_AMBCOLOR),
                          fmt::format("Channel 0 Ambient Color: {:06x}", value));
  case XFMEM_SETCHAN1_AMBCOLOR:
    return std::make_pair(RegName(XFMEM_SETCHAN1_AMBCOLOR),
                          fmt::format("Channel 1 Ambient Color: {:06x}", value));

  case XFMEM_SETCHAN0_MATCOLOR:
    return std::make_pair(RegName(XFMEM_SETCHAN0_MATCOLOR),
                          fmt::format("Channel 0 Material Color: {:06x}", value));
  case XFMEM_SETCHAN1_MATCOLOR:
    return std::make_pair(RegName(XFMEM_SETCHAN1_MATCOLOR),
                          fmt::format("Channel 1 Material Color: {:06x}", value));

  case XFMEM_SETCHAN0_COLOR:  // Channel Color
    return std::make_pair(RegName(XFMEM_SETCHAN0_COLOR),
                          fmt::format("Channel 0 Color config:\n{}", LitChannel{.hex = value}));
  case XFMEM_SETCHAN1_COLOR:
    return std::make_pair(RegName(XFMEM_SETCHAN1_COLOR),
                          fmt::format("Channel 1 Color config:\n{}", LitChannel{.hex = value}));
  case XFMEM_SETCHAN0_ALPHA:  // Channel Alpha
    return std::make_pair(RegName(XFMEM_SETCHAN0_ALPHA),
                          fmt::format("Channel 0 Alpha config:\n{}", LitChannel{.hex = value}));
  case XFMEM_SETCHAN1_ALPHA:
    return std::make_pair(RegName(XFMEM_SETCHAN1_ALPHA),
                          fmt::format("Channel 1 Alpha config:\n{}", LitChannel{.hex = value}));

  case XFMEM_DUALTEX:
    return std::make_pair(RegName(XFMEM_DUALTEX),
                          fmt::format("Dual Tex Trans {}", (value & 1) ? "enabled" : "disabled"));

  case XFMEM_SETMATRIXINDA:
    return std::make_pair(RegName(XFMEM_SETMATRIXINDA),
                          fmt::format("Matrix index A:\n{}", TMatrixIndexA{.Hex = value}));
  case XFMEM_SETMATRIXINDB:
    return std::make_pair(RegName(XFMEM_SETMATRIXINDB),
                          fmt::format("Matrix index B:\n{}", TMatrixIndexB{.Hex = value}));

  case XFMEM_SETVIEWPORT:
    return std::make_pair(RegName(XFMEM_SETVIEWPORT + 0),
                          fmt::format("Viewport width: {}", Common::BitCast<float>(value)));
  case XFMEM_SETVIEWPORT + 1:
    return std::make_pair(RegName(XFMEM_SETVIEWPORT + 1),
                          fmt::format("Viewport height: {}", Common::BitCast<float>(value)));
  case XFMEM_SETVIEWPORT + 2:
    return std::make_pair(RegName(XFMEM_SETVIEWPORT + 2),
                          fmt::format("Viewport z range: {}", Common::BitCast<float>(value)));
  case XFMEM_SETVIEWPORT + 3:
    return std::make_pair(RegName(XFMEM_SETVIEWPORT + 3),
                          fmt::format("Viewport x origin: {}", Common::BitCast<float>(value)));
  case XFMEM_SETVIEWPORT + 4:
    return std::make_pair(RegName(XFMEM_SETVIEWPORT + 4),
                          fmt::format("Viewport y origin: {}", Common::BitCast<float>(value)));
  case XFMEM_SETVIEWPORT + 5:
    return std::make_pair(RegName(XFMEM_SETVIEWPORT + 5),
                          fmt::format("Viewport far z: {}", Common::BitCast<float>(value)));
    break;

  case XFMEM_SETPROJECTION:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 0),
                          fmt::format("Projection[0]: {}", Common::BitCast<float>(value)));
  case XFMEM_SETPROJECTION + 1:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 1),
                          fmt::format("Projection[1]: {}", Common::BitCast<float>(value)));
  case XFMEM_SETPROJECTION + 2:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 2),
                          fmt::format("Projection[2]: {}", Common::BitCast<float>(value)));
  case XFMEM_SETPROJECTION + 3:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 3),
                          fmt::format("Projection[3]: {}", Common::BitCast<float>(value)));
  case XFMEM_SETPROJECTION + 4:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 4),
                          fmt::format("Projection[4]: {}", Common::BitCast<float>(value)));
  case XFMEM_SETPROJECTION + 5:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 5),
                          fmt::format("Projection[5]: {}", Common::BitCast<float>(value)));
  case XFMEM_SETPROJECTION + 6:
    return std::make_pair(RegName(XFMEM_SETPROJECTION + 6),
                          fmt::to_string(static_cast<ProjectionType>(value)));

  case XFMEM_SETNUMTEXGENS:
    return std::make_pair(RegName(XFMEM_SETNUMTEXGENS),
                          fmt::format("Number of tex gens: {}", value & 15));

  case XFMEM_SETTEXMTXINFO:
  case XFMEM_SETTEXMTXINFO + 1:
  case XFMEM_SETTEXMTXINFO + 2:
  case XFMEM_SETTEXMTXINFO + 3:
  case XFMEM_SETTEXMTXINFO + 4:
  case XFMEM_SETTEXMTXINFO + 5:
  case XFMEM_SETTEXMTXINFO + 6:
  case XFMEM_SETTEXMTXINFO + 7:
    return std::make_pair(
        fmt::format("XFMEM_SETTEXMTXINFO Matrix {}", address - XFMEM_SETTEXMTXINFO),
        fmt::to_string(TexMtxInfo{.hex = value}));

  case XFMEM_SETPOSTMTXINFO:
  case XFMEM_SETPOSTMTXINFO + 1:
  case XFMEM_SETPOSTMTXINFO + 2:
  case XFMEM_SETPOSTMTXINFO + 3:
  case XFMEM_SETPOSTMTXINFO + 4:
  case XFMEM_SETPOSTMTXINFO + 5:
  case XFMEM_SETPOSTMTXINFO + 6:
  case XFMEM_SETPOSTMTXINFO + 7:
    return std::make_pair(
        fmt::format("XFMEM_SETPOSTMTXINFO Matrix {}", address - XFMEM_SETPOSTMTXINFO),
        fmt::to_string(PostMtxInfo{.hex = value}));

  // --------------
  // Unknown Regs
  // --------------

  // Maybe these are for Normals?
  case 0x1048:  // xfmem.texcoords[0].nrmmtxinfo.hex = data; break; ??
  case 0x1049:
  case 0x104a:
  case 0x104b:
  case 0x104c:
  case 0x104d:
  case 0x104e:
  case 0x104f:
    return std::make_pair(
        fmt::format("Possible Normal Mtx XF reg?: {:x}={:x}", address, value),
        "Maybe these are for Normals? xfmem.texcoords[0].nrmmtxinfo.hex = data; break; ??");
    break;

  case 0x1013:
  case 0x1014:
  case 0x1015:
  case 0x1016:
  case 0x1017:

  default:
    return std::make_pair(fmt::format("Unknown XF Reg: {:x}={:x}", address, value), "");
  }
#undef RegName
#undef DescriptionlessReg
}

std::string GetXFMemName(u32 address)
{
  if (address >= XFMEM_POSMATRICES && address < XFMEM_POSMATRICES_END)
  {
    const u32 row = (address - XFMEM_POSMATRICES) / 4;
    const u32 col = (address - XFMEM_POSMATRICES) % 4;
    return fmt::format("Position matrix row {:2d} col {:2d}", row, col);
  }
  else if (address >= XFMEM_NORMALMATRICES && address < XFMEM_NORMALMATRICES_END)
  {
    const u32 row = (address - XFMEM_NORMALMATRICES) / 3;
    const u32 col = (address - XFMEM_NORMALMATRICES) % 3;
    return fmt::format("Normal matrix row {:2d} col {:2d}", row, col);
  }
  else if (address >= XFMEM_POSTMATRICES && address < XFMEM_POSTMATRICES_END)
  {
    const u32 row = (address - XFMEM_POSMATRICES) / 4;
    const u32 col = (address - XFMEM_POSMATRICES) % 4;
    return fmt::format("Post matrix row {:2d} col {:2d}", row, col);
  }
  else if (address >= XFMEM_LIGHTS && address < XFMEM_LIGHTS_END)
  {
    const u32 light = (address - XFMEM_LIGHTS) / 16;
    const u32 offset = (address - XFMEM_LIGHTS) % 16;
    switch (offset)
    {
    default:
      return fmt::format("Light {} unused param {}", light, offset);
    case 3:
      return fmt::format("Light {} color", light);
    case 4:
    case 5:
    case 6:
      return fmt::format("Light {} cosine attenuation {}", light, offset - 4);
    case 7:
    case 8:
    case 9:
      return fmt::format("Light {} distance attenuation {}", light, offset - 7);
    case 10:
    case 11:
    case 12:
      // Yagcd says light pos or "inf ldir", while dolphin has a union for dpos and sdir with only
      // dpos being used nowadays. As far as I can tell only the DX9 engine once at
      // Source/Plugins/Plugin_VideoDX9/Src/TransformEngine.cpp used sdir directly...
      return fmt::format("Light {0} {1} position or inf ldir {1}", light, "xyz"[offset - 10]);
    case 13:
    case 14:
    case 15:
      // Yagcd says light dir or "1/2 angle", dolphin has union for ddir or shalfangle.
      // It would make sense if d stood for direction and s for specular, but it's ddir and
      // shalfhangle that have the comment "specular lights only", both at the same offset,
      // while dpos and sdir have none...
      return fmt::format("Light {0} {1} direction or half hangle {1}", light, "xyz"[offset - 13]);
    }
  }
  else
  {
    return fmt::format("Unknown memory {:04x}", address);
  }
}

std::string GetXFMemDescription(u32 address, u32 value)
{
  if ((address >= XFMEM_POSMATRICES && address < XFMEM_POSMATRICES_END) ||
      (address >= XFMEM_NORMALMATRICES && address < XFMEM_NORMALMATRICES_END) ||
      (address >= XFMEM_POSTMATRICES && address < XFMEM_POSTMATRICES_END))
  {
    // The matrices all use floats
    return fmt::format("{} = {}", GetXFMemName(address), Common::BitCast<float>(value));
  }
  else if (address >= XFMEM_LIGHTS && address < XFMEM_LIGHTS_END)
  {
    // Each light is 16 words; for this function we don't care which light it is
    const u32 offset = (address - XFMEM_LIGHTS) % 16;
    if (offset <= 3)
    {
      // The unused parameters (0, 1, 2) and the color (3) should be hex-formatted
      return fmt::format("{} = {:08x}", GetXFMemName(address), value);
    }
    else
    {
      // Everything else is a float
      return fmt::format("{} = {}", GetXFMemName(address), Common::BitCast<float>(value));
    }
  }
  else
  {
    // Unknown address
    return fmt::format("{} = {:08x}", GetXFMemName(address), value);
  }
}

std::pair<std::string, std::string> GetXFTransferInfo(const u8* data)
{
  const u32 cmd = Common::swap32(data);
  data += 4;
  u32 base_address = cmd & 0xFFFF;
  const u32 transfer_size = ((cmd >> 16) & 15) + 1;

  if (base_address > XFMEM_REGISTERS_END)
  {
    return std::make_pair("Invalid XF Transfer", "Base address past end of address space");
  }
  else if (transfer_size == 1 && base_address >= XFMEM_REGISTERS_START)
  {
    // Write directly to a single register
    const u32 value = Common::swap32(data);
    return GetXFRegInfo(base_address, value);
  }

  // More complicated cases
  fmt::memory_buffer name, desc;
  u32 end_address = base_address + transfer_size;  // exclusive

  // do not allow writes past registers
  if (end_address > XFMEM_REGISTERS_END)
  {
    fmt::format_to(name, "Invalid XF Transfer ");
    fmt::format_to(desc, "Transfer ends past end of address space\n\n");
    end_address = XFMEM_REGISTERS_END;
  }

  // write to XF mem
  if (base_address < XFMEM_REGISTERS_START)
  {
    const u32 xf_mem_base = base_address;
    u32 xf_mem_transfer_size = transfer_size;

    if (end_address > XFMEM_REGISTERS_START)
    {
      xf_mem_transfer_size = XFMEM_REGISTERS_START - base_address;
      base_address = XFMEM_REGISTERS_START;
    }

    fmt::format_to(name, "Write {} XF mem words at {:04x}", xf_mem_transfer_size, xf_mem_base);

    for (u32 i = 0; i < xf_mem_transfer_size; i++)
    {
      const auto mem_desc = GetXFMemDescription(xf_mem_base + i, Common::swap32(data));
      fmt::format_to(desc, i == 0 ? "{}" : "\n{}", mem_desc);
      data += 4;
    }

    if (end_address > XFMEM_REGISTERS_START)
      fmt::format_to(name, "; ");
  }

  // write to XF regs
  if (base_address >= XFMEM_REGISTERS_START)
  {
    fmt::format_to(name, "Write {} XF regs at {:04x}", end_address - base_address, base_address);

    for (u32 address = base_address; address < end_address; address++)
    {
      const u32 value = Common::swap32(data);

      const auto [regname, regdesc] = GetXFRegInfo(address, value);
      fmt::format_to(desc, "{}\n{}\n", regname, regdesc);

      data += 4;
    }
  }

  return std::make_pair(fmt::to_string(name), fmt::to_string(desc));
}
