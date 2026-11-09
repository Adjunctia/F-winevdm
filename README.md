# winevdm on 64-bit Windows

![screenshot](screenshot.PNG)

[Download stable version](https://github.com/otya128/winevdm/releases)

[Download latest version (unstable)](https://ci.appveyor.com/project/otya128/winevdm/)

16-bit Windows (Windows 1.x, 2.x, 3.0, 3.1, etc.) on 64-bit Windows

An altered version of winevdm (a 16-bit Windows emulator), ported to 64-bit Windows.

This fork adds the features below on top of [otya128/winevdm](https://github.com/otya128/winevdm); see
[What this fork adds](#what-this-fork-adds).

# How to run

+ If you get an error that VCRUNTIME140.dll is missing, install [Microsoft Visual C++ Redistributable for Visual Studio 2017 (32-bit)](https://aka.ms/vs/15/release/vc_redist.x86.exe)
+ Drag and drop Win16 executable file to otvdm.exe or execute otvdmw.exe.

# How to install

+ Download or compile
+ Run "install" shortcut or right-click on install.inf and select "Install"
+ You can execute Win16 binaries directly!

If the registry is initialized by Windows Update, perform the above procedure again.

You can uninstall it by running uninstall.reg.

# Configuration

See [otvdm.ini](otvdm.ini). otvdm reads the `otvdm.ini` next to otvdm.exe; set the environment
variable `OTVDM_INI=<path>` to use a different file.

# What this fork adds

## Run an app on a secondary monitor (`PreferredMonitor`)

Many Win16 apps size themselves from the primary display and cannot be resized afterwards, so
they come up with the wrong layout on any other monitor. `PreferredMonitor` makes otvdm present
another monitor to the app as if it were the primary display, without changing the Windows
display settings:

```ini
[otvdm]
PreferredMonitor=DISPLAY3   ; device name (or part of it), or a 0-based monitor index
FixScreenSize=1             ; optional: use the monitor's work area (without taskbar) as the screen size
```

With it set:

+ Every way an app can ask for the screen size reports the chosen monitor, with its top-left
  corner at (0,0): `GetSystemMetrics` (`SM_CXSCREEN`, `SM_CYSCREEN`, `SM_CXFULLSCREEN`,
  `SM_CXMAXIMIZED`, ...), `SystemParametersInfo(SPI_GETWORKAREA)`, the rect of the desktop
  window, and `GetDeviceCaps(HORZRES/VERTRES)` on display DCs.
+ Top-level window positions the app sets are moved onto that monitor, and positions read back
  (`GetWindowRect`, `GetWindowPlacement`, `WM_MOVE`, `WM_WINDOWPOSCHANGING/CHANGED`) are relative
  to it, so apps that keep their windows at non-negative coordinates don't jump back to the
  primary. Child windows are not affected.
+ Windows are moved onto the monitor before they are maximized and when they are created at a
  default (`CW_USEDEFAULT`) position.

Monitor coordinates are used exactly as Windows reports them to otvdm (a DPI-unaware process),
so this works with any mix of display scaling. Pointer APIs (`GetCursorPos`, `ClientToScreen`,
...) still use real desktop coordinates. Run once with `ConfigReport=1` to see the monitor list
and indexes.

## Diagnostic log (`ConfigReport`)

```ini
[otvdm]
ConfigReport=1
```

or set the environment variable `OTVDM_CONFIG_REPORT=1` (useful when otvdm.ini itself is not
found). otvdm then appends to `otvdm_config_report.log` next to otvdm.exe (or in the current
directory if that is not writable):

+ which otvdm.ini is used and whether it exists
+ every setting that is read, with its value or `(default, not in ini)`
+ the monitor layout and the chosen `PreferredMonitor`
+ the requested and resulting geometry of windows the app creates and moves

Each distinct line is written once and tagged with the process id, so the log stays short even
for apps that run for a long time. When the option is off nothing is written and there is no
overhead.

## Other fixes

+ Settings that were read from otvdm.ini on every call (`AdjustDPI` in `GetDeviceCaps`,
  `DIBPalette` on drawing paths, `EnvironmentVariableLimitation` per environment variable) are
  now read once.
+ Configuration loading no longer leaves its lock held if the ini path cannot be built.

# How to compile(Visual Studio)

+ Install Visual Studio 2017
+ Edit PropertySheet.props
+ Compile

# How to compile(cmake)

```sh
git clone https://github.com/otya128/winevdm.git
cd winevdm
mkdir build
cd build
cmake ..
make
```

With Visual Studio 2022 (32-bit build; bison and flex from e.g. MSYS2):

```bat
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A Win32 -DBISON_EXECUTABLE=C:/msys64/usr/bin/bison.exe -DFLEX_EXECUTABLE=C:/msys64/usr/bin/flex.exe ..
cmake --build . --config Release
```

The output goes to `build\Release`. Copy your `otvdm.ini` there (or point `OTVDM_INI` at it): a
clean rebuild that deletes the build directory deletes it too.

# How does it work?

This program contains the following items

+ CPU Emulator
  + 64-bit Windows cannot modify LDT(NtSetInformationProcess(,ProcessLdtInformation,,) always returns error)
+ wine based Win16->Win32 conversion codes:
  ```c
  BOOL16 WINAPI DestroyWindow16( HWND16 hwnd )
  {
      return DestroyWindow( WIN_Handle32(hwnd) );
  }
  ```
  Relay routines from 16-bit to 32-bit are autogenerated by convspec
  ```spec
  53  pascal -ret16 DestroyWindow(word) DestroyWindow16
  ```
+ DOS emulation for Win16
+ 16-bit <=> native HANDLE conversion
+ Fix compatibility problems, fix compatibility problems

## install.inf

When 64-bit Windows detects a 16-bit installer, it has a mechanism to start an alternative installer which is not 16-bit.
This program uses it.

## WINDOWS directory redirection

Some Win16 programs try to save their settings in %WINDIR%\<filename>.ini

In recent Windows, it is not allowed to save to %WINDIR%, so it redirects.

# winevdm
```bat
winevdm.exe [--app-name app.exe] command line
winevdm.exe CALC.EXE
```
It can also run DOS executables (DOS emulator-like).
You can set the DOS version with the VDMDOSVER environment variable.

DOS emulation is incomplete and it is recommended to use DOSBox or [MS-DOS Player](http://takeda-toshiya.my.coocan.jp/msdos/).
