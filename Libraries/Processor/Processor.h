/*--------------------------------------------------------------------------------------*\
Things to do:
-Revamp the disassembly dialog so that it only disassembles the opcodes that are within
the window, and make it actively disassemble them on each refresh, so self-modifying code
will have the disassembly updates applied immediately.
-Our revamped disassembly window should show the actual data bytes which form the opcode
as an extra column.
-Note that one potential problem a disassembly window would have if we are to confine the
disassembly to the area inside the window, is that the base address from which we start
the disassembly from affects all the opcodes that follow it. Simply beginning the
disassembly from the current location of the window would often give us a bad opcode as
the first entry, as the latter part of a multi-word instruction is disassembled. This
effect could cascade onto following opcodes. The base address of the disassembly is
important. I suggest establishing a "safety buffer" before the actual contents of the
disassembly window, where instructions are disassembled, but not displayed. It may be
best to make this safety buffer configurable by the user, to increase the size, or turn
it off altogether if desired.
-Add support for adding and removing breakpoints within the disassembly window by
clicking on an area of the line where the breakpoint should be toggled. Suggest also
adding a ctrl+click feature where an existing breakpoint can be enabled or disabled when
clicked.
-Currently, the M68000 and Z80 disassembly windows display relative jump offsets as the
actual data which is stored in the opcode, even if that data is calculated based on an
incremented PC partway through the opcode. The actual assembly would represent such
relative jumps from the base address of the opcode. We should probably do the same. This
would require an enhancement to the EffectiveAddress class to store the base address of
the opcode, in addition to the base address of the extension word.
-Implement the "Enable All" and "Disable All" options for breakpoint/watchpoint lists
-Implement the "Save" and "Load" options for breakpoint/watchpoint lists

-Implement support for Python scripting, to extend the functionality of our active
disassembly process. In particular, we want custom scripts to be able to be written in
order to extend, format, tweak, etc the disassembly process. For example, we might have a
script for the Mega Drive to format the ROM header and disassemble the target of all the
vector table entries. We also definitely want a script to try and guess and disassemble
each entry in jump tables, but since this kind of analysis is highly subjective, and may
easily be improved or tweaked for particular cases, we don't want to hard-code it in our
emulator. This scripting support is critical in order to make disassembly most useful for
advanced users.

-Expand our plans for an active IDA disassembly link. Suggestions for data analysis:
#Each identified "block" of data should be flagged for where it was written to. Eg, if
a block of data was repeatedly read and and all of it was written to 0xC00000, that says
a lot. We should mark the data block as such.
#Data blocks of course need to be set to the size they were read as. Single continuous
blocks must always be read in the same format to be treated as such. These blocks will be
added to an array contruct.
#IDA scripts can read files. The output of our emulator should be a simple file which our
IDA script is able to read and interpret. Our script will be a basic algorithm to read
the target addresses and operations specified in the output file, and apply them as
appropriate.
#Some of the formatting we need to apply:
-Disassemble as code. This must be the first pass. All code must be disassembled before
any further processing.
-Format individual data value to size
-Optional build data value into array
-Optional build array into offset from specified base
-Every element must allow comments and labels to be specified
#In order to determine what is a continuous, discrete data block, I suggest we'll need
some very sophisticated tracking in place. It seems to me, on the M68000, the logical way
to identify individual data blocks is to monitor the address registers, and look for data
which is accessed via a post-incremented addressing mode. Even when the target data is
being "decoded" as part of a compression algorithm or the like, where there may be
multiple points within the decompression algorithm at which the data is read, the address
register which is tracking the data source will remain constant. In this scenario, we
just have to monitor the address registers. Data blocks will be identified based on the
usage of the address registers. Any time the register is manually written to, the current
data block will be considered to be complete. Note that reading the register must not
interrupt the data block analysis, as an algorithm may be testing the address register,
and terminate when it reaches a certain value.
#There is a more complex data analysis task which will be required in order to get good
results for many Mega Drive games, and that relates to cases where decompression is saved
partway through, and resumed later. This is the case for the Nemesis compression format
for example. This scenario would require much more complex tracking methods, where a data
block isn't closed off until the last copy of a reference to that block has been
discarded. In order to resume the decompression task, the source address register must be
saved to RAM, either as an individual write or as part of a MOVEM instruction. In order
for the entire archive to be correctly identified as a single data block, we would need
a way to realize that when that value is loaded back from RAM and the decompression
process resumes, we are continuing to parse the same data block we started earlier. We
also must ensure that the address value has not been modified in any way.
#To tackle the case of the Nemesis decompression algorithm, I suggest a system where we
track the location of all address values which have already initiated a data block
analysis, when they are copied from a register into memory. We must then track those
locations in memory. If any attempt is made to modify the saved register data, the
association must be broken. I also suggest a saved register should only be considered to
be resuming a previously established data block if it is loaded back directly into the
same register slot, and of course, if it is continued to be post-incremented.
#In order to prevent things like checksum routines throwing off our data analysis, we
will consider any data block identification to be unreliable if at any time during that
data block read, a known code location is read as data. This will result in the entire
block analysis being flagged as unreliable. Unreliable data block information will not be
used in our IDA script.

-Implement a link between the disassembly window and the breakpoint window, so
breakpoints can be toggled from the disassembly view.
-Consider a link between the code coverage data and the disassembly window, where the
disassembly is optionally linked to the code coverage data, and ensures opcodes which are
actually executed are disassembled correctly.
\*--------------------------------------------------------------------------------------*/
#ifndef __PROCESSOR_H__
#define __PROCESSOR_H__
#include "SystemInterface/SystemInterface.pkg"
#include "Device/Device.pkg"
#include <string>
#include <vector>
#include <list>
#include <set>
#include "Breakpoint.h"
#include "Watchpoint.h"
#include "ThinList/ThinList.pkg"
#include <boost/thread/mutex.hpp>

class Processor :public Device
{
public:
	//Structures
	struct OpcodeInfo;

public:
	//Constructors
	Processor(const std::wstring& aclassName, const std::wstring& ainstanceName, unsigned int amoduleID);
	~Processor();
	virtual bool Construct(IHeirarchicalStorageNode& node);

	//Execute functions
	virtual void Reset() = 0;
	virtual double ExecuteStep() = 0;
	virtual void ExecuteRollback();
	virtual void ExecuteCommit();
	virtual UpdateMethod GetUpdateMethod() const;

	//Control functions
	double GetClockSpeed() const;
	void SetClockSpeed(double aclockSpeed);
	void OverrideClockSpeed(double aclockSpeed);
	void RestoreClockSpeed();
	double CalculateExecutionTime(unsigned int cycles) const;

	//Instruction functions
	virtual OpcodeInfo GetOpcodeInfo(unsigned int location);
	virtual Data GetRawData(unsigned int location);
	virtual unsigned int GetCurrentPC() const = 0;
	virtual unsigned int GetPCWidth() const = 0;
	virtual unsigned int GetAddressBusWidth() const = 0;
	virtual unsigned int GetDataBusWidth() const = 0;
	virtual unsigned int GetMinimumOpcodeByteSize() const = 0;
	unsigned int GetPCCharWidth() const;
	unsigned int GetAddressBusCharWidth() const;
	unsigned int GetDataBusCharWidth() const;

	//Breakpoint functions
	void CheckExecution(unsigned int location) const;
	void CheckMemoryRead(unsigned int location, unsigned int data) const;
	void CheckMemoryWrite(unsigned int location, unsigned int data) const;
	void TriggerBreak() const;
	void TriggerBreakpoint(Breakpoint* breakpoint) const;
	static void BreakpointCallbackRaw(void* aparams);
	void BreakpointCallback(Breakpoint* breakpoint) const;
	void SetStepOver(bool state);
	void SetStepOut(bool state);

	//Stack functions
	void PushCallStack(unsigned int sourceAddress, unsigned int targetAddress, unsigned int returnAddress, const std::wstring& entry, bool fixedDisassembly = false);
	void PopCallStack(unsigned int returnAddress);
	void ClearCallStack();

	//Trace functions
	void RecordTrace(unsigned int pc);
	void ClearTraceLog();
	std::set<unsigned int> GetTraceCoverageLog() const;
	void ClearTraceCoverageLog();
	unsigned int GetTraceCoverageLogSize() const;
	bool GetTraceEnabled() const;
	void SetTraceEnabled(bool astate);
	unsigned int GetTraceLength() const;
	void SetTraceLength(unsigned int length);
	bool GetTraceCoverageEnabled() const;
	void SetTraceCoverageEnabled(bool astate);
	unsigned int GetTraceCoverageStart() const;
	void SetTraceCoverageStart(unsigned int astart);
	unsigned int GetTraceCoverageEnd() const;
	void SetTraceCoverageEnd(unsigned int aend);

	//Disassembly functions
	void EnableActiveDisassembly(unsigned int startLocation, unsigned int endLocation);
	void DisableActiveDisassembly();
	void AddDisassemblyAddressInfoCode(unsigned int location, unsigned int dataSize);
	void AddDisassemblyAddressInfoData(unsigned int location, unsigned int dataSize);
	void AddDisassemblyAddressInfoOffset(unsigned int location, unsigned int dataSize, bool offsetToCode, bool relativeOffset, unsigned int relativeOffsetBaseAddress);

	//Savestate functions
	virtual void LoadState(IHeirarchicalStorageNode& node);
	virtual void GetState(IHeirarchicalStorageNode& node) const;
	void LoadCallStack(IHeirarchicalStorageNode& node);
	void SaveCallStack(IHeirarchicalStorageNode& node) const;

	//Window functions
	virtual void AddDebugMenuItems(IMenuSegment& menuSegment, IViewModelLauncher& viewModelLauncher);
	virtual void RestoreViewModelState(const std::wstring& menuHandlerName, int viewModelID, IHeirarchicalStorageNode& node, int xpos, int ypos, int width, int height, IViewModelLauncher& viewModelLauncher);
	virtual void OpenViewModel(const std::wstring& menuHandlerName, int viewModelID, IViewModelLauncher& viewModelLauncher);

private:
	//Enumerations
	enum DisassemblyDataType;

	//Structures
	struct BreakpointCallbackParams;
	struct CallStackEntry;
	struct TraceLogEntry;
	struct DisassemblyAddressInfo;
	struct DisassemblyArrayInfo;

	//View and menu classes
	class DebugMenuHandler;
	class ControlViewModel;
	class BreakpointViewModel;
	class WatchpointViewModel;
	class CallStackViewModel;
	class TraceViewModel;
	class DisassemblyViewModel;
	class DisassemblyOldViewModel;
	class ControlView;
	class BreakpointView;
	class WatchpointView;
	class CallStackView;
	class TraceView;
	class DisassemblyView;
	class DisassemblyOldView;
	friend class ControlViewModel;
	friend class BreakpointViewModel;
	friend class WatchpointViewModel;
	friend class CallStackViewModel;
	friend class TraceViewModel;
	friend class DisassemblyViewModel;
	friend class DisassemblyOldViewModel;
	friend class ControlView;
	friend class BreakpointView;
	friend class WatchpointView;
	friend class CallStackView;
	friend class TraceView;
	friend class DisassemblyView;
	friend class DisassemblyOldView;

	//Typedefs
	typedef std::map<unsigned int, DisassemblyArrayInfo> DisassemblyArrayInfoMap;
	typedef std::pair<unsigned int, DisassemblyArrayInfo> DisassemblyArrayInfoMapEntry;

private:
	//Menu handling
	DebugMenuHandler* menuHandler;

	//Clock speed
	double clockSpeed;
	double bclockSpeed;
	bool clockSpeedOverridden;
	double reportedClockSpeed;

	//Breakpoints
	typedef std::list<Breakpoint*> BreakpointList;
	typedef std::list<Watchpoint*> WatchpointList;
	mutable BreakpointList breakpoints;
	mutable WatchpointList watchpoints;
	bool breakpointExists;
	bool watchpointExists;

	//Call stack
	typedef std::list<CallStackEntry> CallStack;
	mutable bool breakOnNextOpcode;
	bool bbreakOnNextOpcode;
	mutable CallStack callStack;
	CallStack bcallStack;
	bool stepOver;
	bool bstepOver;
	bool stepOut;
	bool bstepOut;
	int stackLevel;
	int bstackLevel;
	bool stackDisassemble;

	//Trace
	typedef std::list<TraceLogEntry> Trace;
	mutable Trace trace;
	Trace btrace;
	bool traceEnabled;
	bool traceDisassemble;
	unsigned int traceLength;
	bool traceCoverageEnabled;
	unsigned int traceCoverageStart;
	unsigned int traceCoverageEnd;
	std::set<unsigned int> traceCoverage;

	//Disassembly
	bool activeDisassembly;
	unsigned int activeDisassemblyStartLocation;
	unsigned int activeDisassemblyEndLocation;
	unsigned int disassemblyArrayNextFreeID;
	std::vector<std::list<DisassemblyAddressInfo*>> disassemblyAddressInfo;
	std::set<DisassemblyAddressInfo*> disassemblyAddressInfoSet;
	DisassemblyArrayInfoMap disassemblyArrayInfo;

	//Debug
	mutable boost::mutex debugMutex;
};

#include "Processor.inl"
#endif
