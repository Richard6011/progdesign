param([ValidateSet('Build','Test','Run','Package')][string]$Action='Build',[ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$cmake='C:/Users/Administrator/AppData/Local/Programs/CMake/cmake-3.31.10-windows-x86_64/bin/cmake.exe'
$preset='c-'+$Configuration.ToLowerInvariant()
$env:PATH="D:/Qt/Tools/mingw1310_64/bin;D:/Qt/Tools/Ninja;$(Split-Path -Parent $cmake);$env:PATH"
Push-Location $projectRoot
try {
 & $cmake --preset $preset
 if($LASTEXITCODE){throw 'Configure failed'}
 & $cmake --build --preset $preset --parallel 6
 if($LASTEXITCODE){throw 'Build failed'}
 if($Action -eq 'Test') { & ctest --preset $preset; if($LASTEXITCODE){throw 'Tests failed'} }
 $buildRoot=Join-Path $projectRoot ('build/'+$Configuration.ToLowerInvariant())
 if($Action -eq 'Run'){Start-Process -FilePath (Join-Path $buildRoot 'NoteCollision.exe') -WorkingDirectory $buildRoot}
 if($Action -eq 'Package'){
  $packageRoot=Join-Path $projectRoot 'out/NoteCollision-C'
  & $cmake --install $buildRoot --prefix $packageRoot
  if($LASTEXITCODE){throw 'Install failed'}
  Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath (Join-Path $projectRoot 'out/NoteCollision-C-Windows-x64.zip') -Force
 }
} finally { Pop-Location }
