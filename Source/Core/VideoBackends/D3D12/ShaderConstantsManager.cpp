// Copyright 2015 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <memory>

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DCommandListManager.h"
#include "VideoBackends/D3D12/D3DStreamBuffer.h"
#include "VideoBackends/D3D12/ShaderConstantsManager.h"

#include "VideoCommon/GeometryShaderManager.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"

namespace DX12
{

enum SHADER_STAGE
{
	SHADER_STAGE_GEOMETRY_SHADER = 0,
	SHADER_STAGE_PIXEL_SHADER = 1,
	SHADER_STAGE_VERTEX_SHADER = 2,
	SHADER_STAGE_COUNT = 3
};

static std::array<std::unique_ptr<D3DStreamBuffer>, SHADER_STAGE_COUNT> s_shader_constant_stream_buffers;

static const unsigned int s_shader_constant_buffer_padded_sizes[SHADER_STAGE_COUNT] = {
	(sizeof(GeometryShaderConstants) + 0xff) & ~0xff,
	(sizeof(PixelShaderConstants)    + 0xff) & ~0xff,
	(sizeof(VertexShaderConstants)   + 0xff) & ~0xff
};

void ShaderConstantsManager::Init()
{
	// Allow a large maximum size, as we want to minimize stalls here
	std::generate(std::begin(s_shader_constant_stream_buffers), std::end(s_shader_constant_stream_buffers), []() {
		return std::make_unique<D3DStreamBuffer>(2 * 1024 * 1024, 64 * 1024 * 1024, nullptr);
	});
}

void ShaderConstantsManager::Shutdown()
{
	for (auto& buffer : s_shader_constant_stream_buffers)
		buffer.reset();
}

bool ShaderConstantsManager::LoadAndSetGeometryShaderConstants()
{
	bool command_list_executed = false;

	if (GeometryShaderManager::dirty)
	{
		command_list_executed = s_shader_constant_stream_buffers[SHADER_STAGE_GEOMETRY_SHADER]->AllocateSpaceInBuffer(
			s_shader_constant_buffer_padded_sizes[SHADER_STAGE_GEOMETRY_SHADER],
			0 // The padded sizes are already aligned to 256 bytes, so don't need to worry about manually aligning offset.
			);

		memcpy(
			s_shader_constant_stream_buffers[SHADER_STAGE_GEOMETRY_SHADER]->GetCPUAddressOfCurrentAllocation(),
			&GeometryShaderManager::constants,
			sizeof(GeometryShaderConstants));

		GeometryShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(GeometryShaderConstants));

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_GS_CBV, true);
	}

	if (D3D::command_list_mgr->GetCommandListDirtyState(COMMAND_LIST_STATE_GS_CBV))
	{
		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_GS_CBV,
			s_shader_constant_stream_buffers[SHADER_STAGE_GEOMETRY_SHADER]->GetGPUAddressOfCurrentAllocation()
			);

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_GS_CBV, false);
	}

	return command_list_executed;
}

bool ShaderConstantsManager::LoadAndSetPixelShaderConstants()
{
	bool command_list_executed = false;

	if (PixelShaderManager::dirty)
	{
		command_list_executed = s_shader_constant_stream_buffers[SHADER_STAGE_PIXEL_SHADER]->AllocateSpaceInBuffer(
			s_shader_constant_buffer_padded_sizes[SHADER_STAGE_PIXEL_SHADER],
			0 // The padded sizes are already aligned to 256 bytes, so don't need to worry about manually aligning offset.
			);

		memcpy(
			s_shader_constant_stream_buffers[SHADER_STAGE_PIXEL_SHADER]->GetCPUAddressOfCurrentAllocation(),
			&PixelShaderManager::constants,
			sizeof(PixelShaderConstants));

		PixelShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(PixelShaderConstants));

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_PS_CBV, true);
	}

	if (D3D::command_list_mgr->GetCommandListDirtyState(COMMAND_LIST_STATE_PS_CBV))
	{
		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_PS_CBVONE,
			s_shader_constant_stream_buffers[SHADER_STAGE_PIXEL_SHADER]->GetGPUAddressOfCurrentAllocation()
			);

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_PS_CBV, false);
	}

	return command_list_executed;
}

bool ShaderConstantsManager::LoadAndSetVertexShaderConstants()
{
	bool command_list_executed = false;

	if (VertexShaderManager::dirty)
	{
		command_list_executed = s_shader_constant_stream_buffers[SHADER_STAGE_VERTEX_SHADER]->AllocateSpaceInBuffer(
			s_shader_constant_buffer_padded_sizes[SHADER_STAGE_VERTEX_SHADER],
			0 // The padded sizes are already aligned to 256 bytes, so don't need to worry about manually aligning offset.
			);

		memcpy(
			s_shader_constant_stream_buffers[SHADER_STAGE_VERTEX_SHADER]->GetCPUAddressOfCurrentAllocation(),
			&VertexShaderManager::constants,
			sizeof(VertexShaderConstants));

		VertexShaderManager::dirty = false;

		ADDSTAT(stats.thisFrame.bytesUniformStreamed, sizeof(VertexShaderConstants));

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_VS_CBV, true);
	}

	if (D3D::command_list_mgr->GetCommandListDirtyState(COMMAND_LIST_STATE_VS_CBV))
	{
		const D3D12_GPU_VIRTUAL_ADDRESS calculated_gpu_va =
			s_shader_constant_stream_buffers[SHADER_STAGE_VERTEX_SHADER]->GetGPUAddressOfCurrentAllocation();

		D3D::current_command_list->SetGraphicsRootConstantBufferView(
			DESCRIPTOR_TABLE_VS_CBV,
			calculated_gpu_va
			);

		if (g_ActiveConfig.bEnablePixelLighting)
			D3D::current_command_list->SetGraphicsRootConstantBufferView(
				DESCRIPTOR_TABLE_PS_CBVTWO,
				calculated_gpu_va
				);

		D3D::command_list_mgr->SetCommandListDirtyState(COMMAND_LIST_STATE_VS_CBV, false);
	}

	return command_list_executed;
}

}