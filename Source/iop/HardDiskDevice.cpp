#include "HardDiskDevice.h"
#include <cassert>
#include "StringUtils.h"

using namespace Iop::Ioman;

Framework::CStream* CHardDiskDevice::GetFile(uint32 accessType, const char* devicePath)
{
	if(accessType == (OPEN_FLAG_RDWR | OPEN_FLAG_CREAT))
	{
		//Create a new partition
		auto createParams = StringUtils::Split(devicePath, ',', true);
		assert(createParams.size() == 5);
		return new CHardDiskPartition();
	}
	else
	{
		return nullptr;
	}
}

Directory CHardDiskDevice::GetDirectory(const char* devicePath)
{
	throw std::runtime_error("Blah");
}

void CHardDiskDevice::CreateDirectory(const char* devicePath)
{
	throw std::runtime_error("Blah");
}

//-----------------------------------------------
//CHardDiskPartition

void CHardDiskPartition::Seek(int64, Framework::STREAM_SEEK_DIRECTION)
{
	throw new std::runtime_error("Not supported.");
}

uint64 CHardDiskPartition::Read(void*, uint64)
{
	throw new std::runtime_error("Not supported.");
}

uint64 CHardDiskPartition::Write(const void*, uint64)
{
	throw new std::runtime_error("Not supported.");
}

uint64 CHardDiskPartition::Tell()
{
	return 0;
}

bool CHardDiskPartition::IsEOF()
{
	return false;
}