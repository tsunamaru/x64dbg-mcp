#include "ScriptHandler.h"
#include "../core/Logger.h"
#include "../core/PermissionChecker.h"
#include "../core/ScriptCommandPolicy.h"
#include <sstream>
#include <string>

#ifdef XDBG_SDK_AVAILABLE
#include "_plugins.h"
#include "bridgemain.h"
#endif

std::string ScriptHandler::lastResult = "";
bool ScriptHandler::lastSuccess = false;
std::mutex ScriptHandler::resultMutex;

json ScriptHandler::execute(const json& params) {
    try {
        if (!MCP::PermissionChecker::Instance().IsScriptExecutionAllowed()) {
            return {
                {"success", false},
                {"error", "Script execution is disabled by permissions"}
            };
        }

        if (!params.contains("command") || !params["command"].is_string()) {
            return {
                {"success", false},
                {"error", "Missing or invalid 'command' parameter"}
            };
        }

        std::string command = params["command"];
        MCP::Logger::Debug("Executing script command: {}", command);

        bool wantDirect = false;
        if (params.contains("direct") && params["direct"].is_boolean()) {
            wantDirect = params["direct"];
        }
        const bool guarded = wantDirect &&
            MCP::ScriptCommandPolicy::RequiresQueuedExecution(command);
        const bool asyncOnly = !wantDirect || guarded;
        bool success = asyncOnly ? DbgCmdExec(command.c_str())
                                 : DbgCmdExecDirect(command.c_str());

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            lastSuccess = success;
            if (asyncOnly) {
                lastResult = success
                    ? "Command queued (execution control; result unknown)"
                    : "Command could not be queued";
            } else {
                lastResult = success
                    ? "Command executed successfully"
                    : "Command rejected or failed - see the debugger log";
            }
        }

        json result = {
            {"success", success},
            {"command", command},
            {"queued_only", asyncOnly}
        };

        if (guarded) {
            result["note"] = "Refused direct for an execution-control "
                             "command, queued instead";
        } else if (asyncOnly) {
            result["note"] = "Queued";
        }

        if (!success) {
            result["error"] = asyncOnly
                ? "Command could not be queued"
                : "Command rejected or failed - see the debugger log";
        }

        if (params.contains("capture_output") && params["capture_output"].is_boolean() && params["capture_output"]) {
            std::lock_guard<std::mutex> lock(resultMutex);
            result["output"] = lastResult;
        }

        return result;

    } catch (const std::exception& e) {
        MCP::Logger::Error("Script execution error: {}", e.what());
        return {
            {"success", false},
            {"error", e.what()}
        };
    }
}

json ScriptHandler::executeBatch(const json& params) {
    try {
        if (!MCP::PermissionChecker::Instance().IsScriptExecutionAllowed()) {
            return {
                {"success", false},
                {"error", "Script execution is disabled by permissions"}
            };
        }

        if (!params.contains("commands") || !params["commands"].is_array()) {
            return {
                {"success", false},
                {"error", "Missing or invalid 'commands' parameter (must be array)"}
            };
        }

        json::array_t commands = params["commands"];
        json results = json::array();
        bool allSuccess = true;
        int successCount = 0;
        int failCount = 0;

        bool stopOnError = false;
        if (params.contains("stop_on_error") && params["stop_on_error"].is_boolean()) {
            stopOnError = params["stop_on_error"];
        }

        bool batchDirect = false;
        if (params.contains("direct") && params["direct"].is_boolean()) {
            batchDirect = params["direct"];
        }

        if (batchDirect) {
            for (size_t i = 0; i + 1 < commands.size(); ++i) {
                if (!commands[i].is_string()) {
                    continue;
                }
                const std::string probe = commands[i];
                if (MCP::ScriptCommandPolicy::RequiresQueuedExecution(probe)) {
                    return {
                        {"success", false},
                        {"error", "Execution-control command '" + probe +
                                  "' is not last in a direct batch. It would "
                                  "be queued while later commands ran "
                                  "directly, thus batch could not stay "
                                  "sequential. Nothing was executed. Put it "
                                  "last, split the batch, or drop 'direct'."},
                        {"refused_index", static_cast<int>(i)},
                        {"executed", 0}
                    };
                }
            }
        }

        for (const auto& cmd : commands) {
            if (!cmd.is_string()) {
                results.push_back({
                    {"success", false},
                    {"command", ""},
                    {"error", "Invalid command (not a string)"}
                });
                allSuccess = false;
                failCount++;
                if (stopOnError) {
                    break;
                }
                continue;
            }

            std::string command = cmd;
            const bool guarded = batchDirect &&
                MCP::ScriptCommandPolicy::RequiresQueuedExecution(command);
            const bool asyncOnly = !batchDirect || guarded;
            bool success = asyncOnly ? DbgCmdExec(command.c_str())
                                     : DbgCmdExecDirect(command.c_str());

            json cmdResult = {
                {"success", success},
                {"command", command},
                {"queued_only", asyncOnly}
            };

            if (guarded) {
                cmdResult["note"] = "Queued";
            }

            if (!success) {
                cmdResult["error"] = "Command execution failed";
                allSuccess = false;
                failCount++;
                if (stopOnError) {
                    results.push_back(cmdResult);
                    break;
                }
            } else {
                successCount++;
            }

            results.push_back(cmdResult);
        }

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            lastSuccess = allSuccess;
            if (allSuccess) {
                lastResult = "All " + std::to_string(successCount) + " commands executed successfully";
            } else {
                lastResult = std::to_string(successCount) + " succeeded, " + std::to_string(failCount) + " failed";
            }
        }

        return {
            {"success", allSuccess},
            {"total", commands.size()},
            {"succeeded", successCount},
            {"failed", failCount},
            {"results", results}
        };

    } catch (const std::exception& e) {
        MCP::Logger::Error("Batch script execution error: {}", e.what());
        return {
            {"success", false},
            {"error", e.what()}
        };
    }
}

json ScriptHandler::getLastResult(const json& params) {
    (void)params;

    bool successSnapshot = false;
    std::string resultSnapshot;
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        successSnapshot = lastSuccess;
        resultSnapshot = lastResult;
    }

    json result = {
        {"success", successSnapshot},
        {"result", resultSnapshot}
    };

    if (!successSnapshot) {
        result["error"] = resultSnapshot.empty() ? "No script executed yet or last execution failed" : resultSnapshot;
    }

    return result;
}