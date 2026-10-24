param([switch]$SkipTests,[string]$ToolRoot='',[string]$BuildRoot='')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(!$ToolRoot){$ToolRoot=Join-Path $root '.deps'}
[IO.Directory]::CreateDirectory($ToolRoot)|Out-Null
$ToolRoot=[IO.Path]::GetFullPath($ToolRoot)
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Grab($name,$url,$expected){
 $file=Join-Path $ToolRoot $name
 if(!(Test-Path -LiteralPath $file)){
  Write-Host "Official download: $url"
  Invoke-WebRequest -Uri $url -OutFile ($file+'.download') -UseBasicParsing
  Move-Item -LiteralPath ($file+'.download') -Destination $file
 }
 if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected){throw "SHA256 mismatch: $name"}
 return $file
}
function ExpandSafe($archive,$name){
 $dest=Join-Path $ToolRoot $name
 if(Test-Path -LiteralPath (Join-Path $dest '.ready')){return}
 if(Test-Path -LiteralPath $dest){throw "Incomplete tool extraction: $dest. Use a new -ToolRoot."}
 [IO.Directory]::CreateDirectory($dest)|Out-Null
 $prefix=[IO.Path]::GetFullPath($dest)+[IO.Path]::DirectorySeparatorChar
 $z=[IO.Compression.ZipFile]::OpenRead($archive)
 try{
  foreach($entry in $z.Entries){
   $relative=$entry.FullName.Replace('/',[IO.Path]::DirectorySeparatorChar)
   if([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')){throw 'Unsafe tool archive path'}
   $file=[IO.Path]::GetFullPath([IO.Path]::Combine($dest,$relative))
   if(!$file.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Tool archive traversal'}
   if(!$entry.Name){[IO.Directory]::CreateDirectory($file)|Out-Null}else{
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))|Out-Null
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$file,$false)
   }
  }
 }finally{$z.Dispose()}
 Set-Content -LiteralPath (Join-Path $dest '.ready') -Value 'verified official archive'
}
$zig=Grab 'zig.zip' 'https://ziglang.org/download/0.17.0/zig-x86_64-windows-0.17.0.zip' 'b5663f69581dcf391293fbf16c06cb80d81d806545ce618b4d0bab7f0eb8c428'
$cm=Grab 'cmake.zip' 'https://github.com/Kitware/CMake/releases/download/v4.1.0/cmake-4.1.0-windows-x86_64.zip' '5c09d23786daa89ac433a1a8237f2bdee9f420fb0f2b8a9aded7894d532c0076'
$ni=Grab 'ninja.zip' 'https://github.com/ninja-build/ninja/releases/download/v1.13.1/ninja-win.zip' '26a40fa8595694dec2fad4911e62d29e10525d2133c9a4230b66397774ae25bf'
ExpandSafe $zig 'zig';ExpandSafe $cm 'cmake';ExpandSafe $ni 'ninja'
$env:HX_ZIG=Join-Path $ToolRoot 'zig\zig-x86_64-windows-0.17.0\zig.exe'
$cmake=Join-Path $ToolRoot 'cmake\cmake-4.1.0-windows-x86_64\bin\cmake.exe'
$ctest=Join-Path $ToolRoot 'cmake\cmake-4.1.0-windows-x86_64\bin\ctest.exe'
$ninja=Join-Path $ToolRoot 'ninja\ninja.exe'
if(!$BuildRoot){
 $sha=[Security.Cryptography.SHA256]::Create()
 $id=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root)))).Replace('-','').Substring(0,10)
 $sha.Dispose();$BuildRoot=Join-Path ([IO.Path]::GetTempPath()) ('HX-'+$id)
}
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
if(!$env:ZIG_GLOBAL_CACHE_DIR){$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $BuildRoot 'cache'}
[IO.Directory]::CreateDirectory((Join-Path $root 'logs'))|Out-Null
$log=Join-Path $root 'logs\bootstrap-build.log'
& $cmake -S $root -B $BuildRoot -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_SYSTEM_NAME=Windows' '-DCMAKE_SYSTEM_PROCESSOR=AMD64' "-DCMAKE_C_COMPILER=$PSScriptRoot\zig_cc.cmd" "-DCMAKE_CXX_COMPILER=$PSScriptRoot\zig_cxx.cmd" "-DCMAKE_AR=$PSScriptRoot\zig_ar.cmd" "-DCMAKE_RANLIB=$PSScriptRoot\zig_ranlib.cmd" '-DCMAKE_BUILD_TYPE=Release' '-DHX_PREFER_SYSTEM_LIBS=OFF' '-DHX_ALLOW_FETCH=OFF' 2>&1 | Tee-Object -FilePath $log
if($LASTEXITCODE -ne 0){throw 'CMake configure failed'}
& $cmake --build $BuildRoot --parallel 4 2>&1 | Tee-Object -FilePath $log -Append
if($LASTEXITCODE -ne 0){throw 'Build failed'}
$release=Join-Path $root 'release';[IO.Directory]::CreateDirectory($release)|Out-Null
$dll=Join-Path $release '7z.dll'
if(!(Test-Path -LiteralPath $dll)){throw 'Missing release/7z.dll; restore it from the full verified delivery ZIP'}
if((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne '78BF6B668D75801E3AA180B4CCB16E6D8395FBA80DEC8A065055BB6F8DFABB82'){throw '7z.dll integrity mismatch'}
Copy-Item -LiteralPath $dll -Destination $BuildRoot -Force
if(!$SkipTests){
 & $ctest --test-dir $BuildRoot --output-on-failure 2>&1 | Tee-Object -FilePath $log -Append
 if($LASTEXITCODE -ne 0){throw 'CTest failed; binaries are not published'}
}
foreach($name in @('hx.exe','HyperExtractGUI.exe')){Copy-Item -LiteralPath (Join-Path $BuildRoot $name) -Destination $release -Force}
Write-Host 'Build complete. Start RUN_HYPER_EXTRACT.cmd.'
