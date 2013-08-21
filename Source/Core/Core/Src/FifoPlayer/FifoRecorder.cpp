// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "ConfigManager.h"
#include "Core.h"
#include "FifoRecorder.h"
#include "Thread.h"
#include "HW/Memmap.h"

static FifoRecorder instance;
static std::recursive_mutex sMutex;

using namespace std;

FifoRecorder::FifoRecorder() :
	m_IsRecording(false),
	m_WasRecording(false),
	m_RequestedRecordingEnd(false),
	m_RecordFramesRemaining(0),
	m_FinishedCb(NULL),
	m_File(NULL),
	m_SkipNextData(true),
	m_SkipFutureData(true),
	m_FrameEnded(false),	
	m_Ram(NULL),
	m_ExRam(NULL)
{
}

FifoRecorder::~FifoRecorder()
{
	m_IsRecording = false;
}

void FifoRecorder::StartRecording(s32 numFrames, CallbackFunc finishedCb)
{
	sMutex.lock();

	delete m_File;
	delete []m_Ram;
	delete []m_ExRam;

	m_File = new FifoDataFile;

	m_Ram = new u8[Memory::RAM_SIZE];
	m_ExRam = new u8[Memory::EXRAM_SIZE];
	memset(m_Ram, 0, Memory::RAM_SIZE);
	memset(m_ExRam, 0, Memory::EXRAM_SIZE);

	m_File->SetIsWii(SConfig::GetInstance().m_LocalCoreStartupParameter.bWii);

	if (!m_IsRecording)
	{
		m_WasRecording = false;
		m_IsRecording = true;		
		m_RecordFramesRemaining = numFrames;
	}

	m_RequestedRecordingEnd = false;
	m_FinishedCb = finishedCb;

	sMutex.unlock();
}

void FifoRecorder::StopRecording()
{
	m_RequestedRecordingEnd = true;
}

void FifoRecorder::WriteGPCommand(u8 *data, u32 size)
{
	if (!m_SkipNextData)
	{
		m_RecordAnalyzer.AnalyzeGPCommand(data);

		// Copy data to buffer
		u32 currentSize = m_FifoData.size();
		m_FifoData.resize(currentSize + size);
		memcpy(&m_FifoData[currentSize], data, size);
	}

	if (m_FrameEnded && m_FifoData.size() > 0)
	{
		u32 dataSize = m_FifoData.size();
		m_CurrentFrame.fifoDataSize = dataSize;
		m_CurrentFrame.fifoData = new u8[dataSize];
		memcpy(m_CurrentFrame.fifoData, &m_FifoData[0], dataSize);

		sMutex.lock();
		
		// Copy frame to file
		// The file will be responsible for freeing the memory allocated for each frame's fifoData
		m_File->AddFrame(m_CurrentFrame);

		if (m_FinishedCb && m_RequestedRecordingEnd)
			m_FinishedCb();

		sMutex.unlock();

		m_CurrentFrame.memoryUpdates.clear();
		m_FifoData.clear();
		m_FrameEnded = false;
	}

	m_SkipNextData = m_SkipFutureData;
}

void FifoRecorder::WriteMemory(u32 address, u32 size, MemoryUpdate::Type type)
{
	u8 *curData;
	u8 *newData;
	if (address & 0x10000000)
	{
		curData = &m_ExRam[address & Memory::EXRAM_MASK];
		newData = &Memory::m_pEXRAM[address & Memory::EXRAM_MASK];
	}
	else
	{
		curData = &m_Ram[address & Memory::RAM_MASK];
		newData = &Memory::m_pRAM[address & Memory::RAM_MASK];
	}

	if (memcmp(curData, newData, size) != 0)
	{
		// Update current memory
		memcpy(curData, newData, size);

		// Record memory update
		MemoryUpdate memUpdate;
		memUpdate.address = address;
		memUpdate.fifoPosition = m_FifoData.size();
		memUpdate.size = size;
		memUpdate.type = type;
		memUpdate.data = new u8[size];		
		memcpy(memUpdate.data, newData, size);

		m_CurrentFrame.memoryUpdates.push_back(memUpdate);
	}	
}

void FifoRecorder::EndFrame(u32 fifoStart, u32 fifoEnd)
{
	// m_IsRecording is assumed to be true at this point, otherwise this function would not be called

	sMutex.lock();

	m_FrameEnded = true;

	m_CurrentFrame.fifoStart = fifoStart;
	m_CurrentFrame.fifoEnd = fifoEnd;
		
	if (m_WasRecording)
	{
		// If recording a fixed number of frames then check if the end of the recording was reached
		if (m_RecordFramesRemaining > 0)
		{
			--m_RecordFramesRemaining;
			if (m_RecordFramesRemaining == 0)
				m_RequestedRecordingEnd = true;
		}
	}
	else
	{
		m_WasRecording = true;

		// Skip the first data which will be the frame copy command
		m_SkipNextData = true;
		m_SkipFutureData = false;

		m_FrameEnded = false;

		m_FifoData.reserve(1024 * 1024 * 4);
		m_FifoData.clear();
	}

	if (m_RequestedRecordingEnd)
	{
		// Skip data after the next time WriteFifoData is called
		m_SkipFutureData = true;
		// Signal video backend that it should not call this function when the next frame ends
		m_IsRecording = false;
	}

	sMutex.unlock();
}

void FifoRecorder::SetVideoMemory(u32 *bpMem, u32 *cpMem, u32 *xfMem, u32 *xfRegs, u32 xfRegsSize)
{
	sMutex.lock();

	if (m_File)
	{
		memcpy(m_File->GetBPMem(), bpMem, FifoDataFile::BP_MEM_SIZE * 4);
		memcpy(m_File->GetCPMem(), cpMem, FifoDataFile::CP_MEM_SIZE * 4);
		memcpy(m_File->GetXFMem(), xfMem, FifoDataFile::XF_MEM_SIZE * 4);

		u32 xfRegsCopySize = std::min((u32)FifoDataFile::XF_REGS_SIZE, xfRegsSize);
		memcpy(m_File->GetXFRegs(), xfRegs, xfRegsCopySize * 4);
	}

	m_RecordAnalyzer.Initialize(bpMem, cpMem);

	sMutex.unlock();
}

FifoRecorder &FifoRecorder::GetInstance()
{
	return instance;
}
