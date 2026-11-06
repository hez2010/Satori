#Requires -Version 7.3
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Prepare', 'Sdk', 'Pack')][string]$Action,
    [string]$Version,
    [Parameter(Mandatory)][string]$Output,
    [string]$Rid,
    [string]$BuildRoot,
    [string]$ArchiveOutput
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$prefix = 'PublishWithSatoriGC'
$groups = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'platforms.json') -Raw | ConvertFrom-Json -AsHashtable
$platforms = @($groups.Values | ForEach-Object { $_ })
[xml]$versions = Get-Content -LiteralPath (Join-Path $repoRoot 'eng/Versions.props') -Raw

if ($Action -eq 'Prepare') {
    if (!$Version) {
        $productVersion = (@('MajorVersion', 'MinorVersion', 'PatchVersion') | ForEach-Object { $versions.SelectSingleNode("//$_").InnerText }) -join '.'
        $Version = "$productVersion-satori.$env:GITHUB_RUN_ID.$env:GITHUB_RUN_ATTEMPT"
    }
    "version=$Version" | Add-Content -LiteralPath $Output -Encoding utf8
    foreach ($group in $groups.GetEnumerator()) {
        $matrix = @{ include = @($group.Value) } | ConvertTo-Json -Depth 8 -Compress
        "$($group.Key)=$matrix" | Add-Content -LiteralPath $Output -Encoding utf8
    }
    Write-Host "Package version: $Version"
    exit 0
}

function Test-NativeDebugSymbolPath([string]$Path) {
    $Path -match '(?i)\.(dbg|debug)$|\.dSYM(?:/|$)'
}

function Assert-NoNativeDebugSymbols([Collections.IDictionary]$Files) {
    foreach ($path in $Files.Keys) {
        if (Test-NativeDebugSymbolPath $path) { throw "Native debug symbols must not be packaged: $path" }
    }
}

function Add-Folder([Collections.IDictionary]$Files, [string]$Root, [string]$Destination, [switch]$ExcludeNativeSymbols, [switch]$ExcludePdb) {
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force) {
        $relative = [IO.Path]::GetRelativePath($Root, $file.FullName).Replace('\', '/')
        if ($ExcludePdb -and ($file.Extension -eq '.pdb' -or $relative -match '^PDB/')) { continue }
        if ($ExcludeNativeSymbols -and ($file.Name -in @('ilc.pdb', 'crossgen2.pdb') -or
            (Test-NativeDebugSymbolPath $relative))) { continue }
        $Files["$Destination/$relative"] = $file
    }
}

function Write-Package([string]$Id, [Collections.IDictionary]$Files, [switch]$Sdk) {
    Assert-NoNativeDebugSymbols $Files
    [xml]$nuspec = @'
<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">
  <metadata>
    <id>placeholder</id><version>0.0.0</version><authors>Satori contributors</authors>
    <description>Publish .NET applications with Satori GC.</description>
    <licenseUrl>https://licenses.nuget.org/MIT</licenseUrl>
    <license type="expression">MIT</license><readme>README.md</readme>
  </metadata>
</package>
'@
    $nuspec.package.metadata.id = $Id
    $nuspec.package.metadata.version = $Version
    if ($Sdk) {
        $types = $nuspec.CreateElement('packageTypes', $nuspec.DocumentElement.NamespaceURI)
        $type = $nuspec.CreateElement('packageType', $nuspec.DocumentElement.NamespaceURI)
        $type.SetAttribute('name', 'MSBuildSdk')
        [void]$types.AppendChild($type)
        [void]$nuspec.package.metadata.AppendChild($types)
    }
    $Files["$Id.nuspec"] = [Text.Encoding]::UTF8.GetBytes($nuspec.OuterXml)
    $Files['[Content_Types].xml'] = [Text.Encoding]::UTF8.GetBytes('<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="nuspec" ContentType="application/octet-stream"/><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/></Types>')
    $Files['_rels/.rels'] = [Text.Encoding]::UTF8.GetBytes("<Relationships xmlns=`"http://schemas.openxmlformats.org/package/2006/relationships`"><Relationship Type=`"http://schemas.microsoft.com/packaging/2010/07/manifest`" Target=`"/$Id.nuspec`" Id=`"manifest`" /></Relationships>")
    $readme = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Raw
    $Files['README.md'] = [Text.Encoding]::UTF8.GetBytes($readme.Replace('__PACKAGE_VERSION__', $Version))
    foreach ($name in @('LICENSE.TXT', 'THIRD-PARTY-NOTICES.TXT')) { $Files[$name] = Get-Item -LiteralPath (Join-Path $repoRoot $name) }
    [void][IO.Directory]::CreateDirectory($Output)
    $path = Join-Path $Output "$Id.$Version.nupkg"
    $stream = [IO.File]::Create($path)
    $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($name in ($Files.Keys | Sort-Object)) {
            $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
            $mode = if ($Files[$name] -is [IO.FileInfo] -and !$IsWindows) {
                0x8000 -bor [int][IO.File]::GetUnixFileMode($Files[$name].FullName)
            } elseif ($name -in @('tools/ilc', 'tools/crossgen2-published/crossgen2')) { 0x81ED } else { 0x81A4 }
            $entry.ExternalAttributes = $mode -shl 16
            $dest = $entry.Open()
            try {
                if ($Files[$name] -is [IO.FileInfo]) {
                    $source = $Files[$name].OpenRead()
                    try { $source.CopyTo($dest) } finally { $source.Dispose() }
                } else {
                    [byte[]]$bytes = $Files[$name]
                    $dest.Write($bytes, 0, $bytes.Length)
                }
            } finally { $dest.Dispose() }
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
    Write-Host $path
}

if ($Action -eq 'Sdk') {
    $files = @{}
    Add-Folder $files (Join-Path $PSScriptRoot 'Sdk') 'Sdk'
    $props = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Sdk/Sdk.props') -Raw
    $files['Sdk/Sdk.props'] = [Text.Encoding]::UTF8.GetBytes($props.Replace('__PACKAGE_VERSION__', $Version))
    Write-Package $prefix $files -Sdk
    exit 0
}

$platform = $platforms | Where-Object rid -EQ $Rid
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$filesByKind = @{ Runtime = @{}; AotSdk = @{} }
# Keep SDK payloads and managed PDBs while omitting native debug sidecars.
Add-Folder $filesByKind.AotSdk (Join-Path $BuildRoot 'aotsdk') 'aotsdk' -ExcludeNativeSymbols
if ($platform.tools) {
    $filesByKind.AotTools = @{}
    Add-Folder $filesByKind.AotTools (Join-Path $BuildRoot 'ilc-published') 'tools' -ExcludeNativeSymbols
    Add-Folder $filesByKind.AotTools (Join-Path $BuildRoot 'crossgen2-published') 'tools/crossgen2-published' -ExcludeNativeSymbols
    $singleFileHost = if ($Rid.StartsWith('win-')) { 'singlefilehost.exe' } else { 'singlefilehost' }
    $filesByKind.Runtime["runtime/corehost/$singleFileHost"] = Get-Item -LiteralPath (Join-Path $BuildRoot "corehost/$singleFileHost")
}
# Upstream emits CoreLib separately. Stage it once so packages and binary
# archives consume the same complete sharedFramework directory.
$sharedFramework = Join-Path $BuildRoot 'sharedFramework'
$coreLib = if ($platform.tools) { 'System.Private.CoreLib.dll' } else { 'IL/System.Private.CoreLib.dll' }
Copy-Item -LiteralPath (Join-Path $BuildRoot $coreLib) -Destination (Join-Path $sharedFramework 'System.Private.CoreLib.dll')
Add-Folder $filesByKind.Runtime $sharedFramework 'runtime/sharedFramework' -ExcludePdb -ExcludeNativeSymbols
if ($ArchiveOutput) {
    # Reuse the package file sets, placing sharedFramework contents at the ZIP root.
    foreach ($kind in $filesByKind.Keys) {
        Assert-NoNativeDebugSymbols $filesByKind[$kind]
        foreach ($entry in $filesByKind[$kind].GetEnumerator()) {
            $relative = switch ($kind) {
                'Runtime' { if ($entry.Key.StartsWith('runtime/sharedFramework/')) { $entry.Key.Substring('runtime/sharedFramework/'.Length) } }
                'AotSdk' { $entry.Key }
                'AotTools' {
                    if ($entry.Key.StartsWith('tools/crossgen2-published/')) { 'crossgen2-published/' + $entry.Key.Substring('tools/crossgen2-published/'.Length) }
                    else { 'ilc-published/' + $entry.Key.Substring('tools/'.Length) }
                }
            }
            if (!$relative) { continue }
            $destination = Join-Path $ArchiveOutput $relative
            [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($destination)))
            Copy-Item -LiteralPath $entry.Value.FullName -Destination $destination
        }
    }
}
foreach ($kind in $filesByKind.Keys) {
    $directory = @{ Runtime = 'runtime'; AotSdk = 'aotsdk'; AotTools = 'tools' }[$kind]
    $props = @'
<Project>
  <ItemGroup>
    <Satori__KIND__Pack Include="__RID__">
      <Root>$([MSBuild]::NormalizeDirectory('$(MSBuildThisFileDirectory)', '..', '__DIRECTORY__'))</Root>
    </Satori__KIND__Pack>
  </ItemGroup>
</Project>
'@
    $id = "$prefix.$kind.$Rid"
    $filesByKind[$kind]["build/$id.props"] = [Text.Encoding]::UTF8.GetBytes($props.Replace('__KIND__', $kind).Replace('__RID__', $Rid).Replace('__DIRECTORY__', $directory))
    Write-Package $id $filesByKind[$kind]
}
