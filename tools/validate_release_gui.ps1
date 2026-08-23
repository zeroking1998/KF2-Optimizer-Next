[CmdletBinding()]
param([string] $Executable = '')

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $projectRoot 'out\package\KF2OptimizerNext\KF2Optimizer.exe'
}
$exe = [IO.Path]::GetFullPath($Executable)
$allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out')).TrimEnd('\') + '\'
if (-not $exe.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw 'GUI validation requires a freshly built executable below out'
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KF2GuiNative {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr h, out uint c);
  public struct RECT { public int Left,Top,Right,Bottom; }
}
'@
$existing = @(Get-Process KF2Optimizer -ErrorAction SilentlyContinue)
if ($existing.Count -ne 0) { throw 'Close existing KF2Optimizer instances before fresh GUI validation' }
$process = Start-Process -FilePath $exe -PassThru
try {
    $window = [IntPtr]::Zero
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
        [KF2GuiNative]::EnumWindows({ param($h,$l)
            [uint32]$windowPid = 0; [void][KF2GuiNative]::GetWindowThreadProcessId($h,[ref]$windowPid)
            if ($windowPid -eq $process.Id -and [KF2GuiNative]::IsWindowVisible($h)) { $script:window = $h; return $false }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Fresh release did not create a visible window' }
    $class = New-Object Text.StringBuilder 128
    $title = New-Object Text.StringBuilder 256
    [void][KF2GuiNative]::GetClassName($window,$class,$class.Capacity)
    [void][KF2GuiNative]::GetWindowText($window,$title,$title.Capacity)
    if ($class.ToString() -cne 'KF2OptimizerNextMainWindow') { throw "Unexpected window class: $class" }
    if (-not $title.ToString().StartsWith('KF2 Optimizer Next - ')) { throw "Unexpected title: $title" }
    $rect = New-Object KF2GuiNative+RECT
    if (-not [KF2GuiNative]::GetWindowRect($window,[ref]$rect) -or
        $rect.Right-$rect.Left -lt 800 -or $rect.Bottom-$rect.Top -lt 500) {
        throw 'Fresh release window is smaller than 800x500'
    }
    $duplicate = Start-Process -FilePath $exe -PassThru
    if (-not $duplicate.WaitForExit(10000)) { $duplicate.Kill(); throw 'Second instance did not fail closed' }
    if ($duplicate.ExitCode -ne 0) { throw "Second instance exit code was $($duplicate.ExitCode), expected 0" }
    [void][KF2GuiNative]::SendMessage($window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
    if (-not $process.WaitForExit(15000)) { throw 'Fresh release did not exit after WM_CLOSE' }
    [uint32]$nativeExit = 0
    if (-not [KF2GuiNative]::GetExitCodeProcess($process.Handle,[ref]$nativeExit) -or $nativeExit -ne 0) {
        throw "Native GUI exit code was $nativeExit"
    }
    $eventLog = Join-Path (Split-Path -Parent $exe) 'Data\logs\session-events.json'
    if (-not (Test-Path -LiteralPath $eventLog -PathType Leaf)) {
        throw 'Fresh release did not create its lifecycle event log'
    }
    $events = Get-Content -LiteralPath $eventLog -Raw | ConvertFrom-Json
    if (@($events.events | Where-Object { $_.code -ceq 'PACKAGE_INTEGRITY_FAILED' }).Count -ne 0) {
        throw 'Fresh release reported a package-integrity failure against its own managed files'
    }
    Write-Host 'PASS: verified portable GUI started normally, enforced single instance, showed a visible window and exited cleanly'
}
finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
