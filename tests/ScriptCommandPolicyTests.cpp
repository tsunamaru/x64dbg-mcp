#include "core/ScriptCommandPolicy.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool ExpectQueued(const std::string& command) {
    if (MCP::ScriptCommandPolicy::RequiresQueuedExecution(command)) {
        return true;
    }
    std::cerr << "Expected queued execution: " << command << '\n';
    return false;
}

bool ExpectDirect(const std::string& command) {
    if (!MCP::ScriptCommandPolicy::RequiresQueuedExecution(command)) {
        return true;
    }
    std::cerr << "Expected direct execution: " << command << '\n';
    return false;
}

} // namespace

int main() {
    const std::vector<std::string> executionControlAliases = {
        "InitDebug target.exe", "init target.exe", "initdbg target.exe",
        "StopDebug", "stop", "dbgstop", "AttachDebugger 123", "attach 123",
        "DetachDebugger", "detach", "run", "go", "r", "g",
        "erun", "egun", "er", "eg", "serun", "sego", "pause",
        "StepInto", "sti", "SingleStep", "sstep", "sst",
        "eStepInto", "esti", "seStepInto", "sesti", "eSingleStep", "esstep", "esst",
        "StepOver", "step", "sto", "st", "eStepOver", "estep", "esto", "est",
        "seStepOver", "sestep", "sesto", "sest", "StepOut", "rtr", "eStepOut", "ertr",
        "StepUser", "StepUserInto", "StepSystem", "StepSystemInto",
        "TraceIntoConditional 1", "ticnd 1", "TraceOverConditional 1", "tocnd 1",
        "TraceIntoBeyondTraceCoverage", "TraceIntoBeyondTraceRecord", "tibt",
        "TraceOverBeyondTraceCoverage", "TraceOverBeyondTraceRecord", "tobt",
        "TraceIntoIntoTraceCoverage", "TraceIntoIntoTraceRecord", "tiit",
        "TraceOverIntoTraceCoverage", "TraceOverIntoTraceRecord", "toit",
        "RunToParty 0", "RunToUserCode", "rtu",
    };

    for (const auto& command : executionControlAliases) {
        if (!ExpectQueued(command)) {
            return 1;
        }
    }

    if (!ExpectQueued("bp 401000; run") ||
        !ExpectQueued("bp 401000\nstep") ||
        !ExpectQueued("  $ run") ||
        !ExpectQueued("bp 401000; $ {p:cip}")) {
        return 1;
    }

    const std::vector<std::string> directCommands = {
        "bp 401000",
        "mov eax, 1",
        "log \"before;run;after\"",
        "log \"before\nrun\nafter\"",
        "log \"before; step; after\"; bp 401000",
        "serun_label",
        "",
        "   ",
    };
    for (const auto& command : directCommands) {
        if (!ExpectDirect(command)) {
            return 1;
        }
    }

    return 0;
}
