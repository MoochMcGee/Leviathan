#include "Z80Instruction.h"
#ifndef __Z80_SLA_H__
#define __Z80_SLA_H__
namespace Z80 {

class SLA :public Z80Instruction
{
public:
	virtual SLA* Clone() {return new SLA();}
	virtual SLA* ClonePlacement(void* buffer) {return new(buffer) SLA();}

	virtual bool RegisterOpcode(OpcodeTable<Z80Instruction>* table)
	{
		return table->AllocateRegionToOpcode(this, L"00100***", L"");
	}

	virtual Disassembly Z80Disassemble()
	{
		return Disassembly(L"SLA", target.Disassemble());
	}

	virtual void Z80Decode(Z80* cpu, const Z80Word& location, const Z80Byte& data, bool transparent)
	{
		target.SetIndexState(GetIndexState(), GetIndexOffset());
		doubleOutput = false;

		if(target.Decode8BitRegister(data.GetDataSegment(0, 3)))
		{
			//SLA r			11001011 00100rrr
			AddExecuteCycleCount(4);
		}
		else
		{
			//SLA (HL)		11001011 00100110
			//SLA (IX+d)	11011101 11001011 dddddddd 00100110
			//SLA (IY+d)	11111101 11001011 dddddddd 00100110
			target.SetMode(EffectiveAddress::MODE_HL_INDIRECT);
			AddExecuteCycleCount(11);

			if(GetIndexState() != EffectiveAddress::INDEX_NONE)
			{
				doubleOutput = true;
				targetHL.SetIndexState(GetIndexState(), GetIndexOffset());
				targetHL.SetMode(EffectiveAddress::MODE_HL_INDIRECT);
				AddExecuteCycleCount(4);
			}
		}

		AddInstructionSize(GetIndexOffsetSize(target.UsesIndexOffset()));
		AddInstructionSize(target.ExtensionSize());
	}

	virtual ExecuteTime Z80Execute(Z80* cpu, const Z80Word& location) const
	{
		double additionalTime = 0;
		Z80Byte op1;
		Z80Byte result;

		//Perform the operation
		if(doubleOutput)
		{
			additionalTime += targetHL.Read(cpu, location, op1);
		}
		else
		{
			additionalTime += target.Read(cpu, location, op1);
		}
		result = (op1 << 1);
		if(doubleOutput)
		{
			additionalTime += targetHL.Write(cpu, location, result);
		}
		additionalTime += target.Write(cpu, location, result);

		//Set the flag results
		cpu->SetFlagS(result.Negative());
		cpu->SetFlagZ(result.Zero());
		cpu->SetFlagY(result.GetBit(5));
		cpu->SetFlagH(false);
		cpu->SetFlagX(result.GetBit(3));
		cpu->SetFlagPV(result.ParityEven());
		cpu->SetFlagN(false);
		cpu->SetFlagC(op1.MSB());

		//Adjust the PC and return the execution time
		cpu->SetPC(location + GetInstructionSize());
		return GetExecuteCycleCount(additionalTime);
	}

private:
	EffectiveAddress target;
	EffectiveAddress targetHL;
	bool doubleOutput;
};

} //Close namespace Z80
#endif