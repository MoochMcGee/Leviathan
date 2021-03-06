#include "M68000Instruction.h"
#ifndef __M68000_NBCD_H__
#define __M68000_NBCD_H__
namespace M68000 {

class NBCD :public M68000Instruction
{
public:
	virtual NBCD* Clone() const {return new NBCD();}
	virtual NBCD* ClonePlacement(void* buffer) const {return new(buffer) NBCD();}
	virtual size_t GetOpcodeClassByteSize() const {return sizeof(*this);}

	virtual bool RegisterOpcode(OpcodeTable<M68000Instruction>& table) const
	{
		return table.AllocateRegionToOpcode(this, L"0100100000DDDDDD", L"DDDDDD=000000-000111,010000-110111,111000,111001");
	}

	virtual std::wstring GetOpcodeName() const
	{
		return L"NBCD";
	}

	virtual Disassembly M68000Disassemble(const M68000::LabelSubstitutionSettings& labelSettings) const
	{
		return Disassembly(GetOpcodeName(), target.Disassemble(labelSettings));
	}

	virtual void M68000Decode(const M68000* cpu, const M68000Long& location, const M68000Word& data, bool transparent)
	{
//	-----------------------------------------------------------------
//	|15 |14 |13 |12 |11 |10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
//	|---|---|---|---|---|---|---|---|---|---|-----------|-----------|
//	| 0 | 1 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 |    MODE   | REGISTER  |
//	----------------------------------------=========================
//	                                        |----------<ea>---------|
		target.Decode(data.GetDataSegment(0, 3), data.GetDataSegment(3, 3), BITCOUNT_BYTE, location + GetInstructionSize(), cpu, transparent, GetInstructionRegister());
		AddInstructionSize(target.ExtensionSize());

		if((target.GetAddressMode() == EffectiveAddress::Mode::DataRegDirect) || (target.GetAddressMode() == EffectiveAddress::Mode::AddRegDirect))
		{
			AddExecuteCycleCount(ExecuteTime(6, 1, 0));
		}
		else
		{
			AddExecuteCycleCount(ExecuteTime(8, 1, 1));
			AddExecuteCycleCount(target.DecodeTime());
		}
	}

	virtual ExecuteTime M68000Execute(M68000* cpu, const M68000Long& location) const
	{
		double additionalTime = 0;
		M68000Byte op1Base10;
		M68000Byte op1;
		M68000Byte result;

		//Perform the operation
		additionalTime += target.ReadWithoutAdjustingAddress(cpu, op1Base10, GetInstructionRegister());
		op1 = op1Base10.GetData() & 0x0F;
		op1 += ((op1Base10.GetData() & 0xF0) - (6 * (op1Base10.GetData() >> 4)));

		result = M68000Byte(0) - (op1 + cpu->GetX());

		M68000Byte resultBase10Temp = result;
		bool carry = false;
		if(resultBase10Temp > 99)
		{
			resultBase10Temp += 100;
			carry = true;
		}
		M68000Byte resultBase10;
		resultBase10 = resultBase10Temp.GetData() % 10;
		resultBase10 |= ((resultBase10Temp.GetData() / 10) % 10) << 4;
		additionalTime += target.Write(cpu, resultBase10, GetInstructionRegister());

		//Set the flag results
		cpu->SetX(carry);
		cpu->SetZ(cpu->GetZ() && resultBase10.Zero());
		cpu->SetC(carry);
		//##NOTE## Although the state of the N and V flags are officially undefined,
		//their behaviour on the M68000 is predictable, and is described in "68000
		//Undocumented Behavior Notes" by Bart Trzynadlowski.
		cpu->SetN(resultBase10.Negative());
		cpu->SetV(result.MSB() && !resultBase10.MSB());

		//Adjust the PC and return the execution time
		cpu->SetPC(location + GetInstructionSize());
		return GetExecuteCycleCount(additionalTime);
	}

	virtual void GetLabelTargetLocations(std::set<unsigned int>& labelTargetLocations) const
	{
		target.AddLabelTargetsToSet(labelTargetLocations);
	}

private:
	EffectiveAddress target;
};

} //Close namespace M68000
#endif
