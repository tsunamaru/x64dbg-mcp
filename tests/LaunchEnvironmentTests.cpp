#include "business/LaunchEnvironment.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <type_traits>

namespace {
int checks = 0;
int failures = 0;
void Check(bool result, const char* message) {
    ++checks;
    if (!result) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class F> bool Throws(F action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
std::optional<std::wstring> Read(const std::wstring& name) {
    return MCP::LaunchEnvironmentDetail::Read(name, {});
}

void Parsing() {
    using json = nlohmann::json;
    const auto good = MCP::ParseEnvironmentChanges(json{{"PLAIN", "value"}, {"EMPTY", ""}, {"REMOVE", nullptr},
        {std::string("UNICODE_\xc3\xa9"), std::string("\xf0\x9f\x9a\x97=ok\n")}});
    Check(good.size() == 4, "parse strings, empty, deletion and Unicode");
    const auto find = [&](std::wstring_view name) -> const std::optional<std::wstring>& {
        for (const auto& item : good) if (item.first == name) return item.second;
        throw std::runtime_error("missing fixture");
    };
    Check(find(L"EMPTY") && find(L"EMPTY")->empty() && !find(L"REMOVE"), "empty and deletion remain distinct");
    Check(find(L"UNICODE_\u00e9") == std::optional<std::wstring>(L"\U0001f697=ok\n"), "UTF-8 decodes to exact UTF-16");
    Check(MCP::ParseEnvironmentChanges(json::object()).empty(), "empty object is valid");
    for (const auto& bad : {json(), json::array(), json{{"A", 1}}, json{{"A", true}}, json{{"A", json::array()}},
         json{{"", "x"}}, json{{"A=B", "x"}}, json{{std::string("A\0B", 3), "x"}}, json{{"A", std::string("x\0y", 3)}},
         json{{"Name", "x"}, {"NAME", "y"}}, json{{"A", std::string("\xc0\xaf", 2)}},
         json{{std::string("\xed\xa0\x80", 3), "x"}}}) {
        Check(Throws([&] { MCP::ParseEnvironmentChanges(bad); }), "invalid object/name/value refuses");
    }
    Check(Throws([] { MCP::ParseEnvironmentChanges(json{{"A", std::string(32767, 'x')}}); }), "oversized value refuses");
    Check(Throws([] { MCP::ParseEnvironmentChanges(json{{std::string(1025, 'n'), "x"}}); }), "oversized name refuses");
    json tooMany = json::object();
    for (size_t i = 0; i <= MCP::LaunchEnvironmentLimits::Variables; ++i) tooMany["N" + std::to_string(i)] = "v";
    Check(Throws([&] { MCP::ParseEnvironmentChanges(tooMany); }), "variable count is bounded");
    json tooLarge = json::object();
    for (int i = 0; i < 5; ++i) tooLarge["N" + std::to_string(i)] = std::string(32766, 'x');
    Check(Throws([&] { MCP::ParseEnvironmentChanges(tooLarge); }), "aggregate input size is bounded");
    for (const auto& bad : {json(), json::object(), json::array({""}), json::array({"A=B"}), json::array({1}),
         json::array({std::string("A\0B", 3)}), json::array({std::string("\xff", 1)}), json::array({"PRE", "pre"})}) {
        Check(Throws([&] { MCP::ParseEnvironmentChanges(json::object(), bad); }), "invalid clear prefixes refuse");
    }
    Check(Throws([] { MCP::ParseEnvironmentChanges(json::object(), json(std::vector<std::string>(17, "X"))); }), "prefix count is bounded");
}

struct Fake {
    std::map<std::wstring, std::wstring> values;
    std::vector<size_t> failReads, failWrites, lyingWrites;
    size_t reads = 0, writes = 0;
    bool At(const std::vector<size_t>& list, size_t call) { return std::find(list.begin(), list.end(), call) != list.end(); }
    auto Find(LPCWSTR name) {
        return std::find_if(values.begin(), values.end(), [&](const auto& item) {
            return MCP::LaunchEnvironmentDetail::SameName(item.first, name);
        });
    }
};
Fake* active = nullptr;
DWORD WINAPI FakeGet(LPCWSTR name, LPWSTR buffer, DWORD size) {
    auto& fake = *active;
    if (fake.At(fake.failReads, ++fake.reads)) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
    const auto found = fake.Find(name);
    if (found == fake.values.end()) { SetLastError(ERROR_ENVVAR_NOT_FOUND); return 0; }
    const auto length = static_cast<DWORD>(found->second.size());
    if (size <= length) return length + 1;
    std::copy(found->second.begin(), found->second.end(), buffer); buffer[length] = L'\0';
    return length;
}
BOOL WINAPI FakeSet(LPCWSTR name, LPCWSTR value) {
    auto& fake = *active;
    if (fake.At(fake.failWrites, ++fake.writes)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (fake.At(fake.lyingWrites, fake.writes)) return TRUE;
    const auto old = fake.Find(name);
    if (!value) { if (old != fake.values.end()) fake.values.erase(old); }
    else if (old != fake.values.end()) old->second = value;
    else fake.values.emplace(name, value);
    return TRUE;
}
MCP::EnvironmentApi FakeApi() { MCP::EnvironmentApi api; api.get = &FakeGet; api.set = &FakeSet; return api; }

void Failures() {
    const MCP::EnvironmentChanges changes{{L"A", L"new-a"}, {L"B", L"new-b"}};
    Fake readFailure; readFailure.values = {{L"A", L"old-a"}, {L"B", L"old-b"}}; readFailure.failReads = {2}; active = &readFailure;
    Check(Throws([&] { MCP::ScopedEnvironment scope(changes, FakeApi()); }) && readFailure.writes == 0,
          "all prior values are captured before the first write");
    Fake applyFailure; applyFailure.values = readFailure.values; applyFailure.failWrites = {2}; active = &applyFailure;
    {
        MCP::ScopedEnvironment scope(changes, FakeApi());
        Check(!scope.Applied() && scope.LastError() == ERROR_ACCESS_DENIED && applyFailure.values == readFailure.values,
              "partial application failure rolls back and reports failure");
        Check(scope.Restore(), "restoration after successful rollback is idempotent");
    }
    Fake delayed; delayed.values = readFailure.values; delayed.failWrites = {2,3,4}; active = &delayed;
    {
        MCP::ScopedEnvironment scope(changes, FakeApi());
        Check(!scope.Applied() && delayed.values[L"A"] == L"new-a", "failed rollback keeps a valid pending guard");
        Check(!scope.Restore(), "persistent restoration failure is explicit");
    }
    Check(delayed.values == readFailure.values, "destructor retries pending restoration");
    Fake partial; partial.values = readFailure.values; partial.failWrites = {3}; active = &partial;
    {
        MCP::ScopedEnvironment scope(changes, FakeApi());
        Check(scope.Applied(), "all updates applied before restoration test");
        Check(!scope.Restore() && partial.values[L"A"] == L"old-a" && partial.values[L"B"] == L"new-b",
              "restoration continues to other entries after one failure");
        Check(scope.Restore() && partial.values == readFailure.values && !scope.Applied(), "explicit retry restores remaining entry");
    }
    Fake lying; lying.values = {{L"A", L"old-a"}}; lying.lyingWrites = {2}; active = &lying;
    {
        MCP::ScopedEnvironment scope({{L"A", L"new-a"}}, FakeApi());
        Check(!scope.Restore() && scope.LastError() == ERROR_INVALID_DATA, "restoration is verified by read-back");
        Check(scope.Restore() && lying.values[L"A"] == L"old-a", "read-back mismatch retains its retry snapshot");
    }
    Fake validation; active = &validation;
    Check(Throws([&] { MCP::ScopedEnvironment scope({{L"A", L"x"},{L"a", L"y"}}, FakeApi()); }) && validation.reads == 0 && validation.writes == 0,
          "direct wide changes validate before any environment access");
    Check(Throws([&] { MCP::ScopedEnvironment scope({{std::wstring(1, wchar_t{0xd800}), L"x"}}, FakeApi()); }), "invalid UTF-16 direct changes refuse");
    active = nullptr;
}

void RealEnvironment() {
    const std::string asciiPrefix = "MCP_LAUNCH_ENV_TEST_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()) + "_";
    const std::wstring prefix(asciiPrefix.begin(), asciiPrefix.end());
    const auto a = prefix + L"A", empty = prefix + L"EMPTY", absent = prefix + L"ABSENT", unrelated = prefix + L"OTHER";
    struct Cleanup {
        std::vector<std::wstring> names;
        ~Cleanup() { for (const auto& name : names) SetEnvironmentVariableW(name.c_str(), nullptr); }
    } cleanup{{a,empty,absent,unrelated}};
    Check(SetEnvironmentVariableW(a.c_str(), L"original") && SetEnvironmentVariableW(empty.c_str(), L"") &&
          SetEnvironmentVariableW(unrelated.c_str(), L"untouched"), "create real process-local fixtures");
    Check(Read(empty) == std::optional<std::wstring>(L"") && !Read(absent), "real Windows empty and absent values differ");
    {
        MCP::ScopedEnvironment scope({{a,L"temporary"},{empty,std::nullopt},{absent,L""}});
        Check(scope.Applied() && Read(a) == std::optional<std::wstring>(L"temporary") && !Read(empty) &&
              Read(absent) == std::optional<std::wstring>(L""), "real set, delete and empty-set apply");
        Check(Read(unrelated) == std::optional<std::wstring>(L"untouched"), "unrelated process variable is untouched");
        Check(scope.Restore() && Read(a) == std::optional<std::wstring>(L"original") &&
              Read(empty) == std::optional<std::wstring>(L"") && !Read(absent), "explicit real restoration preserves presence and values");
        Check(scope.Restore(), "real repeated restoration succeeds");
    }
    try {
        MCP::ScopedEnvironment scope({{a,L"throwing"}});
        throw std::runtime_error("fixture");
    } catch (const std::runtime_error&) {}
    Check(Read(a) == std::optional<std::wstring>(L"original"), "real destructor restores during exception unwinding");

    const auto p1 = prefix + L"PREFIX_ONE", p2 = prefix + L"PREFIX_TWO";
    cleanup.names.push_back(p1); cleanup.names.push_back(p2);
    SetEnvironmentVariableW(p1.c_str(), L"stale-one"); SetEnvironmentVariableW(p2.c_str(), L"stale-two");
    auto lowerPrefix = asciiPrefix + "prefix_";
    const auto expanded = MCP::ParseEnvironmentChanges(nlohmann::json{{asciiPrefix + "PREFIX_ONE", "fresh"}},
        nlohmann::json::array({lowerPrefix}));
    Check(expanded.size() == 2, "prefix expansion includes matching current parent variables");
    {
        MCP::ScopedEnvironment scope(expanded);
        Check(scope.Applied() && Read(p1) == std::optional<std::wstring>(L"fresh") && !Read(p2) && Read(a) == std::optional<std::wstring>(L"original"),
              "explicit values override case-insensitive prefix removals without touching unrelated names");
        Check(scope.Restore() && Read(p1) == std::optional<std::wstring>(L"stale-one") && Read(p2) == std::optional<std::wstring>(L"stale-two"),
              "prefix-cleared values restore exactly");
    }
    const auto overlap = MCP::ParseEnvironmentChanges(nlohmann::json::object(), nlohmann::json::array({lowerPrefix,asciiPrefix + "PREFIX_O"}));
    Check(overlap.size() == 2, "overlapping prefixes do not duplicate a removal");
    MCP::ScopedEnvironment noChanges(MCP::ParseEnvironmentChanges(nlohmann::json::object()));
    Check(noChanges.Applied() && noChanges.Restore(), "explicit empty environment is a successful scope");
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<MCP::ScopedEnvironment>);
    try { Parsing(); Failures(); RealEnvironment(); }
    catch (const std::exception&) { Check(false, "unexpected test exception"); }
    std::cout << (failures ? "FAIL" : "PASS") << ": launch environment (" << checks << " checks)\n";
    return failures ? 1 : 0;
}
