// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _FIFORECORDANALYZER_H_
#define _FIFORECORDANALYZER_H_

#include "FifoAnalyzer.h"

#include "Common.h"

#include "BPMemory.h"

class FifoRecordAnalyzer
{
public:
	FifoRecordAnalyzer();

	// Must call this before analyzing GP commands
	void Initialize(u32 *bpMem, u32 *cpMem);

	// Assumes data contains all information for the command
	// Calls FifoRecorder::WriteMemory
	void AnalyzeGPCommand(u8 *data);

private:
	void DecodeOpcode(u8 *data);

	void ProcessLoadTlut1();
	void ProcessPreloadTexture();
	void ProcessLoadIndexedXf(u32 val, int array);
	void ProcessVertexArrays(u8 *data, u8 vtxAttrGroup);
	void ProcessTexMaps();

	void WriteVertexArray(int arrayIndex, u8 *vertexData, int vertexSize, int numVertices);
	void WriteTexMapMemory(int texMap, u32 &writtenTexMaps);

	bool m_DrawingObject;

	BPMemory *m_BpMem;
	FifoAnalyzer::CPMemory m_CpMem;	
};

#endif
