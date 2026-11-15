param(
 [Parameter(Mandatory=$true)][string]$Apk,
 [Parameter(Mandatory=$true)][string]$AndroidSdk,
 [Parameter(Mandatory=$true)][string]$JavaHome
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path "$PSScriptRoot/..").Path
$apkPath=(Resolve-Path -LiteralPath $Apk).Path
$sdk=(Resolve-Path -LiteralPath $AndroidSdk).Path
$jdk=(Resolve-Path -LiteralPath $JavaHome).Path
$env:JAVA_HOME=$jdk
$env:PATH="$jdk/bin;"+$env:PATH
$audit=Join-Path $source 'build/apk-audit'
New-Item -ItemType Directory -Force -Path $audit | Out-Null
$zip=[IO.Compression.ZipFile]::OpenRead($apkPath)
try {
 foreach($abi in @('arm64-v8a','armeabi-v7a')) {
  foreach($lib in @('libmain.so','libSDL3.so')) {
   $entry=$zip.GetEntry("lib/$abi/$lib")
   if(!$entry){throw "APK is missing $abi/$lib"}
   $target=Join-Path $audit "$abi-$lib"
   [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$target,$true)
   $elf=& "$sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe" -h -l -s $target
   if($LASTEXITCODE -ne 0){throw 'ELF inspection failed'}
   if($lib -eq 'libmain.so' -and !($elf -match '\bSDL_main\b')){throw 'Native SDL entry point missing'}
   $loads=@($elf | Where-Object {$_ -match '^\s*LOAD\s'})
   if(!$loads.Count){throw 'Missing ELF load segments'}
   $requiredAlignment=if($abi -eq 'arm64-v8a'){16384}else{4096}
   foreach($line in $loads){$alignment=($line.Trim() -split '\s+')[-1];if([Convert]::ToInt32($alignment.Substring(2),16) -lt $requiredAlignment){throw 'ELF load segment alignment invalid'}}
   Write-Output "${abi}/${lib}: entry/export and $requiredAlignment byte load alignment verified"
  }
 }
 foreach($name in @('LICENSE.txt','THIRD-PARTY-NOTICES.txt')) {
  if(!$zip.GetEntry("assets/licenses/$name")){throw "License notice missing: $name"}
 }
 foreach($file in Get-ChildItem -LiteralPath "$source/LICENSES" -File){
  if(!$zip.GetEntry("assets/licenses/LICENSES/$($file.Name)")){throw "License text missing: $($file.Name)"}
 }
 $assetEntry=$zip.GetEntry('assets/grpp-assets.zip')
 if(!$assetEntry){throw 'Game assets missing'}
 $buffer=[IO.MemoryStream]::new()
 $stream=$assetEntry.Open()
 try{$stream.CopyTo($buffer)}finally{$stream.Dispose()}
 $buffer.Position=0
 $assets=[IO.Compression.ZipArchive]::new($buffer,[IO.Compression.ZipArchiveMode]::Read)
 try {
  $checked=0
  foreach($entry in $assets.Entries){
   if($entry.FullName.EndsWith('/')){continue}
   $original=Join-Path "$source/assets" $entry.FullName
   if(!(Test-Path -LiteralPath $original -PathType Leaf)){throw "Unexpected asset $($entry.FullName)"}
   $inputStream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
   try{$hash=[Convert]::ToHexString($sha.ComputeHash($inputStream))}finally{$sha.Dispose();$inputStream.Dispose()}
   if($hash -ne (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash){throw "Asset mismatch: $($entry.FullName)"}
   $checked++
  }
  if($checked -ne @(Get-ChildItem -LiteralPath "$source/assets" -Recurse -File).Count){throw 'Incomplete APK asset payload'}
  Write-Output "$checked packaged game assets match source SHA256 hashes"
 }finally{$assets.Dispose();$buffer.Dispose()}
}finally{$zip.Dispose()}
& "$sdk/build-tools/35.0.0/apksigner.bat" verify --verbose --print-certs $apkPath
if($LASTEXITCODE -ne 0){throw 'APK signature invalid'}
& "$sdk/build-tools/35.0.0/zipalign.exe" -c -P 16 4 $apkPath
if($LASTEXITCODE -ne 0){throw 'APK ZIP alignment invalid'}
& "$sdk/build-tools/35.0.0/aapt2.exe" dump badging $apkPath | Select-String -Pattern 'package:|minSdkVersion:|targetSdkVersion:|native-code:|launchable-activity:|uses-feature|uses-gl-es'
if($LASTEXITCODE -ne 0){throw 'APK manifest inspection failed'}
Write-Output 'APK structure, native libraries, assets, signature and alignment PASS; physical-device execution is a separate check.'

