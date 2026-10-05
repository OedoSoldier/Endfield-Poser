# Loaded by the installer; never execute a downloaded script until the entire
# release archive has passed size, digest and extraction checks.
$script:PoserReleaseApi = 'https://api.github.com/repos/OedoSoldier/Endfield-Poser/releases?per_page=30'
$script:PoserMirror = 'https://gh-proxy.org/'

function Get-PoserMirrorPrefix([string]$Mirror) {
    $uri = $null
    if (-not [Uri]::TryCreate($Mirror, [UriKind]::Absolute, [ref]$uri) -or
        $uri.Scheme -ne 'https' -or $uri.UserInfo -or $uri.Query -or $uri.Fragment) {
        throw '镜像地址须为 HTTPS 前缀，例如 https://gh-proxy.org/ 。'
    }
    return $uri.AbsoluteUri.TrimEnd('/') + '/'
}

function Get-PoserDownloadSources([string]$Url, [string]$Source = 'Auto', [string]$Mirror) {
    if ($Source -eq 'GitHub') { return @($Url) }
    $prefix = $script:PoserMirror
    if ($Source -eq 'Custom') { $prefix = Get-PoserMirrorPrefix $Mirror }
    if ($Source -eq 'Auto') { return @($Url, ($prefix + $Url)) }
    return @(($prefix + $Url), $Url)
}

function Receive-PoserFile([string]$Url, [string]$Path, [long]$MaxBytes, [int]$Seconds = 120) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $clock = [Diagnostics.Stopwatch]::StartNew()
    for ($redirect = 0; $redirect -le 8; ++$redirect) {
        $uri = [Uri]$Url
        if ($uri.Scheme -ne 'https' -or $uri.UserInfo) { throw '下载重定向不是有效 HTTPS 地址。' }
        $request = [Net.HttpWebRequest]::Create($uri)
        $request.UserAgent = 'Endfield-Poser-Updater'
        $request.Accept = '*/*'
        $request.AllowAutoRedirect = $false
        $request.Timeout = 15000
        $request.ReadWriteTimeout = 15000
        $response = $null; $inputStream = $null; $outputStream = $null
        try {
            $response = $request.GetResponse()
            if ([int]$response.StatusCode -ge 300 -and [int]$response.StatusCode -lt 400) {
                $Url = ([Uri]::new($uri, $response.Headers['Location'])).AbsoluteUri
                continue
            }
            if ([int]$response.StatusCode -ne 200 -or $response.ContentLength -gt $MaxBytes) { throw '下载大小或状态异常。' }
            $inputStream = $response.GetResponseStream()
            $outputStream = [IO.File]::Open($Path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $buffer = New-Object byte[] 65536
            [long]$total = 0
            while (($read = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $total += $read
                if ($total -gt $MaxBytes -or $clock.Elapsed.TotalSeconds -gt $Seconds) { throw '下载超时或超过大小限制。' }
                $outputStream.Write($buffer, 0, $read)
            }
            if ($total -eq 0 -or ($response.ContentLength -ge 0 -and $total -ne $response.ContentLength)) { throw '下载文件不完整。' }
            return
        } finally {
            if ($outputStream) { $outputStream.Dispose() }
            if ($inputStream) { $inputStream.Dispose() }
            if ($response) { $response.Dispose() }
            $request.Abort()
        }
    }
    throw '下载重定向次数过多。'
}

function Get-PoserRelease([string]$Source, [string]$Mirror, [string]$Work) {
    $jsonPath = Join-Path $Work 'releases.json'
    foreach ($url in Get-PoserDownloadSources $script:PoserReleaseApi $Source $Mirror) {
        try {
            Receive-PoserFile $url $jsonPath 4194304 30
            $releases = [IO.File]::ReadAllText($jsonPath) | ConvertFrom-Json
            # Include prereleases: this project's public builds currently use them.
            $eligible = @()
            foreach ($release in $releases) {
                if ($release.draft -or $release.tag_name -notmatch '^v(\d+\.\d+\.\d+)(?:-[a-zA-Z0-9.-]+)?$') { continue }
                $version = [version]$Matches[1]
                $name = 'Endfield-Poser-' + $release.tag_name + '-win64.zip'
                $assets = @($release.assets | Where-Object { $_.name -ceq $name -and $_.state -eq 'uploaded' })
                if ($assets.Count -ne 1) { continue }
                $asset = $assets[0]
                $expectedUrl = 'https://github.com/OedoSoldier/Endfield-Poser/releases/download/' + $release.tag_name + '/' + $name
                if ($asset.browser_download_url -cne $expectedUrl -or $asset.size -lt 1024 -or $asset.size -gt 268435456 -or
                    -not $asset.PSObject.Properties['digest'] -or $asset.digest -notmatch '^sha256:([a-fA-F0-9]{64})$') { continue }
                $eligible += [pscustomobject]@{ version=$version; tag=$release.tag_name; name=$name; url=$expectedUrl;
                    digest=$Matches[1]; size=[long]$asset.size; published=[datetime]$release.published_at; metadataSource=$url }
            }
            if (-not $eligible.Count) { throw '没有找到带 SHA-256 的 Windows 标准安装包。' }
            return $eligible | Sort-Object version,published -Descending | Select-Object -First 1
        } catch { Write-Warning ('版本查询失败（' + ([Uri]$url).Host + '）：' + $_.Exception.Message) }
    }
    throw '无法查询更新。可稍后重试或选择其他下载源；已安装文件未修改。'
}

function Receive-PoserRelease($Release, [string]$Source, [string]$Mirror, [string]$Work) {
    $archive = Join-Path $Work $Release.name
    foreach ($url in Get-PoserDownloadSources $Release.url $Source $Mirror) {
        try {
            Write-Host ('正在下载：' + ([Uri]$url).Host)
            Receive-PoserFile $url $archive $Release.size
            if ((Get-Item -LiteralPath $archive).Length -ne $Release.size -or (Get-Sha $archive) -ne $Release.digest) {
                throw '安装包 SHA-256 或大小不符，拒绝安装。'
            }
            return $archive
        } catch { Write-Warning $_.Exception.Message }
    }
    throw '所有下载源均失败或校验不通过。已安装文件未修改。'
}

function Expand-PoserRelease([string]$Archive, [string]$Destination, [string]$Tag) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if ($zip.Entries.Count -gt 2048) { throw '安装包文件数量异常。' }
        $root = [IO.Path]::GetFullPath($Destination).TrimEnd('\') + '\'
        $expected = 'Endfield-Poser-' + $Tag + '/'
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        [long]$total = 0
        # Validate every path before extracting any file, including Windows ADS,
        # reserved device names and aliasing through trailing dots/spaces.
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\','/')
            if (-not $name.StartsWith($expected, [StringComparison]::Ordinal) -or
                $name -match '[:\x00-\x1f]' -or $name.Contains('//') -or -not $seen.Add($name)) { throw '安装包包含非法或重复路径。' }
            foreach ($part in $name.TrimEnd('/').Split('/')) {
                if ($part -in @('..','.') -or $part -match '[ .]$|^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw '安装包路径异常。' }
            }
            if ((($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw '安装包不能包含符号链接。' }
            $target = [IO.Path]::GetFullPath((Join-Path $root $name))
            if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw '安装包路径越界。' }
            $total += $entry.Length
            if ($total -gt 1073741824) { throw '安装包展开大小异常。' }
        }
        [IO.Directory]::CreateDirectory($root) | Out-Null
        foreach ($entry in $zip.Entries) {
            $target = Join-Path $root $entry.FullName
            if ($entry.FullName.EndsWith('/')) { [IO.Directory]::CreateDirectory($target) | Out-Null; continue }
            [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    } finally { $zip.Dispose() }
    $package = Join-Path $Destination ('Endfield-Poser-' + $Tag)
    foreach ($relative in @('tools\deploy.ps1','tools\character_face_resources.ps1','plugin\poser.dll','BUILD-INFO.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) { throw "安装包缺少 $relative" }
    }
    $build = Get-Content -LiteralPath (Join-Path $package 'BUILD-INFO.json') -Raw | ConvertFrom-Json
    if ($build.product -ne 'Endfield Poser' -or $build.xxmi_bridge -ne $false -or 'v' + $build.version -ne $Tag) {
        throw '安装包版本或构建类型不符。'
    }
    Assert-Dll (Join-Path $package 'plugin\poser.dll')
    $info = (Get-Item -LiteralPath (Join-Path $package 'plugin\poser.dll')).VersionInfo
    if ($info.ProductName -ne 'Endfield Poser' -or $info.FileVersion -ne $build.version) { throw 'DLL 版本不符。' }
    return $package
}

function Invoke-PoserUpdate([string]$GameRoot, [string]$Source = 'Auto', [string]$Mirror, [switch]$CheckOnly) {
    if ($Source -eq 'Custom') { $Mirror = Get-PoserMirrorPrefix $Mirror }
    $cache = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'EndfieldPoser\Updates'
    Assert-NoLink $cache
    $work = Join-Path $cache ([Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($work) | Out-Null
    $release = Get-PoserRelease $Source $Mirror $work
    $installed = Join-Path $GameRoot 'plugin\poser.dll'
    $current = [version]'0.0.0'
    if (Test-Path -LiteralPath $installed) {
        $info = (Get-Item -LiteralPath $installed).VersionInfo
        if ($info.ProductName -eq 'Endfield Poser') { $current = [version]$info.FileVersion }
    }
    Write-Host "当前版本：$current；最新可用：$($release.tag)（含预发布版）"
    Write-Host ('版本信息来源：' + ([Uri]$release.metadataSource).Host)
    if ($current -ge $release.version) { Write-Host '无需更新，不会降级。'; return }
    if ($CheckOnly) { Write-Host '有新版本。选择在线更新即可下载并安装。'; return }
    Assert-GameStopped
    $archive = Receive-PoserRelease $release $Source $Mirror $work
    $package = Expand-PoserRelease $archive (Join-Path $work 'extracted') $release.tag
    Assert-GameStopped
    Write-Host '校验通过，正在备份并安装；配置、姿态和自定义校准将保留。'
    # Run the new version's installer, so migrations and the installer itself
    # are updated too. No shell-built command strings or downloaded pipe eval.
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $package 'tools\deploy.ps1') -GameDir $GameRoot -SourceRoot $package -Action Install
    if ($LASTEXITCODE -ne 0) { throw "新版安装器返回错误：$LASTEXITCODE；安装包保留在 $package" }
    Write-Host "新版安装包保留在：$package"
}
