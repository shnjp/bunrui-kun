#include "router.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace bunrui;
namespace {
constexpr auto progid = L"BunruiKun.Url";
constexpr auto client = L"Software\\Clients\\StartMenuInternet\\BunruiKun";
constexpr auto capabilities = L"Software\\Clients\\StartMenuInternet\\BunruiKun\\Capabilities";
bool interactive_error = true;

std::string winerror(const std::string& context, DWORD code = GetLastError()) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::string result = context + " (" + std::to_string(code) + ")";
    if (message) { result += ": " + narrow(message); LocalFree(message); }
    return result;
}
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
fs::path executable() {
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) fail(winerror("GetModuleFileName"));
    buffer.resize(length);
    return buffer;
}
fs::path user_config() {
    PWSTR folder = nullptr;
    HRESULT status = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder);
    if (FAILED(status)) fail("Cannot locate LocalAppData");
    fs::path result = fs::path(folder) / L"bunrui-kun" / L"config.toml";
    CoTaskMemFree(folder);
    return result;
}
void output(const std::string& text, bool error = false) {
    HANDLE handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0, written = 0;
    auto line = text + "\n";
    if (GetConsoleMode(handle, &mode)) {
        auto wide = widen(line);
        WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
    } else if (handle && handle != INVALID_HANDLE_VALUE) {
        WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
}
void validate_url(const std::wstring& url) {
    if (url.size() > 16384 || url.find_first_of(L"\r\n\t \"") != std::wstring::npos)
        fail("URL contains whitespace/quotes or is too long; percent-encode it first");
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        parts.dwHostNameLength == 0)
        fail("Expected an absolute http:// or https:// URL");
}
void set_value(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY opened = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0,
                                    KEY_SET_VALUE, nullptr, &opened, nullptr);
    if (status != ERROR_SUCCESS) fail(winerror("Create registry key", status));
    status = RegSetValueExW(opened, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(opened);
    if (status != ERROR_SUCCESS) fail(winerror("Write registry value", status));
}
void register_app(const fs::path& exe) {
    const auto command = quote_arg(exe.wstring()) + L" \"%1\"";
    const auto icon = quote_arg(exe.wstring()) + L",0";
    const std::wstring classes = std::wstring(L"Software\\Classes\\") + progid;
    set_value(classes, nullptr, L"Bunrui-kun URL");
    set_value(classes, L"URL Protocol", L"");
    set_value(classes + L"\\DefaultIcon", nullptr, icon);
    set_value(classes + L"\\shell\\open\\command", nullptr, command);
    set_value(client, nullptr, L"Bunrui-kun");
    set_value(std::wstring(client) + L"\\DefaultIcon", nullptr, icon);
    set_value(std::wstring(client) + L"\\shell\\open\\command", nullptr,
              quote_arg(exe.wstring()) + L" --settings");
    set_value(capabilities, L"ApplicationName", L"Bunrui-kun");
    set_value(capabilities, L"ApplicationDescription", L"Route URLs using TOML rules");
    set_value(capabilities, L"ApplicationIcon", icon);
    set_value(std::wstring(capabilities) + L"\\URLAssociations", L"http", progid);
    set_value(std::wstring(capabilities) + L"\\URLAssociations", L"https", progid);
    // Publish the candidate only after its other registration keys are written.
    set_value(L"Software\\RegisteredApplications", L"BunruiKun", capabilities);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
void unregister_app() {
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications",
                                  0, KEY_SET_VALUE, &key);
    if (status == ERROR_SUCCESS) {
        status = RegDeleteValueW(key, L"BunruiKun");
        RegCloseKey(key);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
            fail(winerror("Remove registered application", status));
    } else if (status != ERROR_FILE_NOT_FOUND) fail(winerror("Open registry", status));
    for (const auto& path : {std::wstring(client), std::wstring(L"Software\\Classes\\") + progid}) {
        status = RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
            fail(winerror("Remove registry key", status));
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
void shell_open(const std::wstring& uri) {
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = uri.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) fail(winerror("Open URI"));
}
void launch_app(const Launch& launch, const fs::path& self) {
    if (!launch.uri.empty()) { shell_open(widen(launch.uri)); return; }
    if (!fs::is_regular_file(launch.path))
        fail("App executable not found: " + narrow(launch.path.wstring()));
    if (fs::equivalent(launch.path, self)) fail("App points to bunrui-kun itself");
    std::wstring command = quote_arg(launch.path.wstring());
    for (const auto& arg : launch.args) command += L" " + quote_arg(widen(arg));
    if (command.size() >= 32767) fail("App command line is too long");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launch.path.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, launch.path.parent_path().c_str(), &startup, &process))
        fail(winerror("Launch app"));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
constexpr auto help =
    "bunrui-kun <http(s)-URL>                  Route and open a URL\n"
    "bunrui-kun --dry-run <URL>                Show route without launching\n"
    "bunrui-kun --check                        Validate configuration\n"
    "bunrui-kun --config <file> [--check | --dry-run <URL> | <URL>]\n"
    "bunrui-kun --register                     Register as a browser candidate\n"
    "bunrui-kun --settings                     Open Windows Default Apps\n"
    "bunrui-kun --unregister                   Remove browser registration\n"
    "bunrui-kun --help\n"
    "Only --register/--unregister modify the registry. Select defaults in Windows Settings.";
int run(const std::vector<std::wstring>& args) {
    const auto self = executable();
    fs::path override_config;
    std::wstring action, url;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == L"--config") {
            interactive_error = false;
            if (++i == args.size() || !override_config.empty()) fail("Expected one --config path");
            override_config = fs::absolute(args[i]);
        } else if (arg.rfind(L"--", 0) == 0) {
            interactive_error = false;
            if (!action.empty()) fail("Specify one action");
            if (arg != L"--check" && arg != L"--dry-run" && arg != L"--register" &&
                arg != L"--unregister" && arg != L"--settings" && arg != L"--help")
                fail("Unknown option: " + narrow(arg));
            action = arg;
        } else {
            if (!url.empty()) fail("Expected a single URL");
            url = arg;
        }
    }
    if (action.empty() && url.empty()) action = L"--help";
    if (!url.empty() && !action.empty() && action != L"--dry-run") fail("Unexpected URL for this action");
    if (!override_config.empty() && action != L"--check" && action != L"--dry-run" && !action.empty())
        fail("--config only applies to routing and configuration checks");
    if (action == L"--help") { output(help); return 0; }
    if (action == L"--settings") { shell_open(L"ms-settings:defaultapps"); return 0; }
    if (action == L"--register") {
        register_app(self);
        output("Registered. Run --settings, then select Bunrui-kun for HTTP and HTTPS.\n"
               "Keep this executable at its current location.");
        return 0;
    }
    if (action == L"--unregister") {
        unregister_app();
        output("Registration removed. Choose another default browser in Windows Settings.");
        return 0;
    }
    const auto path = override_config.empty()
        ? find_config(self.parent_path() / L"config.toml", user_config()) : override_config;
    const auto config = load_config(path);
    if (action == L"--check") {
        output("Config OK: " + narrow(path.wstring()) + "\nApps: " + std::to_string(config.apps.size()) +
               ", rules: " + std::to_string(config.rules.size()) +
               "\nSyntax checked; app installation and URI handlers are not checked.");
        return 0;
    }
    if (url.empty()) fail("Expected a URL");
    validate_url(url);
    const auto launch = route(config, narrow(url));
    if (action == L"--dry-run") {
        output("Config: " + narrow(path.wstring()) + "\nRule: " + std::to_string(launch.rule) +
               "\nApp: " + launch.app);
        if (!launch.uri.empty()) output("URI: " + launch.uri);
        else {
            output("Path: " + narrow(launch.path.wstring()));
            for (size_t i = 0; i < launch.args.size(); ++i)
                output("Arg[" + std::to_string(i) + "]: " + launch.args[i]);
        }
    } else launch_app(launch, self);
    return 0;
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // GUI subsystem keeps ordinary URL launches from flashing a console window.
    AttachConsole(ATTACH_PARENT_PROCESS);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try {
        int count = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!argv) fail(winerror("Read arguments"));
        std::vector<std::wstring> args(argv + 1, argv + count);
        LocalFree(argv);
        result = run(args);
    } catch (const std::exception& e) {
        output(e.what(), true);
        if (interactive_error) {
            const auto message = widen(e.what());
            MessageBoxW(nullptr, message.c_str(), L"Bunrui-kun", MB_OK | MB_ICONERROR);
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
