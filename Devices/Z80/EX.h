#include "Z80Instruction.h"
#ifndef __Z80_EX_H__
#define __Z80_EX_H__
namespace Z80 {

class EX :public Z80Instruction
{
public:
	virtual EX* Clone() const {return new EX();}
	virtual EX* ClonePlacement(void* buffer) const {return new(buffer) EX();}
	virtual size_t GetOpcodeClassByteSize() const {return sizeof(*this);}

	virtual bool RegisterOpcode(OpcodeTable<Z80Instruction>& table) const
	{
		bool result = true;
		result &= table.AllocateRegionToOpcode(this, L"1110*011", L"");
		result &= table.AllocateRegionToOpcode(this, L"00001000", L"");
		return result;
	}

	virtual std::wstring GetOpcodeName() const
	{
		return L"EX";
	}

	virtual Disassembly Z80Disassemble(const Z80::LabelSubstitutionSettings& labelSettings) const
	{
		return Disassembly(GetOpcodeName(), target.Disassemble() + L", " + source.Disassemble());
	}

	virtual void Z80Decode(const Z80* cpu, const Z80Word& location, const Z80Byte& data, bool transparent)
	{
		source.SetIndexState(GetIndexState(), GetIndexOffset());
		target.SetIndexState(GetIndexState(), GetIndexOffset());

		if(!data.GetBit(0))
		{
			//EX AF,AF'		00001000
			source.SetMode(EffectiveAddress::Mode::AF);
			target.SetMode(EffectiveAddress::Mode::AF2);
			AddExecuteCycleCount(4);
		}
		else if(data.GetBit(3))
		{
			//EX DE,HL		11101011
			source.SetMode(EffectiveAddress::Mode::DE);
			//##NOTE## We override the index state for the target in this form of the
			//opcode. The only other opcode which has exceptions to the indexing rule
			//is LD8.
			target.SetIndexState(EffectiveAddress::IndexState::None, 0);
			target.SetMode(EffectiveAddress::Mode::HL);
			AddExecuteCycleCount(4);
		}
		else
		{
			//EX (SP),HL		11100011
			//EX (SP),IX		11011101 11100011
			//EX (SP),IY		11111101 11100011
			source.SetMode(EffectiveAddress::Mode::SPIndirect);
			target.SetMode(EffectiveAddress::Mode::HL);
			AddExecuteCycleCount(19);
		}

		AddInstructionSize(GetIndexOffsetSize(source.UsesIndexOffset() || target.UsesIndexOffset()));
		AddInstructionSize(source.ExtensionSize());
		AddInstructionSize(target.ExtensionSize());
	}

	virtual ExecuteTime Z80Execute(Z80* cpu, const Z80Word& location) const
	{
		double additionalTime = 0;
		Z80Word op1;
		Z80Word op2;

		//Perform the operation
		additionalTime += source.Read(cpu, location, op1);
		additionalTime += target.Read(cpu, location, op2);
		additionalTime += source.Write(cpu, location, op2);
		additionalTime += target.Write(cpu, location, op1);

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
