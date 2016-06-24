// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/CommonTypes.h"

#include "VideoBackends/D3D12/BoundingBox.h"
#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DState.h"
#include "VideoBackends/D3D12/D3DStreamBuffer.h"
#include "VideoBackends/D3D12/FramebufferManager.h"
#include "VideoBackends/D3D12/Render.h"
#include "VideoBackends/D3D12/ShaderCache.h"
#include "VideoBackends/D3D12/VertexManager.h"

#include "VideoCommon/BoundingBox.h"
#include "VideoCommon/Debugger.h"
#include "VideoCommon/IndexGenerator.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

static constexpr unsigned int MAX_IBUFFER_SIZE = VertexManager::MAXIBUFFERSIZE * sizeof(u16) * 16;
static constexpr unsigned int MAX_VBUFFER_SIZE = VertexManager::MAXVBUFFERSIZE * 4;

void VertexManager::SetIndexBuffer()
{
	D3D12_INDEX_BUFFER_VIEW ib_view = {
		m_index_stream_buffer->GetBaseGPUAddress(),          // D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
		static_cast<UINT>(m_index_stream_buffer->GetSize()), // UINT SizeInBytes;
		DXGI_FORMAT_R16_UINT                                 // DXGI_FORMAT Format;
	};

	D3D::current_command_list->IASetIndexBuffer(&ib_view);
}

void VertexManager::CreateDeviceObjects()
{
	m_vertex_draw_offset = 0;
	m_index_draw_offset = 0;

	m_vertex_stream_buffer = std::make_unique<D3DStreamBuffer>(MAXVBUFFERSIZE * 2, MAX_VBUFFER_SIZE, &m_vertex_stream_buffer_reallocated);
	m_index_stream_buffer  = std::make_unique<D3DStreamBuffer>(MAXIBUFFERSIZE * sizeof(u16) * 2, MAXIBUFFERSIZE * sizeof(u16) * 16, &m_index_stream_buffer_reallocated);

	SetIndexBuffer();

	// Use CPU-only memory if the GPU won't be reading from the buffers,
	// since reading upload heaps on the CPU is slow..
	m_vertex_cpu_buffer.resize(MAXVBUFFERSIZE);
	m_index_cpu_buffer.resize(MAXIBUFFERSIZE);
}

void VertexManager::DestroyDeviceObjects()
{
	m_vertex_stream_buffer.reset();
	m_index_stream_buffer.reset();

	m_vertex_cpu_buffer.clear();
	m_index_cpu_buffer.clear();
}

VertexManager::VertexManager()
{
	CreateDeviceObjects();
}

VertexManager::~VertexManager()
{
	DestroyDeviceObjects();
}

void VertexManager::PrepareDrawBuffers(u32 stride)
{
	u32 vertex_data_size = IndexGenerator::GetNumVerts() * stride;
	u32 index_data_size = IndexGenerator::GetIndexLen() * sizeof(u16);

	m_vertex_stream_buffer->OverrideSizeOfPreviousAllocation(vertex_data_size);
	m_index_stream_buffer->OverrideSizeOfPreviousAllocation(index_data_size);

	ADDSTAT(stats.thisFrame.bytesVertexStreamed, vertex_data_size);
	ADDSTAT(stats.thisFrame.bytesIndexStreamed, index_data_size);
}

void VertexManager::Draw(u32 stride)
{
	static u32 s_previous_stride = UINT_MAX;

	u32 indices = IndexGenerator::GetIndexLen();

	if (D3D::command_list_mgr->GetCommandListDirtyState(COMMAND_LIST_STATE_VERTEX_BUFFER) || s_previous_stride != stride)
	{
		D3D12_VERTEX_BUFFER_VIEW vb_view = {
			m_vertex_stream_buffer->GetBaseGPUAddress(),          // D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
			static_cast<UINT>(m_vertex_stream_buffer->GetSize()), // UINT SizeInBytes;
			stride                                                // UINT StrideInBytes;
		};

		D3D::current_command_list->IASetVertexBuffers(0, 1, &vb_view);

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_VERTEX_BUFFER, false);
		s_previous_stride = stride;
	}

	D3D_PRIMITIVE_TOPOLOGY d3d_primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

	switch (current_primitive_type)
	{
		case PRIMITIVE_POINTS:
			d3d_primitive_topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			break;
		case PRIMITIVE_LINES:
			d3d_primitive_topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			break;
	}

	if (D3D::command_list_mgr->GetCommandListPrimitiveTopology() != d3d_primitive_topology)
	{
		D3D::current_command_list->IASetPrimitiveTopology(d3d_primitive_topology);
		D3D::command_list_mgr->SetCommandListPrimitiveTopology(d3d_primitive_topology);
	}

	u32 base_vertex = m_vertex_draw_offset / stride;
	u32 start_index = m_index_draw_offset / sizeof(u16);

	D3D::current_command_list->DrawIndexedInstanced(indices, 1, start_index, base_vertex, 0);

	INCSTAT(stats.thisFrame.numDrawCalls);
}

void VertexManager::vFlush(bool use_dst_alpha)
{
	ShaderCache::LoadAndSetActiveShaders(use_dst_alpha ? DSTALPHA_DUAL_SOURCE_BLEND : DSTALPHA_NONE, current_primitive_type);

	if (g_ActiveConfig.backend_info.bSupportsBBox && BoundingBox::active)
		BBox::Invalidate();

	u32 stride = VertexLoaderManager::GetCurrentVertexFormat()->GetVertexStride();

	PrepareDrawBuffers(stride);

	g_renderer->ApplyState(use_dst_alpha);

	Draw(stride);

	D3D::command_list_mgr->m_draws_since_last_execution++;

	// Many Gamecube/Wii titles read from the EFB each frame to determine what new rendering work to submit, e.g. where sun rays are
	// occluded and where they aren't. When the CPU wants to read this data (done in Renderer::AccessEFB), it requires that the GPU
	// finish all oustanding work. As an optimization, when we detect that the CPU is likely to read back data this frame, we break
	// up the rendering work and submit it more frequently to the GPU (via ExecuteCommandList). Thus, when the CPU finally needs the
	// the GPU to finish all of its work, there is (hopefully) less work outstanding to wait on at that moment.

	// D3D12TODO: Decide right threshold for drawCountSinceAsyncFlush at runtime depending on
	// amount of stall measured in AccessEFB.

	// We can't do this with perf queries enabled since it can leave queries open.

	if (D3D::command_list_mgr->m_cpu_access_last_frame &&
		D3D::command_list_mgr->m_draws_since_last_execution > 100 &&
		!PerfQueryBase::ShouldEmulate())
	{
		D3D::command_list_mgr->m_draws_since_last_execution = 0;

		D3D::command_list_mgr->ExecuteQueuedWork();
	}
}

void VertexManager::ResetBuffer(u32 stride)
{
	if (s_cull_all)
	{
		s_pCurBufferPointer = m_vertex_cpu_buffer.data();
		s_pBaseBufferPointer = m_vertex_cpu_buffer.data();
		s_pEndBufferPointer = m_vertex_cpu_buffer.data() + MAXVBUFFERSIZE;

		IndexGenerator::Start(reinterpret_cast<u16*>(m_index_cpu_buffer.data()));
		return;
	}

	m_vertex_stream_buffer->AllocateSpaceInBuffer(MAXVBUFFERSIZE, stride);

	if (m_vertex_stream_buffer_reallocated)
	{
		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_VERTEX_BUFFER, true);
		m_vertex_stream_buffer_reallocated = false;
	}

	s_pBaseBufferPointer = static_cast<u8*>(m_vertex_stream_buffer->GetBaseCPUAddress());
	s_pEndBufferPointer  = s_pBaseBufferPointer + m_vertex_stream_buffer->GetSize();
	s_pCurBufferPointer  = static_cast<u8*>(m_vertex_stream_buffer->GetCPUAddressOfCurrentAllocation());
	m_vertex_draw_offset = static_cast<u32>(m_vertex_stream_buffer->GetOffsetOfCurrentAllocation());

	m_index_stream_buffer->AllocateSpaceInBuffer(MAXIBUFFERSIZE * sizeof(u16), sizeof(u16));

	if (m_index_stream_buffer_reallocated)
	{
		SetIndexBuffer();
		m_index_stream_buffer_reallocated = false;
	}

	m_index_draw_offset = static_cast<u32>(m_index_stream_buffer->GetOffsetOfCurrentAllocation());
	IndexGenerator::Start(static_cast<u16*>(m_index_stream_buffer->GetCPUAddressOfCurrentAllocation()));
}

}  // namespace
