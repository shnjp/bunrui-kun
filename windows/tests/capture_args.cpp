// GUI-subsystem helper: exercises real CreateProcess argument transfer without opening an app UI.
#include "router.hpp"
#include <windows.h>
#include <shellapi.h>
#include <fstream>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args || count < 2) return 1;
    std::ofstream output(std::filesystem::path(args[1]), std::ios::binary);
    for (int i = 2; i < count; ++i) output << bunrui::narrow(args[i]) << '\n';
    LocalFree(args);
    return output ? 0 : 1;
}
