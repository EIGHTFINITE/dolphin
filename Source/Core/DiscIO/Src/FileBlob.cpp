// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "FileBlob.h"

namespace DiscIO
{

PlainFileReader::PlainFileReader(std::FILE* file)
	: m_file(file)
{
	m_size = m_file.GetSize();
}

PlainFileReader* PlainFileReader::Create(const char* filename)
{
	File::IOFile f(filename, "rb");
	if (f)
		return new PlainFileReader(f.ReleaseHandle());
	else
		return NULL;
}

bool PlainFileReader::Read(u64 offset, u64 nbytes, u8* out_ptr)
{
	m_file.Seek(offset, SEEK_SET);
	return m_file.ReadBytes(out_ptr, nbytes);
}

}  // namespace
