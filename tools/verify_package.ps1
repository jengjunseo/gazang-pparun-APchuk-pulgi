param([string]$Root=(Join-Path $PSScriptRoot '..'))
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
$prefix=$Root+[IO.Path]::DirectorySeparatorChar
$count=0
foreach($line in Get-Content -LiteralPath (Join-Path $Root 'SHA256SUMS.txt') -Encoding UTF8){
 if($line -notmatch '^([a-fA-F0-9]{64})  (.+)$'){throw 'Invalid checksum manifest'}
 $expected=$Matches[1];$name=$Matches[2]
 $path=[IO.Path]::GetFullPath((Join-Path $Root $name))
 if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Manifest path escapes package'}
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $expected){throw "Checksum mismatch: $name"}
 ++$count
}
Write-Host "Verified $count package files."
