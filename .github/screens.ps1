# Real screenshots of the app for mytoolkit.ru: runs the installed build with --demo (sample devices)
# on the runner's desktop and saves crops of the taskbar widget, its tooltip and the tray menu to shots/.
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Auto)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, string l, uint f, uint t, out IntPtr r);
}
"@
[W]::SetProcessDPIAware() | Out-Null
$out = Join-Path $PWD "shots"
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $env:LOCALAPPDATA "Programs\Wireless Battery\WirelessBattery.exe"
$reg = "HKCU:\Software\WirelessBattery"
$all = "razer:demo","demo:logi","steelseries:demo","hsc:1b1c:0a6b","hyperx:demo","rog:demo","zowie:demo","bt:demo-ps","xbox:demo","bt:demo-buds","airpods:demo","jabra:demo","jbl:demo","coolermaster:demo","steam:demo","bt:demo-plain"

function Rect($h) { $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null; $r }
function Widget { $tb = [W]::FindWindow("Shell_TrayWnd", $null); [W]::FindWindowEx($tb, [IntPtr]::Zero, "WirelessBatteryWidget", $null) }
function Shot($name, $x1, $y1, $x2, $y2) {
  $sw = [W]::GetSystemMetrics(0); $sh = [W]::GetSystemMetrics(1)
  $x1 = [Math]::Max(0, $x1); $y1 = [Math]::Max(0, $y1); $x2 = [Math]::Min($sw, $x2); $y2 = [Math]::Min($sh, $y2)
  $bmp = New-Object Drawing.Bitmap ($x2 - $x1), ($y2 - $y1)
  $g = [Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x1, $y1, 0, 0, $bmp.Size)
  $bmp.Save((Join-Path $out "$name.png"), [Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Host "saved $name ($($x2-$x1)x$($y2-$y1))"
}
function FullShot($name) { Shot $name 0 0 ([W]::GetSystemMetrics(0)) ([W]::GetSystemMetrics(1)) }
function Theme($light) {
  $p = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
  New-Item $p -Force | Out-Null
  Set-ItemProperty $p SystemUsesLightTheme ([int]$light) -Type DWord
  Set-ItemProperty $p AppsUseLightTheme ([int]$light) -Type DWord
  $r = [IntPtr]::Zero
  [W]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [IntPtr]::Zero, "ImmersiveColorSet", 2, 3000, [ref]$r) | Out-Null
  Start-Sleep 3
}
function Run($show, $opts) {
  Get-Process WirelessBattery -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep 1
  New-Item $reg -Force | Out-Null
  $hide = $all | Where-Object { $show -notcontains $_ }
  New-ItemProperty $reg HiddenDevices -PropertyType MultiString -Value ([string[]]$hide) -Force | Out-Null
  New-ItemProperty $reg DeviceOrder -PropertyType MultiString -Value ([string[]]$show) -Force | Out-Null
  foreach ($k in "BrandColors", "StackedLayout", "WidgetBackground") { Set-ItemProperty $reg $k ([int]($opts -contains $k)) -Type DWord }
  Set-ItemProperty $reg TaskbarWidget 1 -Type DWord
  Set-ItemProperty $reg Language "ru" -Type String
  Start-Process $exe "--demo"
  for ($i = 0; $i -lt 30 -and (Widget) -eq [IntPtr]::Zero; $i++) { Start-Sleep 1 }
  Start-Sleep 5
  $w = Widget
  if ($w -eq [IntPtr]::Zero) { FullShot "debug-nowidget"; throw "widget not found" }
  Rect $w
}
function TaskbarCrop($name, $r) {
  $tb = Rect ([W]::FindWindow("Shell_TrayWnd", $null))
  Shot $name ($r.L - 24) $tb.T ([W]::GetSystemMetrics(0)) $tb.B
}

# Try a bigger desktop; the default runner screen is small.
try { Set-DisplayResolution -Width 1920 -Height 1080 -Force -ErrorAction Stop } catch { Write-Host "resolution unchanged: $_" }
Write-Host ("screen {0}x{1}" -f [W]::GetSystemMetrics(0), [W]::GetSystemMetrics(1))
FullShot "debug-desktop"

$main = "demo:logi", "razer:demo", "xbox:demo", "coolermaster:demo", "steam:demo"
Theme 0
$r = Run $main @()
TaskbarCrop "taskbar-dark" $r

# Tooltip: hover the widget
[W]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2)) | Out-Null
Start-Sleep 3
$tips = New-Object System.Collections.ArrayList
[W]::EnumWindows({ param($h, $l) $s = New-Object Text.StringBuilder 64; [W]::GetClassName($h, $s, 64) | Out-Null; if ($s.ToString() -eq "tooltips_class32" -and [W]::IsWindowVisible($h)) { [void]$tips.Add($h) }; $true }, [IntPtr]::Zero) | Out-Null
if ($tips.Count) {
  $t = Rect $tips[0]
  $tb = Rect ([W]::FindWindow("Shell_TrayWnd", $null))
  Shot "tooltip" ([Math]::Min($t.L, $r.L) - 24) ($t.T - 16) ([W]::GetSystemMetrics(0)) $tb.B
} else { Write-Host "no tooltip" }

# Tray menu: click the widget
[W]::mouse_event(0x2, 0, 0, 0, [IntPtr]::Zero); [W]::mouse_event(0x4, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep 2
$menu = [W]::FindWindow("#32768", $null)
if ($menu -ne [IntPtr]::Zero) {
  $m = Rect $menu
  $tb = Rect ([W]::FindWindow("Shell_TrayWnd", $null))
  Shot "menu" ($m.L - 24) ($m.T - 16) ([Math]::Max($m.R, $r.R) + 24) $tb.B
  [W]::keybd_event(0x1B, 0, 0, [IntPtr]::Zero); [W]::keybd_event(0x1B, 0, 2, [IntPtr]::Zero)
} else { FullShot "debug-nomenu"; Write-Host "no menu" }
[W]::SetCursorPos(10, 10) | Out-Null

$brands = "razer:demo", "demo:logi", "steelseries:demo", "hyperx:demo", "bt:demo-ps", "xbox:demo"
$r = Run $brands @("BrandColors")
TaskbarCrop "brands-dark" $r

$r = Run $main @("StackedLayout")
TaskbarCrop "stacked-dark" $r

Theme 1
$r = Run $main @()
TaskbarCrop "taskbar-light" $r
$r = Run $brands @("BrandColors")
TaskbarCrop "brands-light" $r
FullShot "debug-light"

Get-Process WirelessBattery -ErrorAction SilentlyContinue | Stop-Process -Force
Get-ChildItem $out | Format-Table Name, Length
