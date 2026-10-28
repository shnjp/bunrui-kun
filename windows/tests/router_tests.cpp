#include "router.hpp"
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace bunrui;
namespace {
int checks = 0;
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F fn, const std::string& message) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
const std::string base = R"toml(
[apps.chrome]
path = 'browser.exe'
args = ['--profile-directory=Profile 1', '{url}']
[apps.notion]
uri = 'notion://{1}'
[apps.slack]
uri = 'slack://channel?team={1}&id={2}'
[[rules]]
pattern = '^https://app\.slack\.com/client/(T[A-Z0-9]+)/(C[A-Z0-9]+)/?(?:[?#].*)?$'
app = 'slack'
[[rules]]
pattern = '^https://((?:[A-Za-z0-9-]+\.)?notion\.so/[^?#]*[0-9a-fA-F]{32}(?:[/?#].*)?)$'
app = 'notion'
[[rules]]
pattern = '.*'
app = 'chrome'
)toml";
std::string replaced(std::string text, const std::string& before, const std::string& after) {
    const auto pos = text.find(before);
    if (pos == std::string::npos) throw std::runtime_error("Test replacement missing");
    text.replace(pos, before.size(), after);
    return text;
}
}
int main() {
    try {
        auto config = parse_config(base, fs::current_path());
        auto slack = route(config, "https://app.slack.com/client/T123/C456");
        check(slack.app == "slack" && slack.rule == 1, "First matching rule wins");
        check(slack.uri == "slack://channel?team=T123&id=C456", "Slack capture expansion");
        const std::string page = "https://www.notion.so/My-page-0123456789abcdef0123456789abcdef?v=abc#block";
        auto notion = route(config, page);
        check(notion.uri == "notion://" + page.substr(8), "Notion preserves query and fragment");
        auto browser = route(config, "https://example.com/a?x=1&y={url}#anchor");
        check(browser.args[1] == "https://example.com/a?x=1&y={url}#anchor", "Single-pass URL interpolation");
        check(browser.path == fs::current_path() / "browser.exe", "Relative executable path uses config location");
        check(route(config, "https://notion.so.evil.example/0123456789abcdef0123456789abcdef").app == "chrome",
              "Lookalike domains must not match");
        check(route(config, "https://www.notion.so/login").app == "chrome", "Notion login stays in browser");
        check(route(config, "https://app.slack.com/client/T123/C456/12345").app == "chrome",
              "Unsupported Slack message links stay in browser");
        rejects([&] { parse_config(replaced(base, "app = 'chrome'", "app = 'missing'"), fs::current_path()); }, "Undefined app");
        rejects([&] { parse_config(replaced(base, "pattern = '.*'", "pattern = '['"), fs::current_path()); }, "Invalid regex");
        rejects([&] { parse_config(replaced(base, "{2}", "{3}"), fs::current_path()); }, "Invalid capture reference");
        rejects([&] { parse_config(replaced(base, "{url}", "{typo}"), fs::current_path()); }, "Unknown placeholder");
        rejects([&] { parse_config(replaced(base, "slack://", "https://"), fs::current_path()); }, "Recursive URI");
        rejects([&] { parse_config(replaced(base, "args = [", "argz = ["), fs::current_path()); }, "Unknown key");
        rejects([&] { parse_config(replaced(base, "path = 'browser.exe'", "path = 'browser.exe'\nuri = 'slack://open'"), fs::current_path()); }, "Ambiguous app");
        rejects([&] { parse_config("[apps.x]\npath = 'x.exe'\nargs = []\nrules = []", fs::current_path()); }, "Missing rules");
        rejects([&] { parse_config("apps = {}\nrules = []", fs::current_path()); }, "Empty config");
        rejects([&] { parse_config("[apps", fs::current_path()); }, "Malformed TOML");
        auto no_fallback = config;
        no_fallback.rules.pop_back();
        rejects([&] { route(no_fallback, "https://example.com/"); }, "Unmatched URL");
        const auto unicode = widen("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        check(narrow(unicode) == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "Unicode round trip");
        rejects([&] { widen(std::string("\xff", 1)); }, "Invalid UTF-8 rejected");
        // Verify actual Windows command-line parsing, including empty args, quotes and trailing slashes.
        const std::vector<std::wstring> arguments = {L"", L"simple", L"two words", L"a\"b",
            L"C:\\directory with spaces\\", L"\\\\\"", unicode,
            L"https://example.com/?a=1&b=2", L"$(echo nope)", L"--flag"};
        std::wstring line = quote_arg(L"browser.exe");
        for (const auto& arg : arguments) line += L" " + quote_arg(arg);
        int count = 0;
        auto parsed = CommandLineToArgvW(line.c_str(), &count);
        check(parsed && count == static_cast<int>(arguments.size()) + 1, "Quoted argc");
        for (size_t i = 0; i < arguments.size(); ++i)
            check(parsed[i + 1] == arguments[i], "Quoted argument round trip " + std::to_string(i));
        LocalFree(parsed);

        auto dir = fs::current_path() / ("router-test-" + std::to_string(GetCurrentProcessId()));
        fs::create_directory(dir);
        auto portable = dir / "portable.toml", user = dir / "user.toml";
        rejects([&] { find_config(portable, user); }, "Missing configs");
        { std::ofstream file(user); file << base; }
        check(find_config(portable, user) == user, "User config fallback");
        { std::ofstream file(portable); file << "[invalid"; }
        check(find_config(portable, user) == portable, "Portable config priority");
        rejects([&] { load_config(find_config(portable, user)); }, "Broken portable config never falls through");
        fs::remove(portable);
        fs::create_directory(portable);
        rejects([&] { find_config(portable, user); }, "Config directory is an error");
        fs::remove(portable);
        fs::remove(user);
        fs::remove(dir);
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
