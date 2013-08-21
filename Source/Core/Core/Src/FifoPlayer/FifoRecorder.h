// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _FIFORECORDER_H_
#define _FIFORECORDER_H_

#include "FifoDataFile.h"
#include "FifoRecordAnalyzer.h"

class FifoRecorder
{
public:
	typedef void(*CallbackFunc)(void);

	FifoRecorder();
	~FifoRecorder();

	void StartRecording(s32 numFrames, CallbackFunc finishedCb);
	void StopRecording();

	FifoDataFile *GetRecordedFile() { return m_File; }

	// Called from video thread

	// Must write one full GP command at a time
	void WriteGPCommand(u8 *data, u32 size);

	void WriteMemory(u32 address, u32 size, MemoryUpdate::Type type);
	
	void EndFrame(u32 fifoStart, u32 fifoEnd);

	// This function must be called before writing GP commands
	// bpMem must point to the actual bp mem array used by the plugin because it will be read as fifo data is recorded
	void SetVideoMemory(u32 *bpMem, u32 *cpMem, u32 *xfMem, u32 *xfRegs, u32 xfRegsSize);

	// Checked once per frame prior to callng EndFrame()
	bool IsRecording() { return m_IsRecording; }

	static FifoRecorder &GetInstance();

private:
	// Accessed from both GUI and video threads

	// True if video thread should send data
	volatile bool m_IsRecording;
	// True if m_IsRecording was true during last frame
	volatile bool m_WasRecording;
	volatile bool m_RequestedRecordingEnd;
	volatile s32 m_RecordFramesRemaining;
	volatile CallbackFunc m_FinishedCb;

	FifoDataFile *volatile m_File;

	// Accessed only from video thread

	bool m_SkipNextData;
	bool m_SkipFutureData;
	bool m_FrameEnded;
	FifoFrameInfo m_CurrentFrame;
	std::vector<u8> m_FifoData;
	u8 *m_Ram;
	u8 *m_ExRam;
	FifoRecordAnalyzer m_RecordAnalyzer;
};

#endif
