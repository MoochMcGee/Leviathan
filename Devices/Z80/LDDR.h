#include "Z80Instruction.h"
#ifndef __Z80_LDDR_H__
#define __Z80_LDDR_H__
namespace Z80 {

class LDDR :public Z80Instruction
{
public:
	virtual LDDR* Clone() const {return new LDDR();}
	virtual LDDR* ClonePlacement(void* buffer) const {return new(buffer) LDDR();}
	virtual size_t GetOpcodeClassByteSize() const {return sizeof(*this);}

	virtual bool RegisterOpcode(OpcodeTable<Z80Instruction>& table) const
	{
		return table.AllocateRegionToOpcode(this, L"10111000", L"");
	}

	virtual std::wstring GetOpcodeName() const
	{
		return L"LDDR";
	}

	virtual Disassembly Z80Disassemble(const Z80::LabelSubstitutionSettings& labelSettings) const
	{
		return Disassembly(GetOpcodeName(), L"");
	}

	virtual void Z80Decode(const Z80* cpu, const Z80Word& location, const Z80Byte& data, bool transparent)
	{
		source.SetIndexState(GetIndexState(), GetIndexOffset());
		target.SetIndexState(GetIndexState(), GetIndexOffset());

		//LDDR		11101101 10111000
		source.SetMode(EffectiveAddress::Mode::HLPostDec);
		target.SetMode(EffectiveAddress::Mode::DEPostDec);
		AddExecuteCycleCount(12);

		AddInstructionSize(GetIndexOffsetSize(source.UsesIndexOffset() || target.UsesIndexOffset()));
		AddInstructionSize(source.ExtensionSize());
		AddInstructionSize(target.ExtensionSize());
	}

	virtual ExecuteTime Z80Execute(Z80* cpu, const Z80Word& location) const
	{
		double additionalTime = 0;
		Z80Byte result;
		ExecuteTime additionalCycles;

		//Perform the operation
		additionalTime += source.Read(cpu, location, result);
		additionalTime += target.Write(cpu, location, result);
		cpu->SetBC(cpu->GetBC() - 1);

		//Set the flag results
		cpu->SetFlagH(false);
		cpu->SetFlagPV(cpu->GetBC() != 0);
		cpu->SetFlagN(false);

		Z80Byte currentRegA;
		cpu->GetA(currentRegA);
		Z80Byte regAResult = currentRegA + result;
		cpu->SetFlagY(regAResult.GetBit(1));
		cpu->SetFlagX(regAResult.GetBit(3));

		//Adjust the PC and return the execution time
		if(cpu->GetBC() == 0)
		{
			cpu->SetPC(location + GetInstructionSize());
		}
		else
		{
			additionalCycles.cycles = 5;
		}
		return GetExecuteCycleCount(additionalTime) + additionalCycles;
	}

private:
	EffectiveAddress source;
	EffectiveAddress target;
};

} //Close namespace Z80
#endif
