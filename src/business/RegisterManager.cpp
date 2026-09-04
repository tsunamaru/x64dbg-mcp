#include "RegisterManager.h"
#include <cstring>
#include <string>
#include "DebugController.h"
#include "../core/Logger.h"
#include "../core/Exceptions.h"
#include "../core/TargetValueValidator.h"
#include "../utils/StringUtils.h"
#include "../core/X64DBGBridge.h"

namespace MCP {

RegisterManager& RegisterManager::Instance() {
    static RegisterManager instance;
    return instance;
}

RegisterManager::RegisterManager() {
    InitializeRegisterMap();
}

void RegisterManager::InitializeRegisterMap() {
#ifdef XDBG_ARCH_X64
    // 64位通用寄存器
    m_registerSizes["rax"] = 8;
    m_registerSizes["rbx"] = 8;
    m_registerSizes["rcx"] = 8;
    m_registerSizes["rdx"] = 8;
    m_registerSizes["rsi"] = 8;
    m_registerSizes["rdi"] = 8;
    m_registerSizes["rbp"] = 8;
    m_registerSizes["rsp"] = 8;
    m_registerSizes["r8"] = 8;
    m_registerSizes["r9"] = 8;
    m_registerSizes["r10"] = 8;
    m_registerSizes["r11"] = 8;
    m_registerSizes["r12"] = 8;
    m_registerSizes["r13"] = 8;
    m_registerSizes["r14"] = 8;
    m_registerSizes["r15"] = 8;
    
    // 特殊寄存器
    m_registerSizes["rip"] = 8;
    m_registerSizes["rflags"] = 8;
    
    // 别名
    m_registerSizes["cip"] = 8;  // 当前指令指针
    m_registerSizes["csp"] = 8;  // 当前栈指针
#endif
    
    // 32位寄存器（x86 和 x64 都支持）
    m_registerSizes["eax"] = 4;
    m_registerSizes["ebx"] = 4;
    m_registerSizes["ecx"] = 4;
    m_registerSizes["edx"] = 4;
    m_registerSizes["esi"] = 4;
    m_registerSizes["edi"] = 4;
    m_registerSizes["ebp"] = 4;
    m_registerSizes["esp"] = 4;
    m_registerSizes["eip"] = 4;
    m_registerSizes["eflags"] = 4;
    
#ifdef XDBG_ARCH_X86
    // x86 别名（指向 32 位寄存器）
    m_registerSizes["cip"] = 4;  // 当前指令指针
    m_registerSizes["csp"] = 4;  // 当前栈指针
#endif
    m_registerSizes["mxcsr"] = 4;

    // 段寄存器（x86 和 x64 通用）
    m_registerSizes["cs"] = 2;
    m_registerSizes["ds"] = 2;
    m_registerSizes["es"] = 2;
    m_registerSizes["fs"] = 2;
    m_registerSizes["gs"] = 2;
    m_registerSizes["ss"] = 2;
}

uint64_t RegisterManager::GetRegister(const std::string& name) {
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }
    
    std::string normalizedName = NormalizeName(name);
    
    if (IsVectorRegister(normalizedName)) {
        throw InvalidRegisterException(
            normalizedName + " is a vector register; use register_get_vector");
    }

    if (!IsValidRegister(normalizedName)) {
        throw InvalidRegisterException("Invalid register name: " + name);
    }

    if (normalizedName == "mxcsr") {
        return GetMxCsr();
    }

    // 使用 x64dbg API 读取寄存器
    uint64_t value = DbgValFromString(normalizedName.c_str());
    
    Logger::Trace("Read register {}: 0x{:X}", normalizedName, value);
    return value;
}

bool RegisterManager::SetRegister(const std::string& name, uint64_t value) {
    std::string normalizedName = NormalizeName(name);

    if (!IsValidRegister(normalizedName)) {
        throw InvalidRegisterException("Invalid register name: " + name);
    }

    const size_t registerSize = GetRegisterSize(normalizedName);
    if (!FitsRegisterValue(value, registerSize)) {
        throw InvalidRegisterException(
            "Value exceeds " + std::to_string(registerSize * 8) +
            "-bit register width: " + normalizedName);
    }

    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }

    // x64dbg addresses FPU scalar values through an underscore-prefixed name.
    const std::string sdkName = normalizedName == "mxcsr"
        ? "_MxCsr"
        : normalizedName;

    // 使用 x64dbg API 设置寄存器
    bool success = DbgValToString(sdkName.c_str(), static_cast<duint>(value));
    
    if (success) {
        Logger::Debug("Set register {} = 0x{:X}", normalizedName, value);
    } else {
        Logger::Error("Failed to set register {}", normalizedName);
    }
    
    return success;
}

RegisterInfo RegisterManager::GetRegisterInfo(const std::string& name) {
    std::string normalizedName = NormalizeName(name);
    
    RegisterInfo info;
    info.name = normalizedName;
    info.value = GetRegister(normalizedName);
    info.size = GetRegisterSize(normalizedName);
    
    return info;
}

std::vector<RegisterInfo> RegisterManager::ListAllRegisters() {
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }
    
    std::vector<RegisterInfo> registers;
    
    // 读取所有通用寄存器
#ifdef XDBG_SDK_AVAILABLE
    // 使用实际SDK - 优先使用REGDUMP结构(更简单)
    REGDUMP regDump = {};
    if (DbgGetRegDumpEx((REGDUMP_AVX512*)&regDump, sizeof(REGDUMP))) {
        auto& ctx = regDump.regcontext;
#ifdef XDBG_ARCH_X64
        // x64 架构寄存器
        registers.push_back({"rax", ctx.cax, 8});
        registers.push_back({"rbx", ctx.cbx, 8});
        registers.push_back({"rcx", ctx.ccx, 8});
        registers.push_back({"rdx", ctx.cdx, 8});
        registers.push_back({"rsi", ctx.csi, 8});
        registers.push_back({"rdi", ctx.cdi, 8});
        registers.push_back({"rbp", ctx.cbp, 8});
        registers.push_back({"rsp", ctx.csp, 8});
        registers.push_back({"r8", ctx.r8, 8});
        registers.push_back({"r9", ctx.r9, 8});
        registers.push_back({"r10", ctx.r10, 8});
        registers.push_back({"r11", ctx.r11, 8});
        registers.push_back({"r12", ctx.r12, 8});
        registers.push_back({"r13", ctx.r13, 8});
        registers.push_back({"r14", ctx.r14, 8});
        registers.push_back({"r15", ctx.r15, 8});
        registers.push_back({"rip", ctx.cip, 8});
        registers.push_back({"rflags", ctx.eflags, 8});
#else
        // x86 架构寄存器
        registers.push_back({"eax", static_cast<uint64_t>(ctx.cax & 0xFFFFFFFF), 4});
        registers.push_back({"ebx", static_cast<uint64_t>(ctx.cbx & 0xFFFFFFFF), 4});
        registers.push_back({"ecx", static_cast<uint64_t>(ctx.ccx & 0xFFFFFFFF), 4});
        registers.push_back({"edx", static_cast<uint64_t>(ctx.cdx & 0xFFFFFFFF), 4});
        registers.push_back({"esi", static_cast<uint64_t>(ctx.csi & 0xFFFFFFFF), 4});
        registers.push_back({"edi", static_cast<uint64_t>(ctx.cdi & 0xFFFFFFFF), 4});
        registers.push_back({"ebp", static_cast<uint64_t>(ctx.cbp & 0xFFFFFFFF), 4});
        registers.push_back({"esp", static_cast<uint64_t>(ctx.csp & 0xFFFFFFFF), 4});
        registers.push_back({"eip", static_cast<uint64_t>(ctx.cip & 0xFFFFFFFF), 4});
        registers.push_back({"eflags", static_cast<uint64_t>(ctx.eflags & 0xFFFFFFFF), 4});
#endif
#else
    // Mock版本 - 根据架构条件编译
    REGDUMP regDump = {};
    if (DbgGetRegDumpEx(&regDump, sizeof(regDump))) {
#ifdef XDBG_ARCH_X64
        registers.push_back({"rax", regDump.rax, 8});
        registers.push_back({"rbx", regDump.rbx, 8});
        registers.push_back({"rcx", regDump.rcx, 8});
        registers.push_back({"rdx", regDump.rdx, 8});
        registers.push_back({"rsi", regDump.rsi, 8});
        registers.push_back({"rdi", regDump.rdi, 8});
        registers.push_back({"rbp", regDump.rbp, 8});
        registers.push_back({"rsp", regDump.rsp, 8});
        registers.push_back({"r8", regDump.r8, 8});
        registers.push_back({"r9", regDump.r9, 8});
        registers.push_back({"r10", regDump.r10, 8});
        registers.push_back({"r11", regDump.r11, 8});
        registers.push_back({"r12", regDump.r12, 8});
        registers.push_back({"r13", regDump.r13, 8});
        registers.push_back({"r14", regDump.r14, 8});
        registers.push_back({"r15", regDump.r15, 8});
        registers.push_back({"rip", regDump.rip, 8});
        registers.push_back({"rflags", regDump.eflags, 8});
#else
        registers.push_back({"eax", static_cast<uint64_t>(regDump.eax), 4});
        registers.push_back({"ebx", static_cast<uint64_t>(regDump.ebx), 4});
        registers.push_back({"ecx", static_cast<uint64_t>(regDump.ecx), 4});
        registers.push_back({"edx", static_cast<uint64_t>(regDump.edx), 4});
        registers.push_back({"esi", static_cast<uint64_t>(regDump.esi), 4});
        registers.push_back({"edi", static_cast<uint64_t>(regDump.edi), 4});
        registers.push_back({"ebp", static_cast<uint64_t>(regDump.ebp), 4});
        registers.push_back({"esp", static_cast<uint64_t>(regDump.esp), 4});
        registers.push_back({"eip", static_cast<uint64_t>(regDump.eip), 4});
        registers.push_back({"eflags", regDump.eflags, 4});
#endif
#endif
    }
    
    return registers;
}

std::vector<RegisterInfo> RegisterManager::GetGeneralRegisters() {
    std::vector<RegisterInfo> allRegs = ListAllRegisters();
    std::vector<RegisterInfo> generalRegs;
    
    // 过滤出通用寄存器（排除 IP、flags 和段寄存器）
    for (const auto& reg : allRegs) {
        if (reg.name != "rip" && reg.name != "eip" && 
            reg.name != "eflags" && reg.name != "rflags" &&
            reg.name.find("cs") == std::string::npos &&
            reg.name.find("ds") == std::string::npos &&
            reg.name.find("es") == std::string::npos &&
            reg.name.find("fs") == std::string::npos &&
            reg.name.find("gs") == std::string::npos &&
            reg.name.find("ss") == std::string::npos) {
            generalRegs.push_back(reg);
        }
    }
    
    return generalRegs;
}

RegisterInfo RegisterManager::GetFlagsRegister() {
    return GetRegisterInfo("eflags");
}

bool RegisterManager::IsValidRegister(const std::string& name) const {
    std::string normalizedName = NormalizeName(name);
    return m_registerSizes.find(normalizedName) != m_registerSizes.end();
}

size_t RegisterManager::GetRegisterSize(const std::string& name) const {
    std::string normalizedName = NormalizeName(name);
    
    auto it = m_registerSizes.find(normalizedName);
    if (it != m_registerSizes.end()) {
        return it->second;
    }
    
    return 0;
}

std::string RegisterManager::NormalizeName(const std::string& name) const {
    return StringUtils::ToLower(StringUtils::Trim(name));
}

size_t RegisterManager::VectorRegisterCount() {
#ifdef XDBG_ARCH_X64
    return 16;
#else
    return 8;
#endif
}

bool RegisterManager::ParseVectorName(const std::string& name, char& kind,
                                      size_t& index) {
    if (name.size() < 4)
        return false;
    if (name.compare(1, 2, "mm") != 0)
        return false;
    if (name[0] != 'x' && name[0] != 'y')
        return false;

    const std::string digits = name.substr(3);
    if (digits.empty() || digits.size() > 2)
        return false;
    if (digits.size() > 1 && digits[0] == '0')
        return false;
    for (char c : digits) {
        if (c < '0' || c > '9')
            return false;
    }

    kind = name[0];
    index = static_cast<size_t>(std::stoul(digits));
    return true;
}

bool RegisterManager::IsVectorRegister(const std::string& name) const {
    char kind = 0;
    size_t index = 0;
    if (!ParseVectorName(NormalizeName(name), kind, index))
        return false;
    return index < VectorRegisterCount();
}

std::vector<std::string> RegisterManager::VectorRegisterNames() const {
    std::vector<std::string> names;
    const size_t count = VectorRegisterCount();
    names.reserve(count * 2);
    for (size_t i = 0; i < count; ++i)
        names.push_back("xmm" + std::to_string(i));
    for (size_t i = 0; i < count; ++i)
        names.push_back("ymm" + std::to_string(i));
    return names;
}

uint64_t RegisterManager::GetMxCsr() {
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }
#ifdef XDBG_SDK_AVAILABLE
    REGDUMP regDump = {};
    if (!DbgGetRegDumpEx(reinterpret_cast<REGDUMP_AVX512*>(&regDump),
                         sizeof(REGDUMP))) {
        throw InvalidRegisterException("register dump unavailable");
    }
    return static_cast<uint64_t>(regDump.regcontext.MxCsr);
#else
    throw InvalidRegisterException(
        "mxcsr needs a build with the x64dbg SDK");
#endif
}

VectorRegisterInfo RegisterManager::GetVectorRegister(const std::string& name) {
    if (!DebugController::Instance().IsPaused()) {
        throw DebuggerNotPausedException();
    }

    const std::string normalizedName = NormalizeName(name);
    char kind = 0;
    size_t index = 0;
    if (!ParseVectorName(normalizedName, kind, index)) {
        throw InvalidRegisterException(
            "Not a vector register name: " + name);
    }
    if (index >= VectorRegisterCount()) {
        throw InvalidRegisterException(
            "No such vector register: " + normalizedName + "; this build has " +
            std::to_string(VectorRegisterCount()) + " of each");
    }

#ifdef XDBG_SDK_AVAILABLE
    REGDUMP regDump = {};
    if (!DbgGetRegDumpEx(reinterpret_cast<REGDUMP_AVX512*>(&regDump),
                         sizeof(REGDUMP))) {
        throw InvalidRegisterException("register dump unavailable");
    }

    VectorRegisterInfo info;
    info.name = normalizedName;
    const auto& ctx = regDump.regcontext;
    if (kind == 'x') {
        info.size = sizeof(ctx.XmmRegisters[index]);
        info.bytes.resize(info.size);
        std::memcpy(info.bytes.data(), &ctx.XmmRegisters[index], info.size);
    } else {
        info.size = sizeof(ctx.YmmRegisters[index]);
        info.bytes.resize(info.size);
        std::memcpy(info.bytes.data(), &ctx.YmmRegisters[index], info.size);
    }

    Logger::Trace("Read vector register {} ({} bytes)", info.name, info.size);
    return info;
#else
    throw InvalidRegisterException(
        "Vector registers need a build with the x64dbg SDK");
#endif
}

} // namespace MCP
