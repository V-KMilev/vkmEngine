# Installs vkmEngine for this user, with no administrator: the newest release (or
# $env:VKM_VERSION = '1.2.3') into %LOCALAPPDATA%\Programs\vkmEngine, `vkm` on the user's
# PATH, the editor in the Start Menu, and an entry in Apps & features that removes it all.
# Its games build with GCC, or with Clang under $env:VKM_COMPILER = 'clang'. Installing
# again replaces the engine.
#
#   irm https://github.com/V-KMilev/vkmEngine/releases/latest/download/install.ps1 | iex

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest's bar slows a download tenfold.

$repo     = 'V-KMilev/vkmEngine'
$programs = Join-Path $env:LOCALAPPDATA 'Programs'
$dest     = Join-Path $programs 'vkmEngine'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'vkmEngine.lnk'
$entry    = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\vkmEngine'

function Say($text) { Write-Host "vkmEngine: $text" }

if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'vkmEngine ships for 64-bit Windows only'
}

$compiler = if ($env:VKM_COMPILER) { $env:VKM_COMPILER } else { 'gcc' }
if ($compiler -notin @('gcc', 'clang')) { throw "VKM_COMPILER is gcc or clang, not '$compiler'" }

$version = $env:VKM_VERSION
if (-not $version) {
    $version = (Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest").tag_name.TrimStart('v')
}
$name    = "vkmEngine-$version-windows-x64-$compiler"
$work    = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid())
$archive = Join-Path $work "$name.zip"
New-Item -ItemType Directory -Path $work | Out-Null

try {
    Say "downloading $version, for $compiler"
    Invoke-WebRequest "https://github.com/$repo/releases/download/v$version/$name.zip" -OutFile $archive
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $work)

    # Swapped whole, so a failed unpack leaves the old engine as it was.
    New-Item -ItemType Directory -Force -Path $programs | Out-Null
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Move-Item (Join-Path $work $name) $dest
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

# The editor's icon: an .ico may hold a PNG as it is, behind a 22-byte header.
$png  = [IO.File]::ReadAllBytes((Join-Path $dest 'assets\logo\vkm_engine_icon.png'))
$icon = Join-Path $dest 'vkmEngine.ico'
$head = New-Object IO.MemoryStream
$w    = New-Object IO.BinaryWriter($head)
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]1)              # an icon, one image
$w.Write([Byte]0); $w.Write([Byte]0); $w.Write([Byte]0); $w.Write([Byte]0)   # 256 x 256, no palette
$w.Write([UInt16]1); $w.Write([UInt16]32)                                    # planes, bits per pixel
$w.Write([UInt32]$png.Length); $w.Write([UInt32]22)                          # its size and offset
$w.Write($png)
[IO.File]::WriteAllBytes($icon, $head.ToArray())

# `vkm` from any new terminal: vkm.cmd sits at the engine's root.
$path = [Environment]::GetEnvironmentVariable('Path', 'User')
$dirs = @($path -split ';' | Where-Object { $_ -and $_ -ne $dest })
[Environment]::SetEnvironmentVariable('Path', (@($dest) + $dirs) -join ';', 'User')

$shell = New-Object -ComObject WScript.Shell
$link  = $shell.CreateShortcut($shortcut)
$link.TargetPath       = Join-Path $dest 'bin\vkm_editor.exe'
$link.WorkingDirectory = $env:USERPROFILE
$link.IconLocation     = $icon
$link.Description      = 'Make games with vkmEngine'
$link.Save()

# Everything above, and the tools vkm fetched.
$uninstall = Join-Path $dest 'uninstall.ps1'
@"
`$ErrorActionPreference = 'Continue'
Remove-Item -Force '$shortcut' -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force '$entry' -ErrorAction SilentlyContinue
`$path = [Environment]::GetEnvironmentVariable('Path', 'User')
`$dirs = @(`$path -split ';' | Where-Object { `$_ -and `$_ -ne '$dest' })
[Environment]::SetEnvironmentVariable('Path', `$dirs -join ';', 'User')
Remove-Item -Recurse -Force (Join-Path `$env:LOCALAPPDATA 'vkm') -ErrorAction SilentlyContinue
Set-Location `$env:USERPROFILE
Remove-Item -Recurse -Force '$dest'
Write-Host 'vkmEngine: removed. Your projects are where you left them.'
"@ | Set-Content -Encoding UTF8 $uninstall

New-Item -Force -Path $entry | Out-Null
$values = @{
    DisplayName     = 'vkmEngine'
    DisplayVersion  = $version
    Publisher       = 'vkm'
    InstallLocation = $dest
    DisplayIcon     = $icon
    UninstallString = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$uninstall`""
}
foreach ($key in $values.Keys) { New-ItemProperty -Force -Path $entry -Name $key -Value $values[$key] | Out-Null }
New-ItemProperty -Force -Path $entry -Name NoModify -Value 1 -PropertyType DWord | Out-Null
New-ItemProperty -Force -Path $entry -Name NoRepair -Value 1 -PropertyType DWord | Out-Null

Say "installed $version in $dest; ``vkm doctor`` checks what else this machine needs"
Say 'open a new terminal and run `vkm new mygame`, or open vkmEngine from the Start Menu'
