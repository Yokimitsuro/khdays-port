# Run the KH2 test executable with the usual Release copy's mods, settings, and saves.
param(
    [string]$DataDirectory = (Join-Path $PSScriptRoot '..\..\build\port\obj-release-x64'),
    [string]$Executable = (Join-Path $PSScriptRoot '..\..\build\port\obj-x64\khdays-native.exe')
)
$ErrorActionPreference = 'Stop'
$taskData = (Resolve-Path -LiteralPath $DataDirectory).Path
$taskExe = (Resolve-Path -LiteralPath $Executable).Path
if (-not (Test-Path -LiteralPath (Join-Path $taskData 'data\extracted'))) {
    throw "No data\extracted in $taskData. Pass the usual game folder with -DataDirectory."
}
$taskRunning = @(Get-Process -Name khdays-native -ErrorAction SilentlyContinue)
if ($taskRunning.Count) {
    throw "Close the running game first (process $($taskRunning.Id -join ', ')). A second copy uses a different player's saves/settings."
}
$taskNames = @('KHDAYS_KH2_HUD', 'KHDAYS_PLAYER_SLOT')
$taskOld = @{}
foreach ($taskName in $taskNames) { $taskOld[$taskName] = [Environment]::GetEnvironmentVariable($taskName, 'Process') }
try {
    $env:KHDAYS_KH2_HUD = '1'
    $env:KHDAYS_PLAYER_SLOT = '1'
    Write-Host "Executable: $taskExe"
    Write-Host "Mods, settings, saves: $taskData"
    $taskGame = Start-Process -FilePath $taskExe -WorkingDirectory $taskData -PassThru -Wait
    if ($taskGame.ExitCode) { throw "The game exited with code $($taskGame.ExitCode)." }
} finally {
    foreach ($taskName in $taskNames) { [Environment]::SetEnvironmentVariable($taskName, $taskOld[$taskName], 'Process') }
}
