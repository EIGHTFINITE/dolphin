// Copyright hdcmeta
// Dual-Licensed under MIT and GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D12/D3DBase.h"
#include "VideoBackends/D3D12/D3DQueuedCommandList.h"

namespace DX12
{

template <typename T>
constexpr size_t BufferOffsetForQueueItemType()
{
	return sizeof(T) + sizeof(D3DQueueItemType) * 2;
}

void ID3D12QueuedCommandList::BackgroundThreadFunction(ID3D12QueuedCommandList* parent_queued_command_list)
{
	ID3D12GraphicsCommandList* command_list = parent_queued_command_list->m_command_list;

	byte* queue_array = parent_queued_command_list->m_queue_array;

	unsigned int queue_array_front = 0;

	while (true)
	{
		WaitForSingleObject(parent_queued_command_list->m_begin_execution_event, INFINITE);

		byte* item = &queue_array[queue_array_front];

		while (true)
		{
			switch (reinterpret_cast<D3DQueueItem*>(item)->Type)
			{
				case D3DQueueItemType::ClearDepthStencilView:
				{
					command_list->ClearDepthStencilView(reinterpret_cast<D3DQueueItem*>(item)->ClearDepthStencilView.DepthStencilView, D3D12_CLEAR_FLAG_DEPTH, 0.f, 0, 0, nullptr);

					item += BufferOffsetForQueueItemType<ClearDepthStencilViewArguments>();
					break;
				}

				case D3DQueueItemType::ClearRenderTargetView:
				{
					float clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
					command_list->ClearRenderTargetView(reinterpret_cast<D3DQueueItem*>(item)->ClearRenderTargetView.RenderTargetView, clearColor, 0, nullptr);

					item += BufferOffsetForQueueItemType<ClearRenderTargetViewArguments>();
					break;
				}

				case D3DQueueItemType::CopyBufferRegion:
				{
					command_list->CopyBufferRegion(
						reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.pDstBuffer,
						reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.DstOffset,
						reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.pSrcBuffer,
						reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.SrcOffset,
						reinterpret_cast<D3DQueueItem*>(item)->CopyBufferRegion.NumBytes
						);

					item += BufferOffsetForQueueItemType<CopyBufferRegionArguments>();
					break;
				}

				case D3DQueueItemType::CopyTextureRegion:
				{
					// If box is completely empty, assume that the original API call has a NULL box (which means
					// copy from the entire resource.

					D3D12_BOX* src_box = &reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.srcBox;

					// Front/Back never used, so don't need to check.
					bool empty_box =
						src_box->bottom == 0 &&
						src_box->left == 0 &&
						src_box->right == 0 &&
						src_box->top == 0;

					command_list->CopyTextureRegion(
						&reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.dst,
						reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.DstX,
						reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.DstY,
						reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.DstZ,
						&reinterpret_cast<D3DQueueItem*>(item)->CopyTextureRegion.src,
						empty_box ?
							nullptr : src_box
						);

					item += BufferOffsetForQueueItemType<CopyTextureRegionArguments>();
					break;
				}

				case D3DQueueItemType::DrawIndexedInstanced:
				{
					command_list->DrawIndexedInstanced(
						reinterpret_cast<D3DQueueItem*>(item)->DrawIndexedInstanced.IndexCount,
						1,
						reinterpret_cast<D3DQueueItem*>(item)->DrawIndexedInstanced.StartIndexLocation,
						reinterpret_cast<D3DQueueItem*>(item)->DrawIndexedInstanced.BaseVertexLocation,
						0
						);

					item += BufferOffsetForQueueItemType<DrawIndexedInstancedArguments>();
					break;
				}

				case D3DQueueItemType::DrawInstanced:
				{
					command_list->DrawInstanced(
						reinterpret_cast<D3DQueueItem*>(item)->DrawInstanced.VertexCount,
						1,
						reinterpret_cast<D3DQueueItem*>(item)->DrawInstanced.StartVertexLocation,
						0
						);

					item += BufferOffsetForQueueItemType<DrawInstancedArguments>();
					break;
				}

				case D3DQueueItemType::IASetPrimitiveTopology:
				{
					command_list->IASetPrimitiveTopology(reinterpret_cast<D3DQueueItem*>(item)->IASetPrimitiveTopology.PrimitiveTopology);

					item += BufferOffsetForQueueItemType<IASetPrimitiveTopologyArguments>();
					break;
				}

				case D3DQueueItemType::ResourceBarrier:
				{
					command_list->ResourceBarrier(1, &reinterpret_cast<D3DQueueItem*>(item)->ResourceBarrier.barrier);

					item += BufferOffsetForQueueItemType<ResourceBarrierArguments>();
					break;
				}

				case D3DQueueItemType::RSSetScissorRects:
				{
					D3D12_RECT rect = {
						reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.left,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.top,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.right,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetScissorRects.bottom
					};

					command_list->RSSetScissorRects(1, &rect);
					item += BufferOffsetForQueueItemType<RSSetScissorRectsArguments>();
					break;
				}

				case D3DQueueItemType::RSSetViewports:
				{
					D3D12_VIEWPORT viewport = {
						reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.TopLeftX,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.TopLeftY,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.Width,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.Height,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.MinDepth,
						reinterpret_cast<D3DQueueItem*>(item)->RSSetViewports.MaxDepth
					};

					command_list->RSSetViewports(1, &viewport);
					item += BufferOffsetForQueueItemType<RSSetViewportsArguments>();
					break;
				}

				case D3DQueueItemType::SetDescriptorHeaps:
				{
					command_list->SetDescriptorHeaps(
						reinterpret_cast<D3DQueueItem*>(item)->SetDescriptorHeaps.NumDescriptorHeaps,
						reinterpret_cast<D3DQueueItem*>(item)->SetDescriptorHeaps.ppDescriptorHeap
						);

					item += BufferOffsetForQueueItemType<SetDescriptorHeapsArguments>();
					break;
				}

				case D3DQueueItemType::SetGraphicsRootConstantBufferView:
				{
					command_list->SetGraphicsRootConstantBufferView(
						reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootConstantBufferView.RootParameterIndex,
						reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootConstantBufferView.BufferLocation
						);

					item += BufferOffsetForQueueItemType<SetGraphicsRootConstantBufferViewArguments>();
					break;
				}

				case D3DQueueItemType::SetGraphicsRootDescriptorTable:
				{
					command_list->SetGraphicsRootDescriptorTable(
						reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootDescriptorTable.RootParameterIndex,
						reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootDescriptorTable.BaseDescriptor
						);

					item += BufferOffsetForQueueItemType<SetGraphicsRootDescriptorTableArguments>();
					break;
				}

				case D3DQueueItemType::SetGraphicsRootSignature:
				{
					command_list->SetGraphicsRootSignature(
						reinterpret_cast<D3DQueueItem*>(item)->SetGraphicsRootSignature.pRootSignature
						);

					item += BufferOffsetForQueueItemType<SetGraphicsRootSignatureArguments>();
					break;
				}

				case D3DQueueItemType::SetIndexBuffer:
				{
					command_list->IASetIndexBuffer(
						&reinterpret_cast<D3DQueueItem*>(item)->SetIndexBuffer.desc
						);

					item += BufferOffsetForQueueItemType<SetIndexBufferArguments>();
					break;
				}

				case D3DQueueItemType::SetVertexBuffers:
				{
					command_list->IASetVertexBuffers(
						0,
						1,
						&reinterpret_cast<D3DQueueItem*>(item)->SetVertexBuffers.desc
						);

					item += BufferOffsetForQueueItemType<SetVertexBuffersArguments>();
					break;
				}

				case D3DQueueItemType::SetPipelineState:
				{
					command_list->SetPipelineState(reinterpret_cast<D3DQueueItem*>(item)->SetPipelineState.pPipelineStateObject);
					item += BufferOffsetForQueueItemType<SetPipelineStateArguments>();
					break;
				}

				case D3DQueueItemType::SetRenderTargets:
				{
					unsigned int render_target_count = 0;

					if (reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.RenderTargetDescriptor.ptr)
					{
						render_target_count = 1;
					}

					command_list->OMSetRenderTargets(
						render_target_count,
						reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.RenderTargetDescriptor.ptr == NULL ?
						nullptr :
						&reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.RenderTargetDescriptor,
						FALSE,
						reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.DepthStencilDescriptor.ptr == NULL ?
						nullptr :
						&reinterpret_cast<D3DQueueItem*>(item)->SetRenderTargets.DepthStencilDescriptor
						);

					item += BufferOffsetForQueueItemType<SetRenderTargetsArguments>();
					break;
				}

				case D3DQueueItemType::ResolveSubresource:
				{
					command_list->ResolveSubresource(
						reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.pDstResource,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.DstSubresource,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.pSrcResource,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.SrcSubresource,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveSubresource.Format
						);

					item += BufferOffsetForQueueItemType<ResolveSubresourceArguments>();
					break;
				}

				case D3DQueueItemType::BeginQuery:
				{
					command_list->BeginQuery(
						reinterpret_cast<D3DQueueItem*>(item)->BeginQuery.pQueryHeap,
						reinterpret_cast<D3DQueueItem*>(item)->BeginQuery.Type,
						reinterpret_cast<D3DQueueItem*>(item)->BeginQuery.Index
						);

					item += BufferOffsetForQueueItemType<BeginQueryArguments>();
					break;
				}

				case D3DQueueItemType::EndQuery:
				{
					command_list->EndQuery(
						reinterpret_cast<D3DQueueItem*>(item)->EndQuery.pQueryHeap,
						reinterpret_cast<D3DQueueItem*>(item)->EndQuery.Type,
						reinterpret_cast<D3DQueueItem*>(item)->EndQuery.Index
						);

					item += BufferOffsetForQueueItemType<EndQueryArguments>();
					break;
				}

				case D3DQueueItemType::ResolveQueryData:
				{
					command_list->ResolveQueryData(
						reinterpret_cast<D3DQueueItem*>(item)->ResolveQueryData.pQueryHeap,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveQueryData.Type,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveQueryData.StartElement,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveQueryData.ElementCount,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveQueryData.pDestinationBuffer,
						reinterpret_cast<D3DQueueItem*>(item)->ResolveQueryData.AlignedDestinationBufferOffset
						);

					item += BufferOffsetForQueueItemType<ResolveQueryDataArguments>();
					break;
				}

				case D3DQueueItemType::CloseCommandList:
				{
					CheckHR(command_list->Close());

					item += BufferOffsetForQueueItemType<CloseCommandListArguments>();
					break;
				}

				case D3DQueueItemType::ExecuteCommandList:
				{
					parent_queued_command_list->m_command_queue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(&command_list));

					item += BufferOffsetForQueueItemType<ExecuteCommandListArguments>();
					break;
				}

				case D3DQueueItemType::Present:
				{
					CheckHR(reinterpret_cast<D3DQueueItem*>(item)->Present.swapChain->Present(reinterpret_cast<D3DQueueItem*>(item)->Present.syncInterval, reinterpret_cast<D3DQueueItem*>(item)->Present.flags));

					item += BufferOffsetForQueueItemType<PresentArguments>();
					break;
				}

				case D3DQueueItemType::ResetCommandList:
				{
					CheckHR(command_list->Reset(reinterpret_cast<D3DQueueItem*>(item)->ResetCommandList.allocator, nullptr));

					item += BufferOffsetForQueueItemType<ResetCommandListArguments>();
					break;
				}

				case D3DQueueItemType::ResetCommandAllocator:
				{
					CheckHR(reinterpret_cast<D3DQueueItem*>(item)->ResetCommandAllocator.allocator->Reset());

					item += BufferOffsetForQueueItemType<ResetCommandAllocatorArguments>();
					break;
				}

				case D3DQueueItemType::FenceGpuSignal:
				{
					CheckHR(parent_queued_command_list->m_command_queue->Signal(reinterpret_cast<D3DQueueItem*>(item)->FenceGpuSignal.fence, reinterpret_cast<D3DQueueItem*>(item)->FenceGpuSignal.fence_value));

					item += BufferOffsetForQueueItemType<FenceGpuSignalArguments>();
					break;
				}

				case D3DQueueItemType::FenceCpuSignal:
				{
					CheckHR(reinterpret_cast<D3DQueueItem*>(item)->FenceCpuSignal.fence->Signal(reinterpret_cast<D3DQueueItem*>(item)->FenceCpuSignal.fence_value));

					item += BufferOffsetForQueueItemType<FenceCpuSignalArguments>();
					break;
				}

				case D3DQueueItemType::Stop:

					// Use a goto to break out of the loop, since we can't exit the loop from
					// within a switch statement. We could use a separate 'if' after the switch,
					// but that was the highest source of overhead in the function after profiling.
					// http://stackoverflow.com/questions/1420029/how-to-break-out-of-a-loop-from-inside-a-switch

					bool eligible_to_move_to_front_of_queue = reinterpret_cast<D3DQueueItem*>(item)->Stop.eligible_to_move_to_front_of_queue;
					bool signal_stop_event = reinterpret_cast<D3DQueueItem*>(item)->Stop.signal_stop_event;
					bool terminate_worker_thread = reinterpret_cast<D3DQueueItem*>(item)->Stop.terminate_worker_thread;

					item += BufferOffsetForQueueItemType<StopArguments>();

					if (eligible_to_move_to_front_of_queue && item - queue_array > QUEUE_ARRAY_SIZE * 2 / 3)
					{
						item = queue_array;
					}

					if (signal_stop_event)
					{
						SetEvent(parent_queued_command_list->m_stop_execution_event);
					}

					if (terminate_worker_thread)
						return;

					goto exitLoop;
			}
		}

		exitLoop:

		queue_array_front = static_cast<unsigned int>(item - queue_array);
	}
}

ID3D12QueuedCommandList::ID3D12QueuedCommandList(ID3D12GraphicsCommandList* backing_command_list, ID3D12CommandQueue* backing_command_queue) :
	m_command_list(backing_command_list),
	m_command_queue(backing_command_queue)
{
	memset(m_queue_array, 0, sizeof(m_queue_array));

	m_queue_array_back = m_queue_array;

	m_begin_execution_event = CreateSemaphore(nullptr, 0, 256, nullptr);
	m_stop_execution_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	m_background_thread = std::thread(BackgroundThreadFunction, this);
}

ID3D12QueuedCommandList::~ID3D12QueuedCommandList()
{
	// Kick worker thread, and tell it to exit.
	ProcessQueuedItems(true, true, true);
	m_background_thread.join();

	CloseHandle(m_begin_execution_event);
	CloseHandle(m_stop_execution_event);
}

void ID3D12QueuedCommandList::CheckForOverflow()
{
	constexpr const unsigned int queue_space_allowed_per_frame = QUEUE_ARRAY_SIZE / 3;

	if (m_queue_array_back - m_queue_array_back_at_start_of_frame > queue_space_allowed_per_frame)
	{
		// Game is (possibly) using too much space, kick off queue processing and
		// wait on this thread till it chews through queue.

		// This means the game is submitting more than 28,000 draws a frame.

		ProcessQueuedItems(true, true);
	}
}

void ID3D12QueuedCommandList::ResetQueueOverflowTracking()
{
	m_queue_array_back_at_start_of_frame = m_queue_array_back;
}

void ID3D12QueuedCommandList::QueueExecute()
{
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ExecuteCommandList;

	m_queue_array_back += BufferOffsetForQueueItemType<ExecuteCommandListArguments>();

	CheckForOverflow();
}

void ID3D12QueuedCommandList::QueueFenceGpuSignal(ID3D12Fence* fence_to_signal, UINT64 fence_value)
{
	D3DQueueItem item = {};

	item.Type = D3DQueueItemType::FenceGpuSignal;
	item.FenceGpuSignal.fence = fence_to_signal;
	item.FenceGpuSignal.fence_value = fence_value;

	*reinterpret_cast<D3DQueueItem*>(m_queue_array_back) = item;

	m_queue_array_back += BufferOffsetForQueueItemType<FenceGpuSignalArguments>();

	CheckForOverflow();
}

void ID3D12QueuedCommandList::QueueFenceCpuSignal(ID3D12Fence* fence_to_signal, UINT64 fence_value)
{
	D3DQueueItem item = {};

	item.Type = D3DQueueItemType::FenceCpuSignal;
	item.FenceCpuSignal.fence = fence_to_signal;
	item.FenceCpuSignal.fence_value = fence_value;

	*reinterpret_cast<D3DQueueItem*>(m_queue_array_back) = item;

	m_queue_array_back += BufferOffsetForQueueItemType<FenceCpuSignalArguments>();

	CheckForOverflow();
}

void ID3D12QueuedCommandList::QueuePresent(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags)
{
	D3DQueueItem item = {};

	item.Type = D3DQueueItemType::Present;
	item.Present.swapChain = swap_chain;
	item.Present.flags = flags;
	item.Present.syncInterval = sync_interval;

	*reinterpret_cast<D3DQueueItem*>(m_queue_array_back) = item;

	m_queue_array_back += BufferOffsetForQueueItemType<PresentArguments>();

	CheckForOverflow();
}

void ID3D12QueuedCommandList::ProcessQueuedItems(bool eligible_to_move_to_front_of_queue, bool wait_for_stop, bool terminate_worker_thread)
{
	D3DQueueItem item = {};

	item.Type = D3DQueueItemType::Stop;
	item.Stop.eligible_to_move_to_front_of_queue = eligible_to_move_to_front_of_queue;
	item.Stop.signal_stop_event = wait_for_stop;
	item.Stop.terminate_worker_thread = terminate_worker_thread;

	*reinterpret_cast<D3DQueueItem*>(m_queue_array_back) = item;

	m_queue_array_back += BufferOffsetForQueueItemType<StopArguments>();

	// Only (possibly) move to front of queue when finishing a frame, or when draining GPU queue.
	// Logic in ID3D12QueuedCommandList::CheckForOverflow
	// ensures that not more than one third of queue is used per frame.
	if (eligible_to_move_to_front_of_queue && (m_queue_array_back - m_queue_array > QUEUE_ARRAY_SIZE * 2 / 3))
	{
		m_queue_array_back = m_queue_array;
	}

	if (eligible_to_move_to_front_of_queue)
	{
		ResetQueueOverflowTracking();
	}

	ReleaseSemaphore(m_begin_execution_event, 1, nullptr);

	if (wait_for_stop)
	{
		WaitForSingleObject(m_stop_execution_event, INFINITE);
		ResetEvent(m_stop_execution_event);
	}
}

ULONG ID3D12QueuedCommandList::AddRef()
{
	m_ref.fetch_add(1);
	return m_ref.load();
}

ULONG ID3D12QueuedCommandList::Release()
{
	// fetch_sub returns the value held before the subtraction.
	ULONG ref = m_ref.fetch_sub(1);
	if (ref == 1)
	{
		delete this;
	}

	return ref;
}

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::QueryInterface(
	_In_ REFIID riid,
	_COM_Outptr_ void** ppvObject
	)
{
	*ppvObject = nullptr;
	HRESULT hr = S_OK;

	if (riid == __uuidof(ID3D12GraphicsCommandList))
	{
		*ppvObject = reinterpret_cast<ID3D12GraphicsCommandList*>(this);
	}
	else  if (riid == __uuidof(ID3D12CommandList))
	{
		*ppvObject = reinterpret_cast<ID3D12CommandList*>(this);
	}
	else if (riid == __uuidof(ID3D12DeviceChild))
	{
		*ppvObject = reinterpret_cast<ID3D12DeviceChild*>(this);
	}
	else if (riid == __uuidof(ID3D12Object))
	{
		*ppvObject = reinterpret_cast<ID3D12Object*>(this);
	}
	else
	{
		hr = E_NOINTERFACE;
	}

	if (*ppvObject != nullptr)
	{
		AddRef();
	}

	return hr;
}

// ID3D12Object

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::GetPrivateData(
	_In_  REFGUID guid,
	_Inout_  UINT* pDataSize,
	_Out_writes_bytes_opt_(*pDataSize)  void* pData
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
	return E_FAIL;
}

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPrivateData(
	_In_  REFGUID guid,
	_In_  UINT DataSize,
	_In_reads_bytes_opt_(DataSize)  const void* pData
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
	return E_FAIL;
}

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPrivateDataInterface(
	_In_  REFGUID guid,
	_In_opt_  const IUnknown* pData
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
	return E_FAIL;
}

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::SetName(
	_In_z_  LPCWSTR pName
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
	return E_FAIL;
}

// ID3D12DeviceChild

D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE ID3D12QueuedCommandList::GetType()
{
	return D3D12_COMMAND_LIST_TYPE_DIRECT;
}

// ID3D12CommandList

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::GetDevice(
	REFIID riid,
	_Out_  void** ppDevice
	)
{
	return m_command_list->GetDevice(riid, ppDevice);
}

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::Close() {

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::CloseCommandList;

	m_queue_array_back += BufferOffsetForQueueItemType<CloseCommandListArguments>();

	CheckForOverflow();

	return S_OK;
}

HRESULT STDMETHODCALLTYPE ID3D12QueuedCommandList::Reset(
	_In_  ID3D12CommandAllocator* pAllocator,
	_In_opt_  ID3D12PipelineState* pInitialState
	)
{
	DEBUGCHECK(pInitialState == nullptr, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ResetCommandList;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResetCommandList.allocator = pAllocator;

	m_queue_array_back += BufferOffsetForQueueItemType<ResetCommandListArguments>();

	CheckForOverflow();

	return S_OK;
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearState(
	_In_  ID3D12PipelineState* pPipelineState
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::DrawInstanced(
	_In_  UINT VertexCountPerInstance,
	_In_  UINT InstanceCount,
	_In_  UINT StartVertexLocation,
	_In_  UINT StartInstanceLocation
	)
{
	DEBUGCHECK(InstanceCount == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(StartInstanceLocation == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::DrawInstanced;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->DrawInstanced.StartVertexLocation = StartVertexLocation;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->DrawInstanced.VertexCount = VertexCountPerInstance;

	m_queue_array_back += BufferOffsetForQueueItemType<DrawInstancedArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::DrawIndexedInstanced(
	_In_  UINT IndexCountPerInstance,
	_In_  UINT InstanceCount,
	_In_  UINT StartIndexLocation,
	_In_  INT BaseVertexLocation,
	_In_  UINT StartInstanceLocation
	)
{
	DEBUGCHECK(InstanceCount == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(StartInstanceLocation == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(m_queue_array_back);

	item->Type = D3DQueueItemType::DrawIndexedInstanced;
	item->DrawIndexedInstanced.BaseVertexLocation = BaseVertexLocation;
	item->DrawIndexedInstanced.IndexCount = IndexCountPerInstance;
	item->DrawIndexedInstanced.StartIndexLocation = StartIndexLocation;

	m_queue_array_back += BufferOffsetForQueueItemType<DrawIndexedInstancedArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::Dispatch(
	_In_  UINT ThreadGroupCountX,
	_In_  UINT ThreadGroupCountY,
	_In_  UINT ThreadGroupCountZ
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::DispatchIndirect(
	_In_  ID3D12Resource* pBufferForArgs,
	_In_  UINT AlignedByteOffsetForArgs
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyBufferRegion(
	_In_  ID3D12Resource* pDstBuffer,
	UINT64 DstOffset,
	_In_  ID3D12Resource* pSrcBuffer,
	UINT64 SrcOffset,
	UINT64 NumBytes
	)
{
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::CopyBufferRegion;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyBufferRegion.pDstBuffer = pDstBuffer;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyBufferRegion.DstOffset = static_cast<UINT>(DstOffset);
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyBufferRegion.pSrcBuffer = pSrcBuffer;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyBufferRegion.SrcOffset = static_cast<UINT>(SrcOffset);
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyBufferRegion.NumBytes = static_cast<UINT>(NumBytes);

	m_queue_array_back += BufferOffsetForQueueItemType<CopyBufferRegionArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyTextureRegion(
	_In_  const D3D12_TEXTURE_COPY_LOCATION* pDst,
	UINT DstX,
	UINT DstY,
	UINT DstZ,
	_In_  const D3D12_TEXTURE_COPY_LOCATION* pSrc,
	_In_opt_  const D3D12_BOX* pSrcBox
	)
{
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::CopyTextureRegion;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.dst =* pDst;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.src =* pSrc;

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.DstX = DstX;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.DstY = DstY;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.DstZ = DstZ;

	if (pSrcBox)
		reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.srcBox = *pSrcBox;
	else
		reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->CopyTextureRegion.srcBox = {};

	m_queue_array_back += BufferOffsetForQueueItemType<CopyTextureRegionArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyResource(
	_In_  ID3D12Resource* pDstResource,
	_In_  ID3D12Resource* pSrcResource
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::CopyTiles(
	_In_  ID3D12Resource* pTiledResource,
	_In_  const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate,
	_In_  const D3D12_TILE_REGION_SIZE* pTileRegionSize,
	_In_  ID3D12Resource* pBuffer,
	UINT64 BufferStartOffsetInBytes,
	D3D12_TILE_COPY_FLAGS Flags
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ResolveSubresource(
	_In_  ID3D12Resource* pDstResource,
	_In_  UINT DstSubresource,
	_In_  ID3D12Resource* pSrcResource,
	_In_  UINT SrcSubresource,
	_In_  DXGI_FORMAT Format
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ResolveSubresource;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveSubresource.pDstResource = pDstResource;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveSubresource.DstSubresource = DstSubresource;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveSubresource.pSrcResource = pSrcResource;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveSubresource.SrcSubresource = SrcSubresource;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveSubresource.Format = Format;

	m_queue_array_back += BufferOffsetForQueueItemType<ResolveSubresourceArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::IASetPrimitiveTopology(
	_In_  D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::IASetPrimitiveTopology;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->IASetPrimitiveTopology.PrimitiveTopology = PrimitiveTopology;

	m_queue_array_back += BufferOffsetForQueueItemType<IASetPrimitiveTopologyArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::RSSetViewports(
	_In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT Count,
	_In_reads_(Count)  const D3D12_VIEWPORT* pViewports
	)
{
	DEBUGCHECK(Count == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::RSSetViewports;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetViewports.Height = pViewports->Height;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetViewports.Width = pViewports->Width;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetViewports.TopLeftX = pViewports->TopLeftX;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetViewports.TopLeftY = pViewports->TopLeftY;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetViewports.MinDepth = pViewports->MinDepth;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetViewports.MaxDepth = pViewports->MaxDepth;

	m_queue_array_back += BufferOffsetForQueueItemType<RSSetViewportsArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::RSSetScissorRects(
	_In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT Count,
	_In_reads_(Count)  const D3D12_RECT* pRects
	)
{
	DEBUGCHECK(Count == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::RSSetScissorRects;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetScissorRects.bottom = pRects->bottom;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetScissorRects.left = pRects->left;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetScissorRects.right = pRects->right;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->RSSetScissorRects.top = pRects->top;

	m_queue_array_back += BufferOffsetForQueueItemType<RSSetScissorRectsArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::OMSetBlendFactor(
	_In_opt_  const FLOAT BlendFactor[4]
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::OMSetStencilRef(
	_In_  UINT StencilRef
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPipelineState(
	_In_  ID3D12PipelineState* pPipelineState
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(m_queue_array_back);

	item->Type = D3DQueueItemType::SetPipelineState;
	item->SetPipelineState.pPipelineStateObject = pPipelineState;

	m_queue_array_back += BufferOffsetForQueueItemType<SetPipelineStateArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ResourceBarrier(
	_In_  UINT NumBarriers,
	_In_reads_(NumBarriers)  const D3D12_RESOURCE_BARRIER* pBarriers
	)
{
	DEBUGCHECK(NumBarriers == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ResourceBarrier;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResourceBarrier.barrier = *pBarriers;

	m_queue_array_back += BufferOffsetForQueueItemType<ResourceBarrierArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ExecuteBundle(
	_In_  ID3D12GraphicsCommandList *pCommandList
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::BeginQuery(
	_In_  ID3D12QueryHeap* pQueryHeap,
	_In_  D3D12_QUERY_TYPE Type,
	_In_  UINT Index
	)
{
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::BeginQuery;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->BeginQuery.pQueryHeap = pQueryHeap;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->BeginQuery.Type = Type;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->BeginQuery.Index = Index;

	m_queue_array_back += BufferOffsetForQueueItemType<BeginQueryArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::EndQuery(
	_In_  ID3D12QueryHeap* pQueryHeap,
	_In_  D3D12_QUERY_TYPE Type,
	_In_  UINT Index
	)
{
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::EndQuery;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->EndQuery.pQueryHeap = pQueryHeap;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->EndQuery.Type = Type;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->EndQuery.Index = Index;

	m_queue_array_back += BufferOffsetForQueueItemType<EndQueryArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ResolveQueryData(
	_In_  ID3D12QueryHeap* pQueryHeap,
	_In_  D3D12_QUERY_TYPE Type,
	_In_  UINT StartElement,
	_In_  UINT ElementCount,
	_In_  ID3D12Resource* pDestinationBuffer,
	_In_  UINT64 AlignedDestinationBufferOffset
	)
{
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ResolveQueryData;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveQueryData.pQueryHeap = pQueryHeap;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveQueryData.Type = Type;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveQueryData.StartElement = StartElement;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveQueryData.ElementCount = ElementCount;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveQueryData.pDestinationBuffer = pDestinationBuffer;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ResolveQueryData.AlignedDestinationBufferOffset = AlignedDestinationBufferOffset;

	m_queue_array_back += BufferOffsetForQueueItemType<ResolveQueryDataArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetPredication(
	_In_opt_  ID3D12Resource* pBuffer,
	_In_  UINT64 AlignedBufferOffset,
	_In_  D3D12_PREDICATION_OP Operation
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetDescriptorHeaps(
	_In_  UINT NumDescriptorHeaps,
	_In_reads_(NumDescriptorHeaps) ID3D12DescriptorHeap *const * pDescriptorHeaps
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::SetDescriptorHeaps;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetDescriptorHeaps.ppDescriptorHeap = pDescriptorHeaps;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetDescriptorHeaps.NumDescriptorHeaps = NumDescriptorHeaps;

	m_queue_array_back += BufferOffsetForQueueItemType<SetDescriptorHeapsArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootSignature(
	_In_  ID3D12RootSignature* pRootSignature
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootSignature(
	_In_  ID3D12RootSignature* pRootSignature
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::SetGraphicsRootSignature;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetGraphicsRootSignature.pRootSignature = pRootSignature;

	m_queue_array_back += BufferOffsetForQueueItemType<SetGraphicsRootSignatureArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootDescriptorTable(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootDescriptorTable(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(m_queue_array_back);

	item->Type = D3DQueueItemType::SetGraphicsRootDescriptorTable;
	item->SetGraphicsRootDescriptorTable.RootParameterIndex = RootParameterIndex;
	item->SetGraphicsRootDescriptorTable.BaseDescriptor = BaseDescriptor;

	m_queue_array_back += BufferOffsetForQueueItemType<SetGraphicsRootDescriptorTableArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRoot32BitConstant(
	_In_  UINT RootParameterIndex,
	_In_  UINT SrcData,
	_In_  UINT DestOffsetIn32BitValues
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRoot32BitConstant(
	_In_  UINT RootParameterIndex,
	_In_  UINT SrcData,
	_In_  UINT DestOffsetIn32BitValues
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRoot32BitConstants(
	_In_  UINT RootParameterIndex,
	_In_  UINT Num32BitValuesToSet,
	_In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void* pSrcData,
	_In_  UINT DestOffsetIn32BitValues
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRoot32BitConstants(
	_In_  UINT RootParameterIndex,
	_In_  UINT Num32BitValuesToSet,
	_In_reads_(Num32BitValuesToSet*sizeof(UINT))  const void* pSrcData,
	_In_  UINT DestOffsetIn32BitValues
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootConstantBufferView(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	D3DQueueItem* item = reinterpret_cast<D3DQueueItem*>(m_queue_array_back);

	item->Type = D3DQueueItemType::SetGraphicsRootConstantBufferView;
	item->SetGraphicsRootConstantBufferView.RootParameterIndex = RootParameterIndex;
	item->SetGraphicsRootConstantBufferView.BufferLocation = BufferLocation;

	m_queue_array_back += BufferOffsetForQueueItemType<SetGraphicsRootConstantBufferViewArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootConstantBufferView(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootShaderResourceView(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootShaderResourceView(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetComputeRootUnorderedAccessView(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetGraphicsRootUnorderedAccessView(
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS DescriptorHandle
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::IASetIndexBuffer(
	_In_opt_  const D3D12_INDEX_BUFFER_VIEW* pDesc
	)
{
	// No ignored parameters, no assumptions to DEBUGCHECK.

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::SetIndexBuffer;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetIndexBuffer.desc = *pDesc;

	m_queue_array_back += BufferOffsetForQueueItemType<SetIndexBufferArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::IASetVertexBuffers(
	_In_  UINT StartSlot,
	_In_  UINT NumBuffers,
	_In_  const D3D12_VERTEX_BUFFER_VIEW* pDesc
	)
{
	DEBUGCHECK(StartSlot == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(NumBuffers == 1, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::SetVertexBuffers;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetVertexBuffers.desc = *pDesc;

	m_queue_array_back += BufferOffsetForQueueItemType<SetVertexBuffersArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SOSetTargets(
	_In_  UINT StartSlot,
	_In_  UINT NumViews,
	_In_  const D3D12_STREAM_OUTPUT_BUFFER_VIEW* pViews
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::OMSetRenderTargets(
	_In_  UINT NumRenderTargetDescriptors,
	_In_  const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
	_In_  BOOL RTsSingleHandleToDescriptorRange,
	_In_opt_  const D3D12_CPU_DESCRIPTOR_HANDLE *pDepthStencilDescriptor
	)
{
	DEBUGCHECK(RTsSingleHandleToDescriptorRange == FALSE, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::SetRenderTargets;

	if (pRenderTargetDescriptors)
		reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetRenderTargets.RenderTargetDescriptor = *pRenderTargetDescriptors;
	else
		reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetRenderTargets.RenderTargetDescriptor = {};

	if (pDepthStencilDescriptor)
		reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetRenderTargets.DepthStencilDescriptor = *pDepthStencilDescriptor;
	else
		reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->SetRenderTargets.DepthStencilDescriptor = {};

	m_queue_array_back += BufferOffsetForQueueItemType<SetRenderTargetsArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearDepthStencilView(
	_In_  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView,
	_In_  D3D12_CLEAR_FLAGS ClearFlags,
	_In_  FLOAT Depth,
	_In_  UINT8 Stencil,
	_In_  UINT NumRects,
	_In_reads_opt_(NumRects)  const D3D12_RECT* pRect
	)
{
	DEBUGCHECK(ClearFlags == D3D12_CLEAR_FLAG_DEPTH, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(Depth == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(Stencil == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(pRect == nullptr, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(NumRects == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ClearDepthStencilView;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ClearDepthStencilView.DepthStencilView = DepthStencilView;

	m_queue_array_back += BufferOffsetForQueueItemType<ClearDepthStencilViewArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearRenderTargetView(
	_In_  D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
	_In_  const FLOAT ColorRGBA[4],
	_In_  UINT NumRects,
	_In_reads_opt_(NumRects)  const D3D12_RECT* pRects
	)
{
	DEBUGCHECK(ColorRGBA[0] == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(ColorRGBA[1] == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(ColorRGBA[2] == 0.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(ColorRGBA[3] == 1.0f, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(pRects == nullptr, "Error: Invalid assumption in ID3D12QueuedCommandList.");
	DEBUGCHECK(NumRects == 0, "Error: Invalid assumption in ID3D12QueuedCommandList.");

	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->Type = D3DQueueItemType::ClearRenderTargetView;
	reinterpret_cast<D3DQueueItem*>(m_queue_array_back)->ClearRenderTargetView.RenderTargetView = RenderTargetView;

	m_queue_array_back += BufferOffsetForQueueItemType<ClearRenderTargetViewArguments>();

	CheckForOverflow();
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearUnorderedAccessViewUint(
	_In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
	_In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
	_In_  ID3D12Resource* pResource,
	_In_  const UINT Values[4],
	_In_  UINT NumRects,
	_In_reads_opt_(NumRects)  const D3D12_RECT* pRects
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ClearUnorderedAccessViewFloat(
	_In_  D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap,
	_In_  D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle,
	_In_  ID3D12Resource* pResource,
	_In_  const FLOAT Values[4],
	_In_  UINT NumRects,
	_In_reads_opt_(NumRects)  const D3D12_RECT* pRects
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::DiscardResource(
	_In_  ID3D12Resource* pResource,
	_In_opt_  const D3D12_DISCARD_REGION* pDesc
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::SetMarker(
	UINT Metadata,
	_In_reads_bytes_opt_(Size)  const void* pData,
	UINT Size
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::BeginEvent(
	UINT Metadata,
	_In_reads_bytes_opt_(Size)  const void* pData,
	UINT Size
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::EndEvent()
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

void STDMETHODCALLTYPE ID3D12QueuedCommandList::ExecuteIndirect(
	_In_  ID3D12CommandSignature* pCommandSignature,
	_In_  UINT MaxCommandCount,
	_In_  ID3D12Resource* pArgumentBuffer,
	_In_  UINT64 ArgumentBufferOffset,
	_In_opt_  ID3D12Resource* pCountBuffer,
	_In_  UINT64 CountBufferOffset
	)
{
	// Function not implemented yet.
	DEBUGCHECK(0, "Function not implemented yet.");
}

}  // namespace DX12