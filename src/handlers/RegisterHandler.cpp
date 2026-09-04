#include "RegisterHandler.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include "../business/RegisterManager.h"
#include "../core/MethodDispatcher.h"
#include "../core/RequestValidator.h"
#include "../core/PermissionChecker.h"
#include "../core/Exceptions.h"
#include "../core/Logger.h"
#include "../utils/StringUtils.h"

namespace MCP {

void RegisterHandler::RegisterMethods() {
    auto& dispatcher = MethodDispatcher::Instance();
    
    dispatcher.RegisterMethod("register.get", Get);
    dispatcher.RegisterMethod("register.set", Set);
    dispatcher.RegisterMethod("register.list", List);
    dispatcher.RegisterMethod("register.get_batch", GetBatch);
    dispatcher.RegisterMethod("register.get_vector", GetVector);

    Logger::Info("Registered register.* methods");
}

namespace {

/**
 * @brief Format a float as a string, because JSON has no NaN.
 */
std::string FormatFloatLane(double value, int digits) {
    if (std::isnan(value))
        return std::signbit(value) ? "-nan" : "nan";
    if (std::isinf(value))
        return std::signbit(value) ? "-inf" : "inf";
    std::ostringstream out;
    out << std::setprecision(digits) << value;
    return out.str();
}

std::string ToHex(const std::vector<uint8_t>& bytes) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

json DecodeVector(const VectorRegisterInfo& info) {
    json f32 = json::array();
    json f64 = json::array();
    json u32 = json::array();

    for (size_t off = 0; off + 4 <= info.size; off += 4) {
        float f = 0.0f;
        uint32_t u = 0;
        std::memcpy(&f, info.bytes.data() + off, 4);
        std::memcpy(&u, info.bytes.data() + off, 4);
        f32.push_back(FormatFloatLane(static_cast<double>(f),
                                      std::numeric_limits<float>::max_digits10));
        std::ostringstream hex;
        hex << "0x" << std::hex << std::uppercase << std::setw(8)
            << std::setfill('0') << u;
        u32.push_back(hex.str());
    }
    for (size_t off = 0; off + 8 <= info.size; off += 8) {
        double d = 0.0;
        std::memcpy(&d, info.bytes.data() + off, 8);
        f64.push_back(FormatFloatLane(d,
                                      std::numeric_limits<double>::max_digits10));
    }

    return {
        {"name", info.name},
        {"size", info.size},
        {"hex", ToHex(info.bytes)},
        {"f32", f32},
        {"f64", f64},
        {"u32", u32},
        {"success", true}
    };
}

}  // namespace

json RegisterHandler::GetVector(const json& params) {
    auto& manager = RegisterManager::Instance();

    // Ambiguous input is an error, not a silent preference.
    if (params.contains("name") && params.contains("names")) {
        throw InvalidParamsException(
            "Pass 'name' or 'names', not both");
    }

    std::vector<json> requested;
    if (params.contains("names")) {
        RequestValidator::RequireArray(params, "names");
        for (const auto& nameJson : params["names"])
            requested.push_back(nameJson);
    } else {
        RequestValidator::RequireString(params, "name");
        requested.push_back(params["name"]);
    }

    if (requested.empty()) {
        throw InvalidParamsException(
            "No register names given; nothing to read");
    }

    json results = json::array();
    for (const auto& entry : requested) {
        if (!entry.is_string()) {
            results.push_back({
                {"name", entry.dump()},
                {"success", false},
                {"error", "Register name must be a string"}
            });
            continue;
        }
        const std::string name = entry.get<std::string>();
        try {
            results.push_back(DecodeVector(manager.GetVectorRegister(name)));
        } catch (const std::exception& ex) {
            results.push_back({
                {"name", name},
                {"success", false},
                {"error", ex.what()}
            });
        }
    }

    return {
        {"registers", results},
        {"count", results.size()},
        {"requested", requested.size()},
        {"available", manager.VectorRegisterNames()},
    };
}

json RegisterHandler::Get(const json& params) {
    RequestValidator::RequireString(params, "name");
    
    std::string name = params["name"].get<std::string>();
    
    auto& manager = RegisterManager::Instance();
    auto info = manager.GetRegisterInfo(name);
    
    return {
        {"name", info.name},
        {"value", StringUtils::FormatAddress(info.value)},
        {"value_decimal", info.value},
        {"size", info.size}
    };
}

json RegisterHandler::Set(const json& params) {
    // 检查权限
    if (!PermissionChecker::Instance().IsRegisterWriteAllowed()) {
        throw PermissionDeniedException("Register write not allowed");
    }
    
    RequestValidator::RequireString(params, "name");
    RequestValidator::RequireField(params, "value");
    
    std::string name = params["name"].get<std::string>();
    
    uint64_t value;
    if (params["value"].is_string()) {
        try {
            value = StringUtils::ParseAddress(params["value"].get<std::string>());
        } catch (const std::exception&) {
            throw InvalidParamsException("Invalid register value");
        }
    } else if (params["value"].is_number_unsigned()) {
        value = params["value"].get<uint64_t>();
    } else if (params["value"].is_number_integer()) {
        const int64_t signedValue = params["value"].get<int64_t>();
        if (signedValue < 0) {
            throw InvalidParamsException("Register value cannot be negative");
        }
        value = static_cast<uint64_t>(signedValue);
    } else {
        throw InvalidParamsException("Value must be a string or integer");
    }
    
    auto& manager = RegisterManager::Instance();
    bool success = manager.SetRegister(name, value);
    
    return {
        {"success", success},
        {"name", name},
        {"value", StringUtils::FormatAddress(value)}
    };
}

json RegisterHandler::List(const json& params) {
    auto& manager = RegisterManager::Instance();
    
    // 检查是否只列出通用寄存器
    bool generalOnly = RequestValidator::GetBoolean(params, "general_only", false);
    
    std::vector<RegisterInfo> registers;
    if (generalOnly) {
        registers = manager.GetGeneralRegisters();
    } else {
        registers = manager.ListAllRegisters();
    }
    
    json registerArray = json::array();
    for (const auto& reg : registers) {
        registerArray.push_back({
            {"name", reg.name},
            {"value", StringUtils::FormatAddress(reg.value)},
            {"value_decimal", reg.value},
            {"size", reg.size}
        });
    }
    
    return {
        {"registers", registerArray},
        {"count", registers.size()}
    };
}

json RegisterHandler::GetBatch(const json& params) {
    RequestValidator::RequireArray(params, "names");
    
    auto& manager = RegisterManager::Instance();
    json results = json::array();
    
    for (const auto& nameJson : params["names"]) {
        if (!nameJson.is_string()) {
            continue;
        }
        
        std::string name = nameJson.get<std::string>();
        
        try {
            auto info = manager.GetRegisterInfo(name);
            results.push_back({
                {"name", info.name},
                {"value", StringUtils::FormatAddress(info.value)},
                {"value_decimal", info.value},
                {"size", info.size},
                {"success", true}
            });
        } catch (const std::exception& ex) {
            results.push_back({
                {"name", name},
                {"success", false},
                {"error", ex.what()}
            });
        }
    }
    
    return {
        {"registers", results},
        {"count", results.size()}
    };
}

} // namespace MCP
