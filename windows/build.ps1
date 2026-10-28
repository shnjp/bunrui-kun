param([string]$Compiler = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
if (-not $Compiler) {
    $localCompiler = Join-Path $PSScriptRoot '.tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang++.exe'
    if (Test-Path -LiteralPath $localCompiler) { $Compiler = $localCompiler }
    elseif (Get-Command clang++ -ErrorAction SilentlyContinue) { $Compiler = 'clang++' }
    else { throw 'Use CMake with Visual Studio C++ Build Tools, or pass -Compiler <llvm-mingw clang++.exe>.' }
}
New-Item -ItemType Directory -Force out | Out-Null
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-DUNICODE', '-D_UNICODE', '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN', '-Isrc', '-isystem', 'vendor', '-static')
& $Compiler @flags src/router.cpp src/main.cpp -o out/bunrui-kun.exe -municode -mwindows -lshell32 -lole32 -ladvapi32 -luser32 -lwinhttp -luuid
if ($LASTEXITCODE -ne 0) { throw 'Application build failed' }
if (-not $SkipTests) {
    & $Compiler @flags src/router.cpp tests/router_tests.cpp -o out/router-tests.exe -lshell32
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
    & ./out/router-tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    & $Compiler @flags tests/capture_args.cpp src/router.cpp -o out/capture-args.exe -municode -mwindows -lshell32
    if ($LASTEXITCODE -ne 0) { throw 'Capture helper build failed' }
    & ./tests/integration.ps1 -Exe ./out/bunrui-kun.exe -CaptureExe ./out/capture-args.exe -Sample ./config.example.toml
}
Copy-Item config.example.toml,README.txt,vendor/tomlplusplus-LICENSE,vendor/llvm-LICENSE.txt -Destination out
Write-Output 'Built out/bunrui-kun.exe'
