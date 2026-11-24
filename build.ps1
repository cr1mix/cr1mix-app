param([switch]$SkipGen)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$tc   = Join-Path $root ".tools\llvm-mingw-20261104-ucrt-x86_64"
$py   = Join-Path $tc "python\bin\python.exe"
$bin  = Join-Path $tc "bin"
$out  = Join-Path $root "build"
New-Item -ItemType Directory -Force $out, (Join-Path $out "obj") | Out-Null
$env:PATH = "$bin;$env:PATH"
$env:CPFILE = Join-Path $root "tools\ms_codepoints.txt"

if (-not $SkipGen) {
  Write-Host "[1/3] resources" -ForegroundColor Cyan
  & $py -I (Join-Path $root "tools\make_res.py") $root
  if ($LASTEXITCODE) { throw "make_res.py failed" }
}

Write-Host "[2/3] resource script" -ForegroundColor Cyan
Push-Location (Join-Path $root "res")
& windres.exe cr1mix.rc -O coff -o (Join-Path $out "obj\res.o")
$rc = $LASTEXITCODE
Pop-Location
if ($rc) { throw "windres failed" }

Write-Host "[3/3] compile + link" -ForegroundColor Cyan
$flags = @('-std=c++20','-O2','-flto','-DUNICODE','-D_UNICODE','-municode','-mwindows','-fno-exceptions','-ffunction-sections','-fdata-sections','-Wno-everything')
$objs = @()
$jobs = @()
foreach ($src in Get-ChildItem (Join-Path $root "src") -Filter *.cpp) {
  $o = Join-Path $out ("obj\" + $src.BaseName + ".o")
  $objs += $o
  $jobs += Start-Process -FilePath (Join-Path $bin "clang++.exe") -ArgumentList ($flags + @('-c', "`"$($src.FullName)`"", '-o', "`"$o`"")) -NoNewWindow -PassThru -RedirectStandardError (Join-Path $out ("obj\" + $src.BaseName + ".err"))
}
$jobs | Wait-Process
$fail = $false
foreach ($src in Get-ChildItem (Join-Path $root "src") -Filter *.cpp) {
  $e = Join-Path $out ("obj\" + $src.BaseName + ".err")
  $o = Join-Path $out ("obj\" + $src.BaseName + ".o")
  if (-not (Test-Path $o) -or ((Test-Path $e) -and (Get-Content $e -Raw) -match 'error')) { $fail = $true; Write-Host "--- $($src.Name)" -ForegroundColor Red; Get-Content $e | Select-String 'error' | Select-Object -First 25 | ForEach-Object { $_.Line } }
}
if ($fail) { throw "compile failed" }
$libs = @('-ld3d11','-ldxgi','-ld2d1','-ldwrite','-ldcomp','-lwindowscodecs','-ldwmapi','-lole32','-loleaut32','-lshell32','-ladvapi32','-luser32','-lgdi32','-lpdh','-lpsapi','-liphlpapi','-lwinhttp','-lws2_32','-lcabinet','-luuid','-lshlwapi','-lpowrprof','-lsetupapi','-lcfgmgr32','-lwevtapi','-ltaskschd','-lshcore','-lwintrust','-lversion')
$exe = Join-Path $out "cr1mix.exe"
& clang++.exe @flags @objs (Join-Path $out "obj\res.o") -o $exe -static -s '-Wl,--gc-sections' @libs
if ($LASTEXITCODE) { throw "link failed" }
$sz = (Get-Item $exe).Length
$setup = Join-Path $out "cr1mix app setup.exe"
Copy-Item $exe $setup -Force
Write-Host ("OK  {0}  ({1:N1} MB)" -f $exe, ($sz / 1MB)) -ForegroundColor Green
Write-Host ("OK  {0}" -f $setup) -ForegroundColor Green
