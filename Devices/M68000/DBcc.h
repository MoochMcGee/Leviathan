#include "M68000Instruction.h"
#ifndef __M68000_DBcc_H__
#define __M68000_DBcc_H__
namespace M68000 {

class DBcc :public M68000Instruction
{
public:
	virtual DBcc* Clone() const {return new DBcc();}
	virtual DBcc* ClonePlacement(void* buffer) const {return new(buffer) DBcc();}
	virtual size_t GetOpcodeClassByteSize() const {return sizeof(*this);}

	virtual bool RegisterOpcode(OpcodeTable<M68000Instruction>& table) const
	{
		return table.AllocateRegionToOpcode(this, L"0101****11001***", L"");
	}

	virtual std::wstring GetOpcodeName() const
	{
		return L"DBcc";
	}

	virtual Disassembly M68000Disassemble(const M68000::LabelSubstitutionSettings& labelSettings) const
	{
		return Disassembly(L"DB" + DisassembleConditionCode(conditionCode), source.Disassemble(labelSettings) + L", " + target.DisassembleImmediateAsPCDisplacement(labelSettings), target.DisassembleImmediateAsPCDisplacementTargetAddressString());
	}

	virtual void M68000Decode(const M68000* cpu, const M68000Long& location, const M68000Word& data, bool transparent)
	{
//	-----------------------------------------------------------------
//	|15 |14 |13 |12 |11 |10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
//	|---|---|---|---|---------------|---|---|---|---|---|-----------|
//	| 0 | 1 | 0 | 1 |   CONDITION   | 1 | 1 | 0 | 0 | 1 | REGISTER  |
//	|---------------------------------------------------------------|
//	|                      16 BITS OFFSET (d16)                     |
//	-----------------------------------------------------------------
		conditionCode = (ConditionCode)data.GetDataSegment(8, 4);

		//DBcc	Dn,<label>
		source.BuildDataDirect(BITCOUNT_WORD, location + GetInstructionSize(), data.GetDataSegment(0, 3));
		target.BuildImmediateData(BITCOUNT_WORD, location + GetInstructionSize(), cpu, transparent, GetInstructionRegister());
		AddInstructionSize(target.ExtensionSize());
	}

	virtual ExecuteTime M68000Execute(M68000* cpu, const M68000Long& location) const
	{
		double additionalTime = 0;
		M68000Long newPC;

		//Test the condition code
		bool result = ConditionCodeTrue(cpu, conditionCode);

		ExecuteTime additionalCycles;
		if(!result)
		{
			//If the condition is false, prepare for another loop
			M68000Word counter;
			additionalTime += source.Read(cpu, counter, GetInstructionRegister());
			--counter;
			additionalTime += source.Write(cpu, counter, GetInstructionRegister());
			if(counter != counter.GetMaxValue())
			{
				//The counter has been decremented and no overflow has occurred. Branch
				//to the target location and run the loop again.
				M68000Word offset;
				additionalTime += target.Read(cpu, offset, GetInstructionRegister());
				newPC = target.GetSavedPC() + M68000Long(offset.SignExtend(BITCOUNT_LONG));
				additionalCycles.Set(10, 2, 0);
			}
			else
			{
				//The counter has overflowed. Continue execution at the next instruction.
				newPC = location + GetInstructionSize();
				additionalCycles.Set(14, 3, 0);
			}
		}
		else
		{
			//If the condition is true, skip the branch, and continue execution at the
			//next instruction.
			newPC = location + GetInstructionSize();
			additionalCycles.Set(12, 2, 0);
		}
		cpu->SetPC(newPC);

		//Return the execution time
		return GetExecuteCycleCount(additionalTime) + additionalCycles;
	}

	virtual void GetResultantPCLocations(std::set<unsigned int>& resultantPCLocations, bool& undeterminedResultantPCLocation) const
	{
		//Return the address directly after this opcode, and the possible branch location
		//from executing this opcode, as the possible resultant PC locations from
		//executing this opcode.
		undeterminedResultantPCLocation = false;
		unsigned int nextOpcodeAddress = GetInstructionLocation().GetData() + GetInstructionSize();
		unsigned int branchOpcodeAddress = (target.GetSavedPC() + target.ExtractProcessedImmediateData()).GetData();
		resultantPCLocations.insert(nextOpcodeAddress);
		resultantPCLocations.insert(branchOpcodeAddress);
	}

	virtual void GetLabelTargetLocations(std::set<unsigned int>& labelTargetLocations) const
	{
		unsigned int branchOpcodeAddress = (target.GetSavedPC() + target.ExtractProcessedImmediateData()).GetData();
		labelTargetLocations.insert(branchOpcodeAddress);
	}

private:
	ConditionCode conditionCode;
	EffectiveAddress source;
	EffectiveAddress target;
};

} //Close namespace M68000
#endif
