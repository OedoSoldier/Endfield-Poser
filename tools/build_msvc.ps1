$ErrorActionPreference = 'Stop'

# Endfield Poser - cmake-free MSVC build.
#
# Why this exists:
#   * This machine has no cmake and no installed Windows SDK, but does have
#     VS2022 VC++ tools. We supply Windows SDK headers/libs from NuGet:
#       - deps\winsdk    (Microsoft.Windows.SDK.CPP  -> c\Include\<ver>\{um,shared,ucrt})
#       - deps\winsdkcpp (Microsoft.Windows.SDK.CPP.x64 -> c\um\x64, c\ucrt\x64)
#   * vcvars64.bat sets the MSVC toolchain (cl on PATH, VC include/lib), but
#     NOT the Windows SDK paths, so we append them explicitly below.
#
# Produces:
#   plugin\poser.dll            (main plugin, self-initializing)
#   plugin\d3dcompiler_47.dll   (DX proxy loader, forwards to System32)
#   plugin\vulkan-1.dll         (Vulkan proxy loader, forwards to System32)
# Then compiles + runs the math unit tests with cl.

$root = Join-Path $PSScriptRoot '..'
Set-Location $root

# ---- 1) Locate MSVC toolchain (vcvars64.bat) ----
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
  $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $vsDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsDir) { $vcvars = Join-Path $vsDir 'VC\Auxiliary\Build\vcvars64.bat' }
  }
}
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found (Visual Studio C++ tools missing?)" }

# ---- 2) Locate Windows SDK headers/libs (NuGet, in deps) ----
$winsdkHdr = Join-Path $root 'deps\winsdk\c\Include'
$sdkVerDir = Get-ChildItem $winsdkHdr -Directory -ErrorAction SilentlyContinue |
  Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkVerDir) { throw "Windows SDK headers not found under deps\winsdk (run setup, see AGENT.md)" }

$sdkInc = @(
  (Join-Path $sdkVerDir.FullName 'um'),
  (Join-Path $sdkVerDir.FullName 'shared'),
  (Join-Path $sdkVerDir.FullName 'ucrt')
) | Where-Object { Test-Path $_ }
if ($sdkInc.Count -lt 3) { throw "SDK include dirs incomplete under $($sdkVerDir.FullName)" }

$sdkLibBase = Join-Path $root 'deps\winsdkcpp\c'
$sdkLibDirUm   = Join-Path $sdkLibBase 'um\x64'
$sdkLibDirUcrt = Join-Path $sdkLibBase 'ucrt\x64'
if (-not (Test-Path $sdkLibDirUm) -or -not (Test-Path $sdkLibDirUcrt)) {
  throw "SDK lib dirs not found under deps\winsdkcpp (run setup, see AGENT.md)"
}

New-Item -ItemType Directory -Force -Path 'plugin' | Out-Null
New-Item -ItemType Directory -Force -Path 'build\tests' | Out-Null
New-Item -ItemType Directory -Force -Path 'build\obj' | Out-Null

$sdkIncFlags = ($sdkInc | ForEach-Object { "/I $_" }) -join ' '
$sdkLibFlags = "/LIBPATH:$sdkLibDirUm /LIBPATH:$sdkLibDirUcrt"

$common = "/nologo /std:c++17 /O2 /MD /EHsc /utf-8 /Fo:build\obj\ /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DIMGUI_DEFINE_MATH_OPERATORS $sdkIncFlags"
$inc    = '/I deps /I deps\imgui /I deps\imguizmo /I deps\minhook_lib\include /I deps\json /I src'

function Invoke-Cl([string]$CompileArgs) {
  # Run vcvars64 and cl in ONE cmd session so INCLUDE/LIB/PATH apply to cl.
  $cmdLine = 'call "' + $vcvars + '" >nul 2>&1 && cl ' + $CompileArgs
  cmd /d /c $cmdLine
  if ($LASTEXITCODE -ne 0) { throw "cl failed: $CompileArgs" }
}

Write-Host '=== Building poser.dll ==='
$poserArgs = "$common /DAPPLEPIE_PLUGIN_IMPL $inc /LD " +
  'src\poser.cpp ' +
  'deps\imgui\imgui.cpp deps\imgui\imgui_draw.cpp deps\imgui\imgui_tables.cpp deps\imgui\imgui_widgets.cpp ' +
  'deps\imgui\imgui_impl_dx11.cpp deps\imgui\imgui_impl_win32.cpp ' +
  'deps\imguizmo\ImGuizmo.cpp ' +
  '/Fe:plugin\poser.dll ' +
  "/link /NODEFAULTLIB:LIBCMT /MAP:plugin\poser.map $sdkLibFlags d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib ole32.lib deps\minhook_lib\lib\libMinHook.x64.lib"
Invoke-Cl $poserArgs

Write-Host ''
Write-Host '=== Building d3dcompiler_47.dll (proxy) ==='
$proxyArgs = "$common /LD src\core\proxy_d3dcompiler.cpp /Fe:plugin\d3dcompiler_47.dll /link $sdkLibFlags"
Invoke-Cl $proxyArgs

Write-Host ''
Write-Host '=== Building vulkan-1.dll (proxy) ==='
$vulkanArgs = "$common /LD src\core\proxy_vulkan_full.cpp /Fe:plugin\vulkan-1.dll /link $sdkLibFlags"
Invoke-Cl $vulkanArgs

Write-Host ''
Write-Host '=== Running math unit tests (MSVC) ==='
$tests = @(
  @{ Name = 'test_quat';      Src = 'tests\test_quat.cpp' },
  @{ Name = 'test_ik';        Src = 'tests\test_ik.cpp' },
  @{ Name = 'test_pose_file'; Src = 'tests\test_pose_file.cpp' }
)
foreach ($t in $tests) {
  Invoke-Cl "$common $inc $($t.Src) /Fe:build\tests\$($t.Name).exe /link $sdkLibFlags"
  & ".\build\tests\$($t.Name).exe"
  if ($LASTEXITCODE -ne 0) { throw "test $($t.Name) failed with exit $LASTEXITCODE" }
}

Write-Host ''
Write-Host '=== Build OK ==='
Write-Host '  plugin\poser.dll'
Write-Host '  plugin\d3dcompiler_47.dll'
Write-Host '  plugin\vulkan-1.dll'
Get-ChildItem 'plugin' -Include *.lib,*.exp -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
Write-Host ''
Write-Host 'Deploy:'
Write-Host '  copy plugin\d3dcompiler_47.dll -> game dir   (overwrites the game'"'"'s own)'
Write-Host '  copy plugin\vulkan-1.dll         -> game dir   (only if game runs Vulkan; can place both)'
Write-Host '  copy plugin\poser.dll          -> game dir\plugin\'
