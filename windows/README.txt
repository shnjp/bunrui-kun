分類くん Windows版
=================
C++17 / Win32。設定画面・常駐プロセスはありません。

ビルド
------
Visual Studio C++ Build Tools + CMake:
  cmake -S windows -B windows/build
  cmake --build windows/build --config Release
  ctest --test-dir windows/build -C Release --output-on-failure

または LLVM-MinGW の clang++ を指定:
  powershell -ExecutionPolicy Bypass -File windows/build.ps1 -Compiler C:\tools\llvm-mingw\bin\clang++.exe
出力: windows/out/bunrui-kun.exe
build.ps1 は LLVM-MinGW 用です。MSVC版clang++を使う場合はCMakeを使ってください。
toml++ 3.4.0 を vendor に同梱。ネット接続なしでビルドできます。

使い始める
----------
1. exeを今後も置く場所に配置します。登録後に移動したら再登録が必要です。
2. config.example.toml を config.toml としてexeの横にコピーして編集します。
   または %LOCALAPPDATA%\bunrui-kun\config.toml に配置します。
   exeの横を優先し、最初に見つかった1ファイルだけを読み込みます。
   上位のファイルが不正な場合に次候補へは進みません。
   ファイルやディレクトリは自動作成しません。
3. PowerShellから確認します（GUI実行ファイルの終了を待つため & とパイプを使用）:
   & .\bunrui-kun.exe --check | Out-Host
   & .\bunrui-kun.exe --dry-run 'https://example.com/' | Out-Host
   & .\bunrui-kun.exe --config .\config.toml --dry-run 'https://app.slack.com/client/T123/C456' | Out-Host
4. 通常の起動を試します:
   & .\bunrui-kun.exe 'https://example.com/' | Out-Host
5. 設定ができたら登録します（ユーザー単位。管理者権限不要）:
   & .\bunrui-kun.exe --register | Out-Host
   & .\bunrui-kun.exe --settings | Out-Host
   Windowsの「既定のアプリ」でBunrui-kunを選び、HTTPとHTTPSを関連付けます。
   実行だけで既定ブラウザを変更することはありません。

解除:
   Windows設定で先にChrome等を既定ブラウザに戻してください。
   & .\bunrui-kun.exe --unregister | Out-Host
   アプリ固有の登録のみ削除します。設定ファイルは残します。

設定
----
UTF-8のTOML。詳細とサンプルはconfig.example.tomlのコメントを参照。
アプリは path + args または uri のどちらか一方で定義します。
pathはexeへのパス。相対パスの基準は採用した設定ファイルの場所です。
環境変数や ~ は展開しません。argsは配列で指定し、シェルを介さず起動します。
カスタムURIはWindowsに登録済みのアプリへ渡します。
{url} は元URL、{1}以降はルールの正規表現のキャプチャ。
置換は1回だけ行い、挿入したURL中の波括弧を再解釈しません。
キャプチャは自動でURLエンコードしません。サンプルのSlackルールはIDの文字種を限定しています。

ルールは上から最初に一致したものを使います。patternはstd::regexのECMAScript文法で
UTF-8バイト列に対する大文字小文字を区別する検索です。
Unicodeプロパティや名前付きキャプチャ、後読みは使えません。
ホストを限定するルールは ^https:// 等から書き、ドメインの . をエスケープしてください。
アプリを増やすときは [apps.名前] と [[rules]] を追加します。
最後にpattern = '.*'のルールを置くことでChrome等を既定の行き先にできます。
無効なTOML、未知のキー・アプリ、不正な正規表現・キャプチャ、一致なしはエラーです。
既定ブラウザへ戻すフォールバックは行いません（自分へ戻るループを防ぐため）。

制約
----
--checkは構文と設定の整合性を検証します。アプリのインストール確認はしません。
--dry-runは設定の選択とURL変換を確認するだけで、アプリ・レジストリを変更しません。
通常起動のエラーはダイアログでも通知します。CLIオプション使用時は標準エラーへ出力します。
CLIは成功時0、失敗時1。URLはhttp(s)の絶対URLに限り、空白等は事前にエンコードしてください。
普段のリンク起動ではコンソールウィンドウを表示しません。

Slackサンプルはapp.slack.com/client/T.../C...のチャンネルリンクが対象です。
メッセージリンク、旧形式、招待、認証ページなどはChromeへ渡します。
Notionサンプルはnotion.so配下で32桁ページIDを含むリンクが対象です。
notion.siteやログイン画面等はChromeへ渡します。Notionのバージョンにより対応状況を確認してください。
Slack/Notionの実クライアントへの遷移は端末にインストール済みのURIハンドラーに依存します。
Chrome内での通常のページ遷移はこのアプリを通りません。

登録仕様: HKCU\Software\Classes\BunruiKun.Url と
HKCU\Software\Clients\StartMenuInternet\BunruiKun、
HKCU\Software\RegisteredApplications の BunruiKun 値を使います。
WindowsのUserChoiceを書き換えず、既定ブラウザの選択はWindows設定で行います。

参照
----
https://learn.microsoft.com/en-us/windows/win32/shell/default-programs
https://docs.slack.dev/interactivity/deep-linking/
https://github.com/marzer/tomlplusplus/tree/v3.4.0
toml++のライセンスは同梱のtomlplusplus-LICENSEを参照してください。
LLVM-MinGWでビルドした配布物にはLLVMランタイムのllvm-LICENSE.txtも同梱します。

検証済み範囲
------------
LLVM-MinGW x64でビルドし、37項目の単体チェックと統合テストを実行しました。
統合テストはサンプルTOML、Slack/NotionのURI生成、Unicodeパス、テスト用exeの
実起動、空白・引用符・空引数・末尾バックスラッシュ・URLの保持、異常入力を確認します。
ユーザーの既定アプリ設定やレジストリはテストでは変更しません。
MSVC/CMake経由のビルド、Slack/Notionの実クライアントへの遷移、
Windows設定で選択した後のOSからのリンク起動は、この環境では未確認です。
