#pragma once

#include <zarchive/zarchivereader.h>

#include "Common/android/FilesystemAndroid.h"
#include "Common/android/FdStream.h"

namespace ZArchiveHelpers
{
	inline ZArchiveReader* OpenReader(const fs::path& path)
	{
		if (FilesystemAndroid::IsContentUri(path))
		{
			int fd = FilesystemAndroid::OpenContentUri(path);

			if (fd == -1)
				return nullptr;

			return ZArchiveReader::OpenFromStream(std::make_unique<FdStream>(fd));
		}

		return ZArchiveReader::OpenFromFile(path);
	}
} // namespace ZArchiveHelpers
