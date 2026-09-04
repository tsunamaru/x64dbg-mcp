#pragma once
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace MCP {

/**
 * @brief 寄存器方法处理器
 * 实现 register.* 系列 JSON-RPC 方法
 */
class RegisterHandler {
public:
    /**
     * @brief 注册所有寄存器方法到分发器
     */
    static void RegisterMethods();
    
    /**
     * @brief register.get - 读取寄存器值
     */
    static json Get(const json& params);
    
    /**
     * @brief register.set - 设置寄存器值
     */
    static json Set(const json& params);
    
    /**
     * @brief register.list - 列出所有寄存器
     */
    static json List(const json& params);
    
    /**
     * @brief register.get_batch - 批量读取寄存器
     */
    static json GetBatch(const json& params);

    /**
     * @brief register.get_vector - read SSE / AVX registers (xmm, ymm)
     */
    static json GetVector(const json& params);
};

} // namespace MCP
