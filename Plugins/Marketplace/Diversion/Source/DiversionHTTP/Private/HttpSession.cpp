// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "HttpSession.h"
#include "Types.h"
#include "DiversionHttpModule.h"
#include "BoostHeaders.h"
#include "Misc/Compression.h"

#include <zlib.h>
#include <stdio.h>

bool DecompressGzipWithZlib(const std::string& CompressedFilePath, const std::string& DecompressedFilePath, std::string& OutError)
{
	// Open compressed (GZIP) file
	std::ifstream CompressedFile(CompressedFilePath, std::ios::binary);
	if (!CompressedFile.is_open())
	{
		OutError = "Failed to open compressed file: " + CompressedFilePath;
		return false;
	}

	// Open output file
	std::ofstream DecompressedFile(DecompressedFilePath, std::ios::binary);
	if (!DecompressedFile.is_open())
	{
		OutError = "Failed to open decompressed file for writing: " + DecompressedFilePath;
		return false;
	}

	// Setup zlib's inflate stream
	z_stream Strm;
	std::memset(&Strm, 0, sizeof(z_stream));

	// 16 + MAX_WBITS enables GZIP decoding (instead of raw deflate)
	int Ret = inflateInit2(&Strm, 16 + MAX_WBITS);
	if (Ret != Z_OK)
	{
		OutError =  "inflateInit2 failed with code: " + std::to_string(Ret);
		return false;
	}

	constexpr std::size_t InBufferSize = 4096;  // Size of the read buffer
	constexpr std::size_t OutBufferSize = 4096;  // Size of the decompressed buffer

	std::vector<unsigned char> InBuffer(InBufferSize);
	std::vector<unsigned char> OutBuffer(OutBufferSize);

	bool bSuccess = true;

	// Read the compressed file in chunks
	while (!CompressedFile.eof())
	{
		CompressedFile.read(reinterpret_cast<char*>(InBuffer.data()), InBufferSize);
		std::streamsize BytesRead = CompressedFile.gcount();
		if (BytesRead <= 0)
		{
			break;
		}

		// Supply input to zlib
		Strm.next_in = InBuffer.data();
		Strm.avail_in = static_cast<uInt>(BytesRead);

		// Decompress until we've consumed all input from this chunk
		while (Strm.avail_in > 0)
		{
			Strm.next_out = OutBuffer.data();
			Strm.avail_out = static_cast<uInt>(OutBufferSize);

			// Decompress
			Ret = inflate(&Strm, Z_NO_FLUSH);
			if (Ret == Z_STREAM_ERROR || Ret == Z_DATA_ERROR || Ret == Z_MEM_ERROR)
			{
				OutError = "inflate failed with error code: " + std::to_string(Ret);
				bSuccess = false;
				break;
			}

			// Calculate how many bytes were decompressed into OutBuffer
			std::size_t BytesDecompressed = OutBufferSize - Strm.avail_out;

			// Write those decompressed bytes to the output file
			DecompressedFile.write(reinterpret_cast<const char*>(OutBuffer.data()), BytesDecompressed);

			// If inflate indicates we're done or an error, break out
			if (Ret == Z_STREAM_END)
			{
				break;
			}
		} // end while (Strm.avail_in > 0)

		if (!bSuccess || Ret == Z_STREAM_END)
		{
			break;
		}
	}

	// Clean up
	inflateEnd(&Strm);

	CompressedFile.close();
	DecompressedFile.close();

	std::remove(CompressedFilePath.c_str());

	if (bSuccess)
	{
		UE_LOG(LogDiversionHttp, Display, TEXT("Successfully decompressed file to: %hs"), DecompressedFilePath.c_str());
	}

	return bSuccess;
}

void ConvertHttpResponseToTArray(const http::response<http::string_body>& HttpResponse, TArray<uint8>& OutArray)
{
	const std::string& Body = HttpResponse.body();
	OutArray.Reserve(Body.size());
	OutArray.Append(reinterpret_cast<const uint8*>(Body.data()), Body.size());
}

TArray<uint8> DecompressGzipFromArray(const TArray<uint8>& CompressedData, int32 ExpectedDecompressedSize)
{
	TArray<uint8> UncompressedData;
	UncompressedData.SetNum(ExpectedDecompressedSize);

	bool bSuccess = FCompression::UncompressMemory(
		NAME_Gzip,
		UncompressedData.GetData(),
		ExpectedDecompressedSize,
		CompressedData.GetData(),
		CompressedData.Num()
	);

	if (!bSuccess)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Decompression failed!"));
		return TArray<uint8>();
	}

	// Convert decompressed data to FString
	return UncompressedData;
}

uint32 GetUncompressedSizeFromGzip(const TArray<uint8>& GzipData)
{
	// Ensure the data size is at least large enough for a valid GZIP footer
	if (GzipData.Num() < 4)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Invalid GZIP data: too small to contain uncompressed size."));
		return 0;
	}

	// Read the last 4 bytes of the array
	const uint8* Footer = &GzipData[GzipData.Num() - 4];

	// Convert the last 4 bytes to a uint32 (little-endian format)
	uint32 UncompressedSize = 0;
	UncompressedSize |= Footer[0];
	UncompressedSize |= Footer[1] << 8;
	UncompressedSize |= Footer[2] << 16;
	UncompressedSize |= Footer[3] << 24;

	return UncompressedSize;
}
