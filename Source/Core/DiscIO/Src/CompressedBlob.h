// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.


// WARNING Code not big-endian safe.

// To create new compressed BLOBs, use CompressFileToBlob.

// File format
// * Header
// * [Block Pointers interleaved with block hashes (hash of decompressed data)]
// * [Data]

#ifndef COMPRESSED_BLOB_H_
#define COMPRESSED_BLOB_H_

#include <string>

#include "Blob.h"
#include "FileUtil.h"

namespace DiscIO
{

bool IsCompressedBlob(const char* filename);

const u32 kBlobCookie = 0xB10BC001;

// A blob file structure:
// BlobHeader
// u64 offsetsToBlocks[n], top bit specifies whether the block is compressed, or not.
// compressed data

// Blocks that won't compress to less than 97% of the original size are stored as-is.
struct CompressedBlobHeader // 32 bytes
{
	u32 magic_cookie; //0xB10BB10B
	u32 sub_type; // gc image, whatever
	u64 compressed_data_size;
	u64 data_size;
	u32 block_size;
	u32 num_blocks;
};

class CompressedBlobReader : public SectorReader
{
public:
	static CompressedBlobReader* Create(const char *filename);
	~CompressedBlobReader();
	const CompressedBlobHeader &GetHeader() const { return header; }
	u64 GetDataSize() const { return header.data_size; }
	u64 GetRawSize() const { return file_size; }
	u64 GetBlockCompressedSize(u64 block_num) const;
	void GetBlock(u64 block_num, u8 *out_ptr);
private:
	CompressedBlobReader(const char *filename);

	CompressedBlobHeader header;
	u64 *block_pointers;
	u32 *hashes;
	int data_offset;
	File::IOFile m_file;
	u64 file_size;
	u8 *zlib_buffer;
	int zlib_buffer_size;
	std::string file_name;
};

}  // namespace

#endif  // COMPRESSED_BLOB_H_
