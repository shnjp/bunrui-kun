#pragma once
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace bunrui {
namespace fs = std::filesystem;
struct App {
    fs::path path;
    std::vector<std::string> args;
    std::string uri;
};
struct Rule {
    std::string pattern;
    std::regex regex;
    std::string app;
};
struct Config {
    std::map<std::string, App> apps;
    std::vector<Rule> rules;
};
struct Launch {
    std::string app;
    fs::path path;
    std::vector<std::string> args;
    std::string uri;
    size_t rule;
};
Config parse_config(const std::string& text, const fs::path& base);
Config load_config(const fs::path& path);
fs::path find_config(const fs::path& portable, const fs::path& user);
Launch route(const Config& config, const std::string& url);
std::wstring widen(const std::string& text);
std::string narrow(const std::wstring& text);
std::wstring quote_arg(const std::wstring& arg);
}
