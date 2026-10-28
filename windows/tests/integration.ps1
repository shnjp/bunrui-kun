param([Parameter(Mandatory=$true)][string]$Exe,
      [Parameter(Mandatory=$true)][string]$CaptureExe,
      [Parameter(Mandatory=$true)][string]$Sample)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$CaptureExe = (Resolve-Path -LiteralPath $CaptureExe).Path
$Sample = (Resolve-Path -LiteralPath $Sample).Path
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Invoke-Router([string[]]$Arguments, [int]$ExpectedCode = 0) {
    $output = (& $Exe @Arguments 2>&1 | Out-String)
    if ($LASTEXITCODE -ne $ExpectedCode) { throw "Expected exit $ExpectedCode, got $LASTEXITCODE : $output" }
    return $output
}
$text = Invoke-Router @('--config', $Sample, '--check')
if ($text -notmatch 'Config OK') { throw 'Sample config did not validate' }
$text = Invoke-Router @('--config', $Sample, '--dry-run', 'https://app.slack.com/client/T123/C456')
if (-not $text.Contains('slack://channel?team=T123&id=C456')) { throw 'Slack dry run failed' }
$text = Invoke-Router @('--config', $Sample, '--dry-run', 'https://www.notion.so/Page-0123456789abcdef0123456789abcdef?v=1#block')
if (-not $text.Contains('notion://www.notion.so/Page-0123456789abcdef0123456789abcdef?v=1#block')) { throw 'Notion dry run failed' }
# Preserve Japanese path names by constructing the character independently of PowerShell 5's source encoding.
$dir = Join-Path (Split-Path $Exe) ('integration-' + [char]0x65e5 + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir | Out-Null
try {
    $captured = Join-Path $dir 'captured.txt'
    $config = Join-Path $dir 'test.toml'
    $text = "[apps.capture]`npath = '$CaptureExe'`nargs = ['$captured', '', 'two words', 'C:\space here\', 'a`"b', '{url}']`n[[rules]]`npattern = '.*'`napp = 'capture'`n"
    [IO.File]::WriteAllText($config, $text, $utf8)
    $url = 'https://example.org/a?x=1&y={url}#anchor'
    Invoke-Router @('--config', $config, $url) | Out-Null
    # App launch is deliberately asynchronous. Wait only for the short-lived test helper.
    $expected = @('', 'two words', 'C:\space here\', 'a"b', $url)
    $actual = @()
    for ($i = 0; $i -lt 100; $i++) {
        if (Test-Path -LiteralPath $captured) {
            try { $actual = [IO.File]::ReadAllLines($captured, $utf8) } catch { $actual = @() }
            if ($actual.Count -eq $expected.Count) { break }
        }
        Start-Sleep -Milliseconds 50
    }
    if ($actual.Count -ne $expected.Count) { throw 'Capture helper did not produce expected arguments' }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if ($actual[$i] -cne $expected[$i]) { throw "Argument $i changed during CreateProcess launch" }
    }
    # Errors are emitted through stderr, with no modal dialog for CLI requests.
    # Native stderr records are non-terminating in Windows PowerShell; capture them here.
    $ErrorActionPreference = 'Continue'
    $invalid = Invoke-Router @('--config', $config, '--dry-run', 'file:///C:/Windows/win.ini') 1
    $ErrorActionPreference = 'Stop'
    if ($invalid -notmatch 'absolute http') { throw 'Invalid URL was not rejected' }
    [IO.File]::WriteAllText($config, "[broken", $utf8)
    $ErrorActionPreference = 'Continue'
    Invoke-Router @('--config', $config, '--check') 1 | Out-Null
    $ErrorActionPreference = 'Stop'
    Write-Output 'Integration passed: sample TOML, Slack/Notion dry runs, Unicode paths, real app launch, argument preservation, invalid URL/config.'
} finally {
    # Remove only explicitly named files created by this test; no recursive deletion.
    foreach ($file in @('captured.txt', 'test.toml')) {
        $path = Join-Path $dir $file
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    Remove-Item -LiteralPath $dir
}
