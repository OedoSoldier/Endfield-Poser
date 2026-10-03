param([Parameter(Mandatory=$true)][string]$SourceDir)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source=[IO.Path]::GetFullPath($SourceDir)
$out=Join-Path $root 'build\sbm-mmd'
$manifest=Get-Content (Join-Path $root 'integrations\sbm\source-manifest.json') -Raw | ConvertFrom-Json
foreach($entry in $manifest.files.PSObject.Properties) {
  $path=Join-Path $source $entry.Name
  if(!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value) {
    throw "SBM source does not match the supported revision $($manifest.revision): $($entry.Name)"
  }
}
foreach($entry in $manifest.files.PSObject.Properties) {
  $dest=Join-Path $out $entry.Name
  New-Item -ItemType Directory -Force -Path (Split-Path $dest -Parent) | Out-Null
  Copy-Item -LiteralPath (Join-Path $source $entry.Name) -Destination $dest -Force
}
Copy-Item -LiteralPath (Join-Path $root 'integrations\sbm\mmd_bridge.h') -Destination (Join-Path $out 'src\config\mmd_bridge.h') -Force
Copy-Item -LiteralPath (Join-Path $root 'src\integrations\sbm_mmd_api.h') -Destination (Join-Path $out 'src\config\sbm_mmd_api.h') -Force
$utf8=New-Object Text.UTF8Encoding($false)
function Replace-Once([string]$text,[string]$from,[string]$to) {
  if(([regex]::Matches($text,[regex]::Escape($from))).Count -ne 1) {throw "Ambiguous patch anchor: $from"}
  return $text.Replace($from,$to)
}
$file=Join-Path $out 'src\config\config_loader.h'
$content=[IO.File]::ReadAllText($file).Replace("`r`n","`n")
$content=Replace-Once $content '#include "config_types.h"' "#include `"config_types.h`"`n#include `"mmd_bridge.h`""
# Both initial load and hot reload publish a copied table, with no Unity access.
$content=$content.Replace('ConfigSnapshot *installed = new ConfigSnapshot(std::move(fresh));',"ConfigSnapshot *installed = new ConfigSnapshot(std::move(fresh));`n  sbm_mmd_bridge::Publish(*installed);")
[IO.File]::WriteAllText($file,$content,$utf8)
$file=Join-Path $out 'src\motion\motion_engine.h'
$content=[IO.File]::ReadAllText($file).Replace("`r`n","`n")
$guard=@'
    sbm_mmd_bridge::NativeWriter writer;
    if (!writer.locked || MmdSuppressed(writer)) return;
'@
$content=Replace-Once $content '    // FRAME DEDUP for writes:' ($guard+"`n`n    // FRAME DEDUP for writes:")
$content=Replace-Once $content '  void ReplayTarget() {' ("  void ReplayTarget() {`n"+$guard+"`n    ReplayTargetUnlocked();`n  }`n  void ReplayTargetUnlocked() {")
$content=Replace-Once $content '  void OnLateTick() {' ("  void OnLateTick() {`n"+$guard)
$helper=@'
private:
  uint64_t mmdGeneration_ = 0;
  bool MmdSuppressed(const sbm_mmd_bridge::NativeWriter &writer) {
    if (mmdGeneration_ != sbm_mmd_bridge::generation) {
      mmdGeneration_ = sbm_mmd_bridge::generation;
      OnCharacterReset();
      lastWriteFrame_ = -1;
    }
    if (writer.owned(active_.animator)) {
      OnCharacterReset();
      return true;
    }
    return false;
  }
'@
$content=Replace-Once $content 'private:' $helper
[IO.File]::WriteAllText($file,$content,$utf8)

$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs) {throw 'MSVC x64 build tools not found'}
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmd=@"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cd /d "$out"
rc /nologo /fo version.res src\version.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MD /LD /EHsc /std:c++17 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /I"$root\deps\minhook_lib\include" src\sbm.cpp version.res "$root\deps\minhook_lib\lib\libMinHook.x64.lib" user32.lib gdi32.lib winmm.lib comdlg32.lib d3d11.lib dxgi.lib dwmapi.lib ole32.lib winhttp.lib /Fe:sbm.dll /link /DLL /LTCG /NODEFAULTLIB:LIBCMT
exit /b %errorlevel%
"@
$cmdFile=Join-Path $out 'build.cmd'
[IO.File]::WriteAllText($cmdFile,$cmd,[Text.Encoding]::Default)
& cmd.exe /d /c $cmdFile
if($LASTEXITCODE -ne 0) {throw 'SBM compatibility build failed'}
Get-FileHash -LiteralPath (Join-Path $out 'sbm.dll') -Algorithm SHA256
Write-Host 'Built SBM MMD compatibility DLL. No files have been installed.'
