# Building Windows Fader Bridge

Windows Fader Bridge is open-source application code, but its native EUCON
adapter requires the separately licensed Avid EUCON Application SDK. The SDK is
not redistributable as part of this repository and is not downloaded by the
build script.

## 1. Requirements

- Windows 11 x64;
- Visual Studio 2022 Build Tools with:
  - Desktop development with C++;
  - MSVC v143 x64 toolchain;
  - a current Windows 10/11 SDK;
- Avid EUCON Application SDK compatible with this project;
- Avid Workstation Unified / EuControl compatible with the installed SDK and
  the target EUCON surface.

The currently verified combination is EUCON SDK 2026.4 and Workstation Unified
2026.4 on Windows 11 x64.

## 2. Obtain the Avid software separately

1. Visit the [official Avid EUCON Application SDK page](https://developer.avid.com/eucon/).
2. Sign in or create an Avid developer account if required.
3. Request or download the EUCON Application SDK/Evaluation Toolkit offered by
   Avid.
4. Read and accept the license supplied by Avid.
5. Install the SDK and the appropriate Avid Workstation Unified/EuControl
   software separately.

Do not copy Avid headers, libraries, examples, PDFs, installers or other SDK
materials into this repository or into a contribution.

## 3. Expected SDK layout

The default installer location expected by the build is:

```text
C:\Program Files\Avid\EUCON SDK\
  include\
    EuConManager.h
    ProcessorAPI\
  lib\
    EuAPI2_vc17.lib
    EuAPI2d_vc17.lib
```

Exact files remain governed by Avid's license and can vary between SDK
releases. Use the SDK version's own installation instructions as authoritative.

## 4. Build

From a PowerShell prompt in the repository root:

```powershell
.\scripts\build-eucon.ps1
```

The Release executable is written to:

```text
artifacts\eucon\Release\WindowsFaderBridge.exe
```

If the SDK is installed somewhere else:

```powershell
.\scripts\build-eucon.ps1 -AvidEuconSdkDir 'D:\SDKs\Avid\EUCON SDK'
```

The script checks for `include\EuConManager.h` before invoking MSBuild. It does
not install, download or copy any SDK component.

## 5. Run and verify

Run only one processor-side EUCON test application at a time. Start EuControl
normally, then launch `WindowsFaderBridge.exe`. Do not include EuControl, Avid
drivers or SDK runtime installers in project releases unless Avid separately
authorizes that distribution.

For model-affecting changes, verify both a physical surface and Avid Control
where available: attachment, banking, faders, touch, encoders, LEDs, labels,
meters, commands and application add/remove behavior.

## 6. Clean-repository rule

Before contributing, check that no proprietary or generated material is staged:

```powershell
git status --short
git diff --cached --name-only
```

The repository ignores `sdk/`, `research/`, build artifacts, logs, installers
and known Avid installer archive names. If in doubt, keep a file outside the
repository and reference the official source instead.
