#pragma once

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

namespace MCP {
namespace ScriptCommandPolicy {

inline const std::set<std::string>& AsyncOnlyVerbs() {
    static const std::set<std::string> verbs = {
        // Debuggee lifetime.
        "initdebug", "init", "initdbg",
        "stopdebug", "stop", "dbgstop", "close",
        "attachdebugger", "attach",
        "detachdebugger", "detach",
        "restart", "reload",

        // Run / resume.
        "run", "go", "r", "g", "resume",
        "erun", "egun", "er", "eg", "ego",
        "serun", "sego",
        "pause",

        // Stepping.
        "stepinto", "sti", "singlestep", "sstep", "sst", "t",
        "estepinto", "esti", "et",
        "sestepinto", "sesti", "esinglestep", "esstep", "esst",
        "stepover", "step", "sto", "st", "p",
        "estepover", "estep", "esto", "est", "ep",
        "sestepover", "sestep", "sesto", "sest",
        "stepout", "rtr",
        "estepout", "ertr",
        "stepuser", "stepuserinto",
        "stepsystem", "stepsysteminto",
        "skip",

        // Tracing / run-until commands.
        "traceintoconditional", "ticnd", "traceinto",
        "traceoverconditional", "tocnd", "traceover",
        "traceintobeyondtracecoverage", "traceintobeyondtracerecord", "tibt",
        "traceoverbeyondtracecoverage", "traceoverbeyondtracerecord", "tobt",
        "traceintointotracecoverage", "traceintointotracerecord", "tiit",
        "traceoverintotracecoverage", "traceoverintotracerecord", "toit",
        "runtoparty", "rtp",
        "runtousercode", "rtu",
    };
    return verbs;
}

inline std::string FirstVerb(const std::string& command) {
    size_t begin = 0;
    while (begin < command.size() &&
           std::isspace(static_cast<unsigned char>(command[begin]))) {
        ++begin;
    }
    if (begin == command.size()) {
        return "";
    }

    size_t end = begin;
    while (end < command.size() &&
           !std::isspace(static_cast<unsigned char>(command[end]))) {
        ++end;
    }

    std::string verb = command.substr(begin, end - begin);
    std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return verb;
}

inline bool SegmentRequiresQueue(const std::string& segment) {
    const std::string verb = FirstVerb(segment);
    if (verb.empty()) {
        return false;
    }

    // x64dbg expands a leading '$' with stringformatinline before parsing it.
    // The resulting verb cannot be known here, so direct execution is unsafe.
    if (verb[0] == '$') {
        return true;
    }

    return AsyncOnlyVerbs().count(verb) != 0;
}

inline bool RequiresQueuedExecution(const std::string& command) {
    bool inQuote = false;
    bool inEscape = false;
    size_t start = 0;

    // Mirror x64dbg's quote-aware semicolon splitting. Newlines are accepted as
    // additional separators because MCP clients commonly send command blocks.
    for (size_t i = 0; i < command.size(); ++i) {
        const char ch = command[i];
        switch (ch) {
        case '"':
            if (!inEscape) {
                inQuote = !inQuote;
            }
            inEscape = false;
            break;
        case '\\':
            inEscape = !inEscape;
            break;
        default:
            inEscape = false;
            break;
        }

        const bool separator = !inQuote &&
            (ch == ';' || ch == '\n' || ch == '\r');
        if (!separator) {
            continue;
        }

        if (SegmentRequiresQueue(command.substr(start, i - start))) {
            return true;
        }
        start = i + 1;
    }

    return SegmentRequiresQueue(command.substr(start));
}

} // namespace ScriptCommandPolicy
} // namespace MCP
