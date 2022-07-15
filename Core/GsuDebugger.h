#pragma once
#include "stdafx.h"
#include "DebugTypes.h"
#include "IDebugger.h"

class Disassembler;
class Debugger;
class CodeDataLogger;
class Gsu;
class MemoryAccessCounter;
class MemoryManager;
class BreakpointManager;
class EmuSettings;

class GsuDebugger final : public IDebugger
{
	Debugger* _debugger;
	Disassembler* _disassembler;
	CodeDataLogger* _codeDataLogger;
	MemoryAccessCounter* _memoryAccessCounter;
	MemoryManager* _memoryManager;
	Gsu* _gsu;
	EmuSettings* _settings;

	unique_ptr<BreakpointManager> _breakpointManager;
	unique_ptr<StepRequest> _step;

	uint8_t _prevOpCode = 0xFF;
	uint32_t _prevProgramCounter = 0;

public:
	GsuDebugger(Debugger* debugger);

	void Reset();

	void ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType type);
	void ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType type);
	void Run();
	void Step(int32_t stepCount, StepType type);

	BreakpointManager* GetBreakpointManager();
};
