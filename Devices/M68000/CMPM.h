#include "M68000Instruction.h"
#ifndef __M68000_CMPM_H__
#define __M68000_CMPM_H__
namespace M68000 {

class CMPM :public M68000Instruction
{
public:
	virtual CMPM* Clone() const {return new CMPM();}
	virtual CMPM* ClonePlacement(void* buffer) const {return new(buffer) CMPM();}
	virtual size_t GetOpcodeClassByteSize() const {return sizeof(*this);}

	virtual bool RegisterOpcode(OpcodeTable<M68000Instruction>& table) const
	{
		return table.AllocateRegionToOpcode(this, L"1011***1CC001***", L"CC=00-10");
	}

	virtual std::wstring GetOpcodeName() const
	{
		return L"CMPM";
	}

	virtual Disassembly M68000Disassemble(const M68000::LabelSubstitutionSettings& labelSettings) const
	{
		return Disassembly(GetOpcodeName() + L"." + DisassembleSize(size), source.Disassemble(labelSettings) + L", " + target.Disassemble(labelSettings));
	}

	virtual void M68000Decode(const M68000* cpu, const M68000Long& location, const M68000Word& data, bool transparent)
	{
//	-----------------------------------------------------------------
//	|15 |14 |13 |12 |11 |10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
//	|---|---|---|---|-----------|---|-------|---|---|---|-----------|
//	| 1 | 0 | 1 | 1 |Ax REGISTER| 1 | SIZE  | 0 | 0 | 1 |Ay REGISTER|
//	----------------------------------------=========================
//	                                        |----------<ea>---------|
		switch(data.GetDataSegment(6, 2))
		{
		case 0:	//00
			size = BITCOUNT_BYTE;
			break;
		case 1:	//01
			size = BITCOUNT_WORD;
			break;
		case 2:	//10
			size = BITCOUNT_LONG;
			break;
		}

		//CMPM	(Ay)+,(Ax)+
		source.BuildAddressPostinc(size, location + GetInstructionSize(), data.GetDataSegment(0, 3));
		target.BuildAddressPostinc(size, location + GetInstructionSize(), data.GetDataSegment(9, 3));

		if(size != BITCOUNT_LONG)
		{
			AddExecuteCycleCount(ExecuteTime(12, 3, 0));
		}
		else
		{
			AddExecuteCycleCount(ExecuteTime(20, 5, 0));
		}
	}

	virtual ExecuteTime M68000Execute(M68000* cpu, const M68000Long& location) const
	{
		double additionalTime = 0;
		Data op1(size);
		Data op2(size);
		Data result(size);

		//Perform the operation
		additionalTime += source.Read(cpu, op1, GetInstructionRegister());
		additionalTime += target.Read(cpu, op2, GetInstructionRegister());
		result = op2 - op1;

		//Set the flag results
		bool overflow = (op1.MSB() == result.MSB()) && (op2.MSB() != op1.MSB());
		bool borrow = (op1.MSB() && result.MSB()) || (!op2.MSB() && (op1.MSB() || result.MSB()));
		cpu->SetN(result.Negative());
		cpu->SetZ(result.Zero());
		cpu->SetV(overflow);
		cpu->SetC(borrow);

		//Adjust the PC and return the execution time
		cpu->SetPC(location + GetInstructionSize());
		return GetExecuteCycleCount(additionalTime);
	}

	virtual void GetLabelTargetLocations(std::set<unsigned int>& labelTargetLocations) const
	{
		source.AddLabelTargetsToSet(labelTargetLocations);
		target.AddLabelTargetsToSet(labelTargetLocations);
	}

private:
	EffectiveAddress source;
	EffectiveAddress target;
	Bitcount size;
};

} //Close namespace M68000
#endif
