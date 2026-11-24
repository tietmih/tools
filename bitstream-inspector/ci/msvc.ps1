# MSVC compile check. The media layer is skipped because only the mingw-built
# FFmpeg import libraries are bundled.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-msvc'

& cmake -S $root -B $build -A x64 -DBS_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
& cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "MSVC build failed" }
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "MSVC unit tests failed" }
Write-Host "MSVC build and unit tests passed"
