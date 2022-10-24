#include "VuExecutor.h"
#include "VuBasicBlock.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

CVuExecutor::CVuExecutor(CMIPS& context, uint32 maxAddress)
    : CGenericMipsExecutor(context, maxAddress, BLOCK_CATEGORY_PS2_VU)
{
}

void CVuExecutor::Reset()
{
	m_cachedBlocks.clear();
	CGenericMipsExecutor::Reset();
}

BasicBlockPtr CVuExecutor::BlockFactory(CMIPS& context, uint32 begin, uint32 end)
{
	uint32 blockSize = ((end - begin) + 4) / 4;
	uint32 blockSizeByte = blockSize * 4;
	uint32* blockMemory = reinterpret_cast<uint32*>(alloca(blockSizeByte));
	for(uint32 address = begin; address <= end; address += 8)
	{
		uint32 index = (address - begin) / 4;

		uint32 addressLo = address + 0;
		uint32 addressHi = address + 4;

		uint32 opcodeLo = m_context.m_pMemoryMap->GetInstruction(addressLo);
		uint32 opcodeHi = m_context.m_pMemoryMap->GetInstruction(addressHi);

		assert((index + 0) < blockSize);
		blockMemory[index + 0] = opcodeLo;
		assert((index + 1) < blockSize);
		blockMemory[index + 1] = opcodeHi;
	}

	auto xxHash = XXH3_128bits(blockMemory, blockSizeByte);
	uint128 hash;
	memcpy(&hash, &xxHash, sizeof(xxHash));
	static_assert(sizeof(hash) == sizeof(xxHash));
	auto blockKey = std::make_pair(hash, end - begin);

	auto blockIterator = m_cachedBlocks.find(blockKey);
	if(blockIterator != std::end(m_cachedBlocks))
	{
		const auto& basicBlock(blockIterator->second);
		if(basicBlock->GetBeginAddress() == begin && basicBlock->GetEndAddress() == end)
		{
			return basicBlock;
		}
		else
		{
			auto result = std::make_shared<CVuBasicBlock>(context, begin, end, m_blockCategory);
			result->CopyFunctionFrom(basicBlock);
			return result;
		}
	}

	auto result = std::make_shared<CVuBasicBlock>(context, begin, end, m_blockCategory);
	result->Compile();
	m_cachedBlocks.insert(std::make_pair(blockKey, result));
	return result;
}

#define VU_UPPEROP_BIT_I (0x80000000)
#define VU_UPPEROP_BIT_E (0x40000000)

void CVuExecutor::PartitionFunction(uint32 startAddress)
{
	uint32 endAddress = startAddress + MAX_BLOCK_SIZE - 4;
	uint32 branchAddress = 0;
	for(uint32 address = startAddress; address < endAddress; address += 8)
	{
		uint32 addrLo = address + 0;
		uint32 addrHi = address + 4;
		uint32 lowerOp = m_context.m_pMemoryMap->GetInstruction(addrLo);
		uint32 upperOp = m_context.m_pMemoryMap->GetInstruction(addrHi);
		auto branchType = m_context.m_pArch->IsInstructionBranch(&m_context, addrLo, lowerOp);
		if(upperOp & VU_UPPEROP_BIT_E)
		{
			endAddress = address + 0xC;
			break;
		}
		else if(branchType == MIPS_BRANCH_NORMAL)
		{
			branchAddress = m_context.m_pArch->GetInstructionEffectiveAddress(&m_context, addrLo, lowerOp);
			endAddress = address + 0xC;
			break;
		}
		else if(branchType == MIPS_BRANCH_NODELAY)
		{
			//Should never happen
			assert(false);
		}
	}
	assert((endAddress - startAddress) <= MAX_BLOCK_SIZE);
	CreateBlock(startAddress, endAddress);
	auto block = static_cast<CVuBasicBlock*>(FindBlockStartingAt(startAddress));
	if(block->IsLinkable())
	{
		SetupBlockLinks(startAddress, endAddress, branchAddress);
	}
}
