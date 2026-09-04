#pragma once
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "../core/TargetValueValidator.h"

namespace MCP {

/**
 * @brief 寄存器信息
 */
struct RegisterInfo {
    std::string name;
    uint64_t value;
    size_t size;  // 字节数
};

/**
 * @brief SSE / AVX vector register contents.
 */
struct VectorRegisterInfo {
    std::string name;
    size_t size = 0;             // bytes: 16 for XMM, 32 for YMM
    std::vector<uint8_t> bytes;  // little-endian, as stored
};

/**
 * @brief 寄存器管理器
 * 封装 x64dbg 寄存器访问 API
 */
class RegisterManager {
public:
    /**
     * @brief 获取单例实例
     */
    static RegisterManager& Instance();
    
    /**
     * @brief 读取寄存器值
     * @param name 寄存器名称（不区分大小写）
     * @return 寄存器值
     */
    uint64_t GetRegister(const std::string& name);
    
    /**
     * @brief 设置寄存器值
     * @param name 寄存器名称
     * @param value 新值
     * @return 是否成功
     */
    bool SetRegister(const std::string& name, uint64_t value);
    
    /**
     * @brief 获取寄存器详细信息
     * @param name 寄存器名称
     * @return 寄存器信息
     */
    RegisterInfo GetRegisterInfo(const std::string& name);
    
    /**
     * @brief 列出所有寄存器及其值
     * @return 寄存器信息列表
     */
    std::vector<RegisterInfo> ListAllRegisters();
    
    /**
     * @brief 获取通用寄存器列表
     */
    std::vector<RegisterInfo> GetGeneralRegisters();
    
    /**
     * @brief 获取标志寄存器
     */
    RegisterInfo GetFlagsRegister();
    
    /**
     * @brief 验证寄存器名称是否有效
     * @param name 寄存器名称
     * @return 是否有效
     */
    bool IsValidRegister(const std::string& name) const;

    /**
     * @brief Is this the name of an SSE / AVX vector register?
     */
    bool IsVectorRegister(const std::string& name) const;

    /**
     * @brief Read one vector register (xmm0.., ymm0..).
     * @throws InvalidRegisterException on a bad name or index
     * @throws DebuggerNotPausedException when the debuggee is running
     */
    VectorRegisterInfo GetVectorRegister(const std::string& name);

    /**
     * @brief Every vector register name this build can read.
     */
    std::vector<std::string> VectorRegisterNames() const;

    /**
     * @brief How many XMM/YMM registers this architecture has (8 or 16).
     */
    static size_t VectorRegisterCount();
    
    /**
     * @brief 获取寄存器大小（字节数）
     * @param name 寄存器名称
     * @return 大小（字节）
     */
    size_t GetRegisterSize(const std::string& name) const;

    static constexpr bool FitsRegisterValue(uint64_t value, size_t sizeBytes) noexcept {
        return TargetValueValidator::FitsUnsignedValue(value, sizeBytes);
    }

private:
    RegisterManager();
    ~RegisterManager() = default;
    RegisterManager(const RegisterManager&) = delete;
    RegisterManager& operator=(const RegisterManager&) = delete;
    
    void InitializeRegisterMap();
    std::string NormalizeName(const std::string& name) const;

    static bool ParseVectorName(const std::string& name, char& kind,
                                size_t& index);

    uint64_t GetMxCsr();
    
    // 寄存器名称到大小的映射
    std::unordered_map<std::string, size_t> m_registerSizes;
};

} // namespace MCP
