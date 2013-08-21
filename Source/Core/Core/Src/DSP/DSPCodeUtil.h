// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _DSPCODEUTIL_H
#define _DSPCODEUTIL_H

#include <string>
#include <vector>

#include "Common.h"

bool Assemble(const char *text, std::vector<u16> &code, bool force = false);
bool Disassemble(const std::vector<u16> &code, bool line_numbers, std::string &text);
bool Compare(const std::vector<u16> &code1, const std::vector<u16> &code2);
void GenRandomCode(u32 size, std::vector<u16> &code);
void CodeToHeader(const std::vector<u16> &code, std::string _filename,
				  const char *name, std::string &header);
void CodesToHeader(const std::vector<u16> *codes, const std::vector<std::string> *filenames,
				   u32 numCodes, const char *name, std::string &header);

// Big-endian, for writing straight to file using File::WriteStringToFile.
void CodeToBinaryStringBE(const std::vector<u16> &code, std::string &str);
void BinaryStringBEToCode(const std::string &str, std::vector<u16> &code);

// Load code (big endian binary).
bool LoadBinary(const char *filename, std::vector<u16> &code);
bool SaveBinary(const std::vector<u16> &code, const char *filename);

#endif  // _DSPCODEUTIL_H
