// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/CommonTypes.h"

#include "VideoBackends/D3D12/D3D12VertexManager.h"

#include "Common/Align.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"

#include "VideoBackends/D3D12/D3D12Renderer.h"
#include "VideoBackends/D3D12/D3D12StreamBuffer.h"
#include "VideoBackends/D3D12/DX12Context.h"

#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{
VertexManager::VertexManager() = default;

VertexManager::~VertexManager() = default;

bool VertexManager::Initialize()
{
  if (!VertexManagerBase::Initialize())
    return false;

  if (!m_vertex_stream_buffer.AllocateBuffer(VERTEX_STREAM_BUFFER_SIZE) ||
      !m_index_stream_buffer.AllocateBuffer(INDEX_STREAM_BUFFER_SIZE) ||
      !m_uniform_stream_buffer.AllocateBuffer(UNIFORM_STREAM_BUFFER_SIZE) ||
      !m_texel_stream_buffer.AllocateBuffer(TEXEL_STREAM_BUFFER_SIZE))
  {
    PanicAlertFmt("Failed to allocate streaming buffers");
    return false;
  }

  static constexpr std::array<std::pair<TexelBufferFormat, DXGI_FORMAT>, NUM_TEXEL_BUFFER_FORMATS>
      format_mapping = {{
          {TEXEL_BUFFER_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT},
          {TEXEL_BUFFER_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT},
          {TEXEL_BUFFER_FORMAT_RGBA8_UINT, DXGI_FORMAT_R8G8B8A8_UINT},
          {TEXEL_BUFFER_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_UINT},
      }};
  for (const auto& it : format_mapping)
  {
    DescriptorHandle& dh = m_texel_buffer_views[it.first];
    if (!g_dx_context->GetDescriptorHeapManager().Allocate(&dh))
    {
      PanicAlertFmt("Failed to allocate descriptor for texel buffer");
      return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {it.second, D3D12_SRV_DIMENSION_BUFFER,
                                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
    srv_desc.Buffer.NumElements =
        m_texel_stream_buffer.GetSize() / GetTexelBufferElementSize(it.first);
    g_dx_context->GetDevice()->CreateShaderResourceView(m_texel_stream_buffer.GetBuffer(),
                                                        &srv_desc, dh.cpu_handle);
  }

  UploadAllConstants();
  return true;
}

void VertexManager::ResetBuffer(u32 vertex_stride)
{
  // Attempt to allocate from buffers
  bool has_vbuffer_allocation = m_vertex_stream_buffer.ReserveMemory(MAXVBUFFERSIZE, vertex_stride);
  bool has_ibuffer_allocation =
      m_index_stream_buffer.ReserveMemory(MAXIBUFFERSIZE * sizeof(u16), sizeof(u16));
  if (!has_vbuffer_allocation || !has_ibuffer_allocation)
  {
    // Flush any pending commands first, so that we can wait on the fences
    WARN_LOG_FMT(VIDEO, "Executing command list while waiting for space in vertex/index buffer");
    Renderer::GetInstance()->ExecuteCommandList(false);

    // Attempt to allocate again, this may cause a fence wait
    if (!has_vbuffer_allocation)
      has_vbuffer_allocation = m_vertex_stream_buffer.ReserveMemory(MAXVBUFFERSIZE, vertex_stride);
    if (!has_ibuffer_allocation)
      has_ibuffer_allocation =
          m_index_stream_buffer.ReserveMemory(MAXIBUFFERSIZE * sizeof(u16), sizeof(u16));

    // If we still failed, that means the allocation was too large and will never succeed, so panic
    if (!has_vbuffer_allocation || !has_ibuffer_allocation)
      PanicAlertFmt("Failed to allocate space in streaming buffers for pending draw");
  }

  // Update pointers
  m_base_buffer_pointer = m_vertex_stream_buffer.GetHostPointer();
  m_end_buffer_pointer = m_vertex_stream_buffer.GetCurrentHostPointer() + MAXVBUFFERSIZE;
  m_cur_buffer_pointer = m_vertex_stream_buffer.GetCurrentHostPointer();
  m_index_generator.Start(reinterpret_cast<u16*>(m_index_stream_buffer.GetCurrentHostPointer()));
}

void VertexManager::CommitBuffer(u32 num_vertices, u32 vertex_stride, u32 num_indices,
                                 u32* out_base_vertex, u32* out_base_index)
{
  const u32 vertex_data_size = num_vertices * vertex_stride;
  const u32 index_data_size = num_indices * sizeof(u16);

  *out_base_vertex =
      vertex_stride > 0 ? (m_vertex_stream_buffer.GetCurrentOffset() / vertex_stride) : 0;
  *out_base_index = m_index_stream_buffer.GetCurrentOffset() / sizeof(u16);

  m_vertex_stream_buffer.CommitMemory(vertex_data_size);
  m_index_stream_buffer.CommitMemory(index_data_size);

  ADDSTAT(g_stats.this_frame.bytes_vertex_streamed, static_cast<int>(vertex_data_size));
  ADDSTAT(g_stats.this_frame.bytes_index_streamed, static_cast<int>(index_data_size));

  Renderer::GetInstance()->SetVertexBuffer(m_vertex_stream_buffer.GetGPUPointer(), vertex_stride,
                                           m_vertex_stream_buffer.GetSize());
  Renderer::GetInstance()->SetIndexBuffer(m_index_stream_buffer.GetGPUPointer(),
                                          m_index_stream_buffer.GetSize(), DXGI_FORMAT_R16_UINT);
}

void VertexManager::UploadUniforms()
{
  UpdateVertexShaderConstants();
  UpdateGeometryShaderConstants();
  UpdatePixelShaderConstants();
}

void VertexManager::UpdateVertexShaderConstants()
{
  if (!VertexShaderManager::dirty || !ReserveConstantStorage())
    return;

  Renderer::GetInstance()->SetConstantBuffer(1, m_uniform_stream_buffer.GetCurrentGPUPointer());
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), &VertexShaderManager::constants,
              sizeof(VertexShaderConstants));
  m_uniform_stream_buffer.CommitMemory(sizeof(VertexShaderConstants));
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, sizeof(VertexShaderConstants));
  VertexShaderManager::dirty = false;
}

void VertexManager::UpdateGeometryShaderConstants()
{
  if (!GeometryShaderManager::dirty || !ReserveConstantStorage())
    return;

  Renderer::GetInstance()->SetConstantBuffer(2, m_uniform_stream_buffer.GetCurrentGPUPointer());
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), &GeometryShaderManager::constants,
              sizeof(GeometryShaderConstants));
  m_uniform_stream_buffer.CommitMemory(sizeof(GeometryShaderConstants));
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, sizeof(GeometryShaderConstants));
  GeometryShaderManager::dirty = false;
}

void VertexManager::UpdatePixelShaderConstants()
{
  if (!PixelShaderManager::dirty || !ReserveConstantStorage())
    return;

  Renderer::GetInstance()->SetConstantBuffer(0, m_uniform_stream_buffer.GetCurrentGPUPointer());
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), &PixelShaderManager::constants,
              sizeof(PixelShaderConstants));
  m_uniform_stream_buffer.CommitMemory(sizeof(PixelShaderConstants));
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, sizeof(PixelShaderConstants));
  PixelShaderManager::dirty = false;
}

bool VertexManager::ReserveConstantStorage()
{
  static constexpr u32 reserve_size =
      static_cast<u32>(std::max({sizeof(PixelShaderConstants), sizeof(VertexShaderConstants),
                                 sizeof(GeometryShaderConstants)}));
  if (m_uniform_stream_buffer.ReserveMemory(reserve_size,
                                            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
  {
    return true;
  }

  // The only places that call constant updates are safe to have state restored.
  WARN_LOG_FMT(VIDEO, "Executing command list while waiting for space in uniform buffer");
  Renderer::GetInstance()->ExecuteCommandList(false);

  // Since we are on a new command buffer, all constants have been invalidated, and we need
  // to reupload them. We may as well do this now, since we're issuing a draw anyway.
  UploadAllConstants();
  return false;
}

void VertexManager::UploadAllConstants()
{
  // We are free to re-use parts of the buffer now since we're uploading all constants.
  const u32 pixel_constants_offset = 0;
  constexpr u32 vertex_constants_offset =
      Common::AlignUp(pixel_constants_offset + sizeof(PixelShaderConstants),
                      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  constexpr u32 geometry_constants_offset =
      Common::AlignUp(vertex_constants_offset + sizeof(VertexShaderConstants),
                      D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
  const u32 allocation_size = geometry_constants_offset + sizeof(GeometryShaderConstants);

  // Allocate everything at once.
  // We should only be here if the buffer was full and a command buffer was submitted anyway.
  if (!m_uniform_stream_buffer.ReserveMemory(allocation_size,
                                             D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
  {
    PanicAlertFmt("Failed to allocate space for constants in streaming buffer");
    return;
  }

  // Update bindings
  Renderer::GetInstance()->SetConstantBuffer(0, m_uniform_stream_buffer.GetCurrentGPUPointer() +
                                                    pixel_constants_offset);
  Renderer::GetInstance()->SetConstantBuffer(1, m_uniform_stream_buffer.GetCurrentGPUPointer() +
                                                    vertex_constants_offset);
  Renderer::GetInstance()->SetConstantBuffer(2, m_uniform_stream_buffer.GetCurrentGPUPointer() +
                                                    geometry_constants_offset);

  // Copy the actual data in
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer() + pixel_constants_offset,
              &PixelShaderManager::constants, sizeof(PixelShaderConstants));
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer() + vertex_constants_offset,
              &VertexShaderManager::constants, sizeof(VertexShaderConstants));
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer() + geometry_constants_offset,
              &GeometryShaderManager::constants, sizeof(GeometryShaderConstants));

  // Finally, flush buffer memory after copying
  m_uniform_stream_buffer.CommitMemory(allocation_size);
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, allocation_size);

  // Clear dirty flags
  VertexShaderManager::dirty = false;
  GeometryShaderManager::dirty = false;
  PixelShaderManager::dirty = false;
}

void VertexManager::UploadUtilityUniforms(const void* data, u32 data_size)
{
  InvalidateConstants();
  if (!m_uniform_stream_buffer.ReserveMemory(data_size,
                                             D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
  {
    WARN_LOG_FMT(VIDEO, "Executing command buffer while waiting for ext space in uniform buffer");
    Renderer::GetInstance()->ExecuteCommandList(false);
  }

  Renderer::GetInstance()->SetConstantBuffer(0, m_uniform_stream_buffer.GetCurrentGPUPointer());
  Renderer::GetInstance()->SetConstantBuffer(1, m_uniform_stream_buffer.GetCurrentGPUPointer());
  Renderer::GetInstance()->SetConstantBuffer(2, m_uniform_stream_buffer.GetCurrentGPUPointer());
  std::memcpy(m_uniform_stream_buffer.GetCurrentHostPointer(), data, data_size);
  m_uniform_stream_buffer.CommitMemory(data_size);
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, data_size);
}

bool VertexManager::UploadTexelBuffer(const void* data, u32 data_size, TexelBufferFormat format,
                                      u32* out_offset)
{
  if (data_size > m_texel_stream_buffer.GetSize())
    return false;

  const u32 elem_size = GetTexelBufferElementSize(format);
  if (!m_texel_stream_buffer.ReserveMemory(data_size, elem_size))
  {
    // Try submitting cmdbuffer.
    WARN_LOG_FMT(VIDEO, "Submitting command buffer while waiting for space in texel buffer");
    Renderer::GetInstance()->ExecuteCommandList(false);
    if (!m_texel_stream_buffer.ReserveMemory(data_size, elem_size))
    {
      PanicAlertFmt("Failed to allocate {} bytes from texel buffer", data_size);
      return false;
    }
  }

  std::memcpy(m_texel_stream_buffer.GetCurrentHostPointer(), data, data_size);
  *out_offset = static_cast<u32>(m_texel_stream_buffer.GetCurrentOffset()) / elem_size;
  m_texel_stream_buffer.CommitMemory(data_size);
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, data_size);
  Renderer::GetInstance()->SetTextureDescriptor(0, m_texel_buffer_views[format].cpu_handle);
  return true;
}

bool VertexManager::UploadTexelBuffer(const void* data, u32 data_size, TexelBufferFormat format,
                                      u32* out_offset, const void* palette_data, u32 palette_size,
                                      TexelBufferFormat palette_format, u32* out_palette_offset)
{
  const u32 elem_size = GetTexelBufferElementSize(format);
  const u32 palette_elem_size = GetTexelBufferElementSize(palette_format);
  const u32 reserve_size = data_size + palette_size + palette_elem_size;
  if (reserve_size > m_texel_stream_buffer.GetSize())
    return false;

  if (!m_texel_stream_buffer.ReserveMemory(reserve_size, elem_size))
  {
    // Try submitting cmdbuffer.
    WARN_LOG_FMT(VIDEO, "Submitting command buffer while waiting for space in texel buffer");
    Renderer::GetInstance()->ExecuteCommandList(false);
    if (!m_texel_stream_buffer.ReserveMemory(reserve_size, elem_size))
    {
      PanicAlertFmt("Failed to allocate {} bytes from texel buffer", reserve_size);
      return false;
    }
  }

  const u32 palette_byte_offset = Common::AlignUp(data_size, palette_elem_size);
  std::memcpy(m_texel_stream_buffer.GetCurrentHostPointer(), data, data_size);
  std::memcpy(m_texel_stream_buffer.GetCurrentHostPointer() + palette_byte_offset, palette_data,
              palette_size);
  *out_offset = static_cast<u32>(m_texel_stream_buffer.GetCurrentOffset()) / elem_size;
  *out_palette_offset =
      (static_cast<u32>(m_texel_stream_buffer.GetCurrentOffset()) + palette_byte_offset) /
      palette_elem_size;

  m_texel_stream_buffer.CommitMemory(palette_byte_offset + palette_size);
  ADDSTAT(g_stats.this_frame.bytes_uniform_streamed, palette_byte_offset + palette_size);
  Renderer::GetInstance()->SetTextureDescriptor(0, m_texel_buffer_views[format].cpu_handle);
  Renderer::GetInstance()->SetTextureDescriptor(1, m_texel_buffer_views[palette_format].cpu_handle);
  return true;
}

}  // namespace DX12
