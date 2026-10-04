[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BlenderExe)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $BlenderExe).ProviderPath
$script = Join-Path $PSScriptRoot 'install_addon.py'
& $exe --background --python-exit-code 1 --python $script
if ($LASTEXITCODE -ne 0) { throw 'Blender add-on installation failed.' }
Write-Host 'Endfield Poser Bridge installed. In Blender: 3D View > N > Endfield.'
