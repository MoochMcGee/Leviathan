#ifndef __M68000INSTRUCTION_H__
#define __M68000INSTRUCTION_H__
#include "M68000.h"
#include "ExecuteTime.h"
#include "EffectiveAddress.h"
namespace M68000 {

class M68000Instruction
{
public:
	//Enumerations
	enum class ConditionCode;

	//Structures
	struct Disassembly;

	//Constructors
	inline M68000Instruction();
	inline M68000Instruction(const M68000Instruction& object);
	virtual ~M68000Instruction();

	//Clone functions
	virtual M68000Instruction* Clone() const = 0;
	virtual M68000Instruction* ClonePlacement(void* buffer) const = 0;
	virtual size_t GetOpcodeClassByteSize() const = 0;

	//Registration functions
	virtual bool RegisterOpcode(OpcodeTable<M68000Instruction>& table) const = 0;

	//Size functions
	inline unsigned int GetInstructionSize() const;
	inline void SetInstructionSize(unsigned int ainstructionSize);
	inline void AddInstructionSize(unsigned int ainstructionSize);

	//Condition code functions
	static bool ConditionCodeTrue(M68000* cpu, ConditionCode code);

	//Execute functions
	virtual void M68000Decode(const M68000* cpu, const M68000Long& location, const M68000Word& data, bool transparent) = 0;
	virtual ExecuteTime M68000Execute(M68000* cpu, const M68000Long& location) const = 0;

	//Execute time functions
	inline ExecuteTime GetExecuteCycleCount() const;
	inline ExecuteTime GetExecuteCycleCount(double additionalTime) const;
	inline void AddExecuteCycleCount(const ExecuteTime& aexecuteCycleCount);

	//Instruction info functions
	virtual bool Privileged() const;
	inline const M68000Word& GetInstructionRegister() const;
	inline void SetInstructionRegister(const M68000Word& ainstructionData);
	inline const M68000Long& GetInstructionLocation() const;
	inline void SetInstructionLocation(const M68000Long& ainstructionLocation);

	//Transparent decode functions
	inline bool GetTransparentFlag() const;
	inline void SetTransparentFlag(bool astate);

	//Disassembly functions
	virtual std::wstring GetOpcodeName() const;
	virtual Disassembly M68000Disassemble(const M68000::LabelSubstitutionSettings& labelSettings) const;
	virtual void GetResultantPCLocations(std::set<unsigned int>& resultantPCLocations, bool& undeterminedResultantPCLocation) const;
	virtual void GetLabelTargetLocations(std::set<unsigned int>& labelTargetLocations) const = 0;
	static std::wstring DisassembleConditionCode(ConditionCode code);
	static std::wstring DisassembleSize(Bitcount asize);

private:
	unsigned int instructionSize;
	ExecuteTime executeCycleCount;
	M68000Word instructionRegister;
	M68000Long instructionLocation;
	bool transparentOpcode;
};

} //Close namespace M68000
#include "M68000Instruction.inl"
#endif
