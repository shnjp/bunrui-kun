#include "router.hpp"
#include "toml.hpp"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cwctype>

namespace bunrui {
namespace {
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error(text); }
void keys(const toml::table& table, std::initializer_list<std::string_view> allowed,
          const std::string& where) {
    for (const auto& [key, node] : table) {
        (void)node;
        if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end())
            fail(where + ": unknown key " + std::string(key.str()));
    }
}
std::string string_value(const toml::table& table, std::string_view key) {
    auto value = table[key].value<std::string>();
    if (!value || value->empty() || value->find('\0') != std::string::npos)
        fail("Expected a nonempty string: " + std::string(key));
    // Validate UTF-8 before anything is passed to Windows.
    widen(*value);
    return *value;
}
// Template expansion is one pass: braces in a substituted URL stay literal.
std::string expand(const std::string& value, const std::string& url,
                   const std::smatch* match, size_t captures) {
    std::string result;
    for (size_t i = 0; i < value.size();) {
        if (value[i] != '{') { result += value[i++]; continue; }
        auto end = value.find('}', i);
        if (end == std::string::npos) fail("Unclosed placeholder in: " + value);
        auto key = value.substr(i + 1, end - i - 1);
        if (key == "url") result += url;
        else {
            if (key.empty() || key.size() > 3 ||
                key.find_first_not_of("0123456789") != std::string::npos)
                fail("Unknown placeholder: {" + key + "}");
            auto index = static_cast<size_t>(std::stoul(key));
            if (index == 0 || index > captures) fail("Capture out of range: {" + key + "}");
            if (match) result += (*match)[index].str();
        }
        i = end + 1;
    }
    return result;
}
void validate_uri(const std::string& uri) {
    const auto end = uri.find(':');
    if (end == std::string::npos || end == 0) fail("App uri must have a custom scheme");
    auto scheme = uri.substr(0, end);
    if (!std::regex_match(scheme, std::regex("[A-Za-z][A-Za-z0-9+.-]*")))
        fail("Invalid app URI scheme");
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (scheme == "http" || scheme == "https" || scheme == "file")
        fail("App uri must use a custom scheme (http/https would recurse)");
    if (uri.find_first_of("\r\n\t ") != std::string::npos)
        fail("App uri must not contain whitespace; encode URL components first");
}
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                 static_cast<int>(text.size()), nullptr, 0);
    if (!size) fail("Invalid UTF-8");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}
std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) fail("Invalid UTF-16");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring quote_arg(const std::wstring& arg) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}

Config parse_config(const std::string& text, const fs::path& base) {
    const auto table = toml::parse(text);
    keys(table, {"apps", "rules"}, "config");
    auto apps = table["apps"].as_table();
    auto rules = table["rules"].as_array();
    if (!apps || apps->empty()) fail("apps must be a nonempty table");
    if (!rules || rules->empty()) fail("rules must be a nonempty array of tables");
    Config config;
    for (const auto& [name, node] : *apps) {
        auto app_table = node.as_table();
        if (!app_table) fail("Each app must be a table");
        keys(*app_table, {"path", "args", "uri"}, "apps." + std::string(name.str()));
        App app;
        bool has_path = app_table->contains("path"), has_uri = app_table->contains("uri");
        if (has_path == has_uri) fail("Each app requires exactly one of path or uri");
        if (has_uri) {
            if (app_table->contains("args")) fail("uri apps cannot have args");
            app.uri = string_value(*app_table, "uri");
            validate_uri(app.uri);
        } else {
            app.path = fs::path(widen(string_value(*app_table, "path")));
            if (app.path.is_relative()) app.path = fs::absolute(base / app.path);
            app.path = app.path.lexically_normal();
            auto extension = app.path.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (extension != L".exe")
                fail("App path must name an .exe file");
            auto args = (*app_table)["args"].as_array();
            if (!args) fail("path apps require an args array");
            for (const auto& arg : *args) {
                auto value = arg.value<std::string>();
                if (!value || value->find('\0') != std::string::npos)
                    fail("args must contain strings without NUL bytes");
                widen(*value);
                app.args.push_back(*value);
            }
        }
        // Even unused apps must have syntactically valid templates. Rule-specific
        // capture counts are checked below when validating references.
        for (const auto& arg : app.args) expand(arg, "", nullptr, 999);
        if (!app.uri.empty()) expand(app.uri, "", nullptr, 999);
        config.apps.emplace(std::string(name.str()), std::move(app));
    }
    for (const auto& node : *rules) {
        auto rule_table = node.as_table();
        if (!rule_table) fail("Each rule must be a table");
        keys(*rule_table, {"pattern", "app"}, "rules");
        auto pattern = string_value(*rule_table, "pattern");
        auto app = string_value(*rule_table, "app");
        if (!config.apps.count(app)) fail("Undefined app: " + app);
        std::regex regex;
        try { regex = std::regex(pattern, std::regex::ECMAScript); }
        catch (const std::regex_error& e) { fail("Invalid regex " + pattern + ": " + e.what()); }
        const auto& target = config.apps.at(app);
        for (const auto& arg : target.args) expand(arg, "", nullptr, regex.mark_count());
        if (!target.uri.empty()) expand(target.uri, "", nullptr, regex.mark_count());
        config.rules.push_back({pattern, std::move(regex), app});
    }
    return config;
}
Config load_config(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) fail("Cannot read config: " + narrow(path.wstring()));
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) fail("Failed reading config: " + narrow(path.wstring()));
    try { return parse_config(contents.str(), path.parent_path()); }
    catch (const std::exception& e) { fail(narrow(path.wstring()) + "\n" + e.what()); }
}
fs::path find_config(const fs::path& portable, const fs::path& user) {
    // status errors propagate; an unreadable higher-priority config must not be skipped.
    for (const auto& path : {portable, user}) {
        if (fs::exists(path)) {
            if (!fs::is_regular_file(path)) fail("Config is not a regular file: " + narrow(path.wstring()));
            return path;
        }
    }
    fail("Config not found. Create one of:\n" + narrow(portable.wstring()) + "\n" + narrow(user.wstring()));
}
Launch route(const Config& config, const std::string& url) {
    for (size_t i = 0; i < config.rules.size(); ++i) {
        const auto& rule = config.rules[i];
        std::smatch match;
        if (!std::regex_search(url, match, rule.regex)) continue;
        const auto& app = config.apps.at(rule.app);
        Launch launch{rule.app, app.path, {}, {}, i + 1};
        for (const auto& arg : app.args)
            launch.args.push_back(expand(arg, url, &match, rule.regex.mark_count()));
        if (!app.uri.empty()) {
            launch.uri = expand(app.uri, url, &match, rule.regex.mark_count());
            validate_uri(launch.uri);
        }
        return launch;
    }
    fail("No rule matched this URL");
}
}
