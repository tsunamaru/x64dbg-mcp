#pragma once

#include <windows.h>
#include <nlohmann/json.hpp>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MCP {

using EnvironmentChanges = std::vector<std::pair<std::wstring, std::optional<std::wstring>>>;

struct LaunchEnvironmentLimits {
    static constexpr size_t Variables = 128;
    static constexpr size_t Prefixes = 16;
    static constexpr size_t NameUnits = 1024;
    static constexpr size_t ValueUnits = 32766;
    static constexpr size_t TotalUnits = 131072;
    static constexpr size_t ParentBlockUnits = 1048576;
};

// Small Win32 seams allow failure tests without changing the GUI's environment.
struct EnvironmentApi {
    decltype(&::GetEnvironmentVariableW) get = &::GetEnvironmentVariableW;
    decltype(&::SetEnvironmentVariableW) set = &::SetEnvironmentVariableW;
    decltype(&::GetEnvironmentStringsW) strings = &::GetEnvironmentStringsW;
    decltype(&::FreeEnvironmentStringsW) freeStrings = &::FreeEnvironmentStringsW;
};

namespace LaunchEnvironmentDetail {

inline bool ValidWide(std::wstring_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const auto ch = static_cast<unsigned>(text[i]);
        if (ch == 0) return false;
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (ch >= 0xdc00 && ch <= 0xdfff) return false;
    }
    return true;
}

inline void ValidateName(std::wstring_view name) {
    if (name.empty() || name.size() > LaunchEnvironmentLimits::NameUnits ||
        !ValidWide(name) || name.find(L'=') != std::wstring_view::npos) {
        throw std::invalid_argument("Invalid launch environment name or prefix");
    }
}

inline std::wstring Utf8(const std::string& text, size_t limit) {
    if (text.size() > 4 * limit || text.find('\0') != std::string::npos)
        throw std::invalid_argument("Invalid or oversized launch environment text");
    if (text.empty()) return {};
    // Microsoft MultiByteToWideChar: MB_ERR_INVALID_CHARS refuses malformed UTF-8 instead of replacing it.
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0 || static_cast<size_t>(length) > limit)
        throw std::invalid_argument("Invalid UTF-8 or oversized launch environment text");
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) != length)
        throw std::invalid_argument("Launch environment UTF-8 conversion failed");
    return result;
}

inline bool SameName(std::wstring_view a, std::wstring_view b) {
    const int result = CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
    if (result == 0) throw std::runtime_error("Launch environment name comparison failed");
    return result == CSTR_EQUAL;
}

inline void ValidateChanges(const EnvironmentChanges& changes) {
    if (changes.size() > LaunchEnvironmentLimits::Variables) throw std::invalid_argument("Too many launch environment variables");
    size_t total = 0;
    for (size_t i = 0; i < changes.size(); ++i) {
        const auto& item = changes[i];
        ValidateName(item.first);
        if (item.second && (item.second->size() > LaunchEnvironmentLimits::ValueUnits || !ValidWide(*item.second)))
            throw std::invalid_argument("Invalid or oversized launch environment value");
        total += item.first.size() + (item.second ? item.second->size() : 0) + 2;
        if (total > LaunchEnvironmentLimits::TotalUnits) throw std::invalid_argument("Launch environment exceeds total size limit");
        for (size_t old = 0; old < i; ++old) {
            if (SameName(item.first, changes[old].first)) throw std::invalid_argument("Duplicate launch environment name");
        }
    }
}

inline std::optional<std::wstring> Read(const std::wstring& name, const EnvironmentApi& api) {
    std::array<wchar_t, 32768> buffer{};
    // GetEnvironmentVariableW returns zero for absence and for an empty value; preserve the distinction using LastError.
    SetLastError(ERROR_SUCCESS);
    const DWORD length = api.get(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
        if (error == ERROR_SUCCESS) return std::wstring{};
        throw std::runtime_error("Launch environment snapshot failed");
    }
    if (length >= buffer.size()) throw std::runtime_error("Launch environment snapshot exceeds size limit");
    return std::wstring(buffer.data(), length);
}

} // namespace LaunchEnvironmentDetail

// Prefix expansion reads the current parent environment. Hold the init mutex while parsing and until restoration completes.
// Explicit entries override prefix removals. Empty strings set empty values; null removes the name.
inline EnvironmentChanges ParseEnvironmentChanges(const nlohmann::json& object,
                                                  const nlohmann::json& clearPrefixes = nlohmann::json::array(),
                                                  EnvironmentApi api = {}) {
    using namespace LaunchEnvironmentDetail;
    if (!object.is_object() || object.size() > LaunchEnvironmentLimits::Variables)
        throw std::invalid_argument("Launch environment must be a bounded object");
    EnvironmentChanges explicitChanges;
    explicitChanges.reserve(object.size());
    for (auto it = object.begin(); it != object.end(); ++it) {
        auto name = Utf8(it.key(), LaunchEnvironmentLimits::NameUnits);
        std::optional<std::wstring> value;
        if (it.value().is_string()) value = Utf8(it.value().get_ref<const std::string&>(), LaunchEnvironmentLimits::ValueUnits);
        else if (!it.value().is_null()) throw std::invalid_argument("Launch environment values must be strings or null");
        explicitChanges.emplace_back(std::move(name), std::move(value));
    }
    ValidateChanges(explicitChanges);
    if (!clearPrefixes.is_array() || clearPrefixes.size() > LaunchEnvironmentLimits::Prefixes)
        throw std::invalid_argument("Environment clear prefixes must be a bounded array");
    std::vector<std::wstring> prefixes;
    for (const auto& item : clearPrefixes) {
        if (!item.is_string()) throw std::invalid_argument("Environment clear prefixes must be strings");
        auto prefix = Utf8(item.get_ref<const std::string&>(), LaunchEnvironmentLimits::NameUnits);
        ValidateName(prefix);
        for (const auto& old : prefixes) if (SameName(prefix, old)) throw std::invalid_argument("Duplicate environment clear prefix");
        prefixes.push_back(std::move(prefix));
    }
    if (prefixes.empty()) return explicitChanges;
    if (!api.strings || !api.freeStrings) throw std::invalid_argument("Invalid environment API");
    auto* block = api.strings();
    if (!block) throw std::runtime_error("Parent environment enumeration failed");
    struct Release {
        LPWCH block;
        decltype(&::FreeEnvironmentStringsW) free;
        ~Release() { free(block); }
    } release{block, api.freeStrings};
    EnvironmentChanges changes;
    size_t offset = 0;
    while (offset < LaunchEnvironmentLimits::ParentBlockUnits && block[offset] != L'\0') {
        size_t end = offset;
        while (end < LaunchEnvironmentLimits::ParentBlockUnits && block[end] != L'\0') ++end;
        if (end == LaunchEnvironmentLimits::ParentBlockUnits) throw std::runtime_error("Parent environment exceeds size limit");
        const std::wstring_view entry(block + offset, end - offset);
        offset = end + 1;
        if (entry.front() == L'=') continue; // Preserve the unrelated drive-current-directory entries.
        const auto separator = entry.find(L'=');
        if (separator == std::wstring_view::npos) throw std::runtime_error("Invalid parent environment entry");
        const auto name = entry.substr(0, separator);
        bool matched = false;
        for (const auto& prefix : prefixes) {
            if (name.size() >= prefix.size() && SameName(name.substr(0, prefix.size()), prefix)) { matched = true; break; }
        }
        if (matched) {
            changes.emplace_back(std::wstring(name), std::nullopt);
            ValidateChanges(changes);
        }
    }
    if (offset == LaunchEnvironmentLimits::ParentBlockUnits) throw std::runtime_error("Parent environment exceeds size limit");
    for (auto& item : explicitChanges) {
        bool replaced = false;
        for (auto& removal : changes) {
            if (SameName(item.first, removal.first)) { removal.second = std::move(item.second); replaced = true; break; }
        }
        if (!replaced) changes.push_back(std::move(item));
    }
    ValidateChanges(changes);
    return changes;
}

// PROCESS-GLOBAL scope, not isolation from other GUI/plugin activity. The caller coordinates exclusive launching/environment changes.
// Snapshot all affected values before the first mutation. Restore only these names, leaving unrelated variables alone.
// Use around synchronous DbgCmdExecDirect(init); queued init followed by immediate restoration is unsafe.
class ScopedEnvironment {
public:
    explicit ScopedEnvironment(const EnvironmentChanges& changes, EnvironmentApi api = {}) : api_(api) {
        LaunchEnvironmentDetail::ValidateChanges(changes);
        if (!api_.get || !api_.set) throw std::invalid_argument("Invalid environment API");
        prior_.reserve(changes.size());
        size_t total = 0;
        for (const auto& item : changes) {
            auto value = LaunchEnvironmentDetail::Read(item.first, api_);
            total += item.first.size() + (value ? value->size() : 0) + 2;
            if (total > LaunchEnvironmentLimits::TotalUnits) throw std::runtime_error("Prior environment exceeds size limit");
            prior_.push_back({item.first, std::move(value), false});
        }
        for (size_t i = 0; i < changes.size(); ++i) {
            if (changes[i].second == prior_[i].value) continue;
            prior_[i].pending = true; // Include a failed write in rollback, without assuming it left the value unchanged.
            if (!Write(prior_[i].name, changes[i].second)) { Restore(); return; }
        }
        applied_ = true;
    }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    ~ScopedEnvironment() { Restore(); }

    bool Applied() const noexcept { return applied_; }
    DWORD LastError() const noexcept { return lastError_; }

    // Call explicitly and check the result. Failures retain pending snapshots for a later retry/destructor fallback.
    bool Restore() noexcept {
        applied_ = false;
        bool restored = true;
        for (auto it = prior_.rbegin(); it != prior_.rend(); ++it) {
            if (!it->pending) continue;
            try {
                if (LaunchEnvironmentDetail::Read(it->name, api_) != it->value) {
                    if (!Write(it->name, it->value)) { restored = false; continue; }
                    if (LaunchEnvironmentDetail::Read(it->name, api_) != it->value) {
                        lastError_ = ERROR_INVALID_DATA; restored = false; continue;
                    }
                }
                it->pending = false;
            } catch (...) { lastError_ = ERROR_GEN_FAILURE; restored = false; }
        }
        return restored;
    }

private:
    struct Snapshot { std::wstring name; std::optional<std::wstring> value; bool pending; };
    bool Write(const std::wstring& name, const std::optional<std::wstring>& value) noexcept {
        try {
            SetLastError(ERROR_SUCCESS);
            // Microsoft SetEnvironmentVariableW: nullptr deletes; a non-null empty string remains a distinct request.
            if (api_.set(name.c_str(), value ? value->c_str() : nullptr)) return true;
            const DWORD error = GetLastError();
            lastError_ = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
        } catch (...) { lastError_ = ERROR_GEN_FAILURE; }
        return false;
    }
    EnvironmentApi api_;
    std::vector<Snapshot> prior_;
    DWORD lastError_ = ERROR_SUCCESS;
    bool applied_ = false;
};

} // namespace MCP
