param(
 [Parameter(Mandatory=$true)][string]$AndroidSdk,
 [Parameter(Mandatory=$true)][string]$SdlSource,
 [Parameter(Mandatory=$true)][string]$JavaHome,
 [string]$GradleUserHome='',
 [string]$NativeBuildDirectory='',
 [string]$Abis='arm64-v8a,armeabi-v7a',
 [switch]$Offline
)
$ErrorActionPreference='Stop'
$source=(Resolve-Path "$PSScriptRoot/..").Path
$sdk=(Resolve-Path -LiteralPath $AndroidSdk).Path
$sdl=(Resolve-Path -LiteralPath $SdlSource).Path
$jdk=(Resolve-Path -LiteralPath $JavaHome).Path
$env:ANDROID_HOME=$sdk
$env:JAVA_HOME=$jdk
$env:PATH="$jdk/bin;"+$env:PATH
if(!$GradleUserHome){$GradleUserHome=Join-Path $source 'android/.gradle-user'}
$env:GRADLE_USER_HOME=[IO.Path]::GetFullPath($GradleUserHome)
$key=Join-Path $source 'android/local/grpp-test.jks'
$privateSigningFile=if($env:OPENMINOTAUR_SIGNING_FILE){$env:OPENMINOTAUR_SIGNING_FILE}else{Join-Path ([Environment]::GetFolderPath('UserProfile')) 'AndroidSigning/openminotaur-signing.json'}
if(!(Test-Path -LiteralPath $privateSigningFile -PathType Leaf) -and !(Test-Path -LiteralPath $key)){
 New-Item -ItemType Directory -Force -Path (Split-Path -Parent $key) | Out-Null
 & "$jdk/bin/keytool.exe" -genkeypair -keystore $key -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=Gridrunner Testing, O=Development, C=US'
 if($LASTEXITCODE -ne 0){throw 'Test signing key generation failed'}
}
if(!$NativeBuildDirectory){$NativeBuildDirectory=Join-Path $source 'build/android-native'}
$native=[IO.Path]::GetFullPath($NativeBuildDirectory)
$arguments=@('--no-daemon','--console=plain',"-PtargetAbis=$Abis","-PsdlSourceDir=$sdl","-PnativeBuildDir=$native",':app:assembleRelease')
if($Offline){$arguments=@('--offline')+$arguments}
Push-Location "$source/android"
try{& './gradlew.bat' @arguments;if($LASTEXITCODE -ne 0){throw 'Android build failed'}}finally{Pop-Location}
$apk=Join-Path $source 'android/app/build/outputs/apk/release/app-release.apk'
& "$sdk/build-tools/35.0.0/apksigner.bat" verify --verbose --print-certs $apk
if($LASTEXITCODE -ne 0){throw 'APK signature verification failed'}
& "$sdk/build-tools/35.0.0/zipalign.exe" -c -P 16 4 $apk
if($LASTEXITCODE -ne 0){throw 'APK alignment verification failed'}
Write-Output "Built and verified: $apk"
