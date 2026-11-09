param([Parameter(Mandatory=$true)][string]$Module, [string]$BaseIpa = 'C:\Users\Charl\Downloads\LiveContainer+SideStore.ipa', [string]$OutputIpa = 'D:\LiveContainer-SecondScreen\build-output\LiveContainer-RemoteTouch.ipa')
$ErrorActionPreference = 'Stop'
if ((Get-FileHash -LiteralPath $BaseIpa -Algorithm SHA256).Hash -ne '97DC0FD2202FD4460EFCAB389943B8D5FDBB4988EFFF76B116B92B84A4662425') { throw 'Unexpected base IPA; verify its version.' }
$moduleBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Module))
if ($moduleBytes.Length -lt 10000 -or [BitConverter]::ToUInt32($moduleBytes,0) -ne 0xfeedfacfL) { throw 'Not a device Mach-O binary.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
Copy-Item -LiteralPath $BaseIpa -Destination $OutputIpa
$zip = [IO.Compression.ZipFile]::Open($OutputIpa,[IO.Compression.ZipArchiveMode]::Update)
try {
 $target = 'Payload/LiveContainer.app/Frameworks/TweakLoader.dylib'
 $old = $zip.GetEntry($target)
 if (!$old) { throw 'Missing base module.' }
 $old.Delete()
 $stream = $zip.CreateEntry($target).Open()
 try { $stream.Write($moduleBytes,0,$moduleBytes.Length) } finally { $stream.Dispose() }
} finally { $zip.Dispose() }
$zip = [IO.Compression.ZipFile]::OpenRead($OutputIpa)
try {
 foreach ($name in @('LiveContainer','Frameworks/TweakLoader.dylib','Frameworks/SideStoreApp.framework/SideStore','Frameworks/SideStore.framework/SideStore')) {
  $entry = $zip.GetEntry('Payload/LiveContainer.app/'+$name)
  if (!$entry -or $entry.Length -eq 0) { throw "Missing executable: $name" }
 }
} finally { $zip.Dispose() }
Get-FileHash -LiteralPath $OutputIpa -Algorithm SHA256
