#include "Z80Instruction.h"
#ifndef __Z80_CPD_H__
#define __Z80_CPD_H__
namespace Z80 {

class CPD :public Z80Instruction
{
public:
	virtual CPD* Clone() const {return new CPD();}
	virtual CPD* ClonePlacement(void* buffer) const {return new(buffer) CPD();}
	virtual size_t GetOpcodeClassByteSize() const {return sizeof(*this);}

	virtual bool RegisterOpcode(OpcodeTable<Z80Instruction>& table) const
	{
		return table.AllocateRegionToOpcode(this, L"10101001", L"");
	}

	virtual std::wstring GetOpcodeName() const
	{
		return L"CPD";
	}

	virtual Disassembly Z80Disassemble(const Z80::LabelSubstitutionSettings& labelSettings) const
	{
		return Disassembly(GetOpcodeName(), L"");
	}

	virtual void Z80Decode(const Z80* cpu, const Z80Word& location, const Z80Byte& data, bool transparent)
	{
		source.SetIndexState(GetIndexState(), GetIndexOffset());
		target.SetIndexState(GetIndexState(), GetIndexOffset());

		//CPD		11101101 10101001
		source.SetMode(EffectiveAddress::Mode::HLPostDec);
		target.SetMode(EffectiveAddress::Mode::A);
		AddExecuteCycleCount(12);

		AddInstructionSize(GetIndexOffsetSize(source.UsesIndexOffset() || target.UsesIndexOffset()));
		AddInstructionSize(source.ExtensionSize());
		AddInstructionSize(target.ExtensionSize());
	}

	virtual ExecuteTime Z80Execute(Z80* cpu, const Z80Word& location) const
	{
		double additionalTime = 0;
		Z80Byte op1;
		Z80Byte op2;
		Z80Byte result;

		//Perform the operation
		additionalTime += source.Read(cpu, location, op1);
		additionalTime += target.Read(cpu, location, op2);
		result = op2 - op1;
		cpu->SetBC(cpu->GetBC() - 1);

		//Set the flag results
		cpu->SetFlagS(result.Negative());
		cpu->SetFlagZ(result.Zero());
		cpu->SetFlagH(op1.GetDataSegment(0, 4) > op2.GetDataSegment(0, 4));
		cpu->SetFlagPV(cpu->GetBC() != 0);
		cpu->SetFlagN(true);

		Z80Byte currentRegA = cpu->GetA();
		Z80Byte regAResult = currentRegA - (result + (unsigned int)cpu->GetFlagH());
		cpu->SetFlagY(regAResult.GetBit(1));
		cpu->SetFlagX(regAResult.GetBit(3));

		//Adjust the PC and return the execution time
		cpu->SetPC(location + GetInstructionSize());
		return GetExecuteCycleCount(additionalTime);
	}

private:
	EffectiveAddress source;
	EffectiveAddress target;
};

} //Close namespace Z80
#endif
