# Third-party notices

Windows Fader Bridge is licensed under the Mozilla Public License 2.0. The
following separately obtained components are not relicensed by this project.

## Avid EUCON

The native adapter uses public C++ interfaces supplied by the separately
licensed Avid EUCON Application SDK and links locally against its EUCON API
library.

- The Avid SDK, headers, libraries, examples, documentation and installers are
  not included in this repository.
- Contributors and builders must obtain them directly from
  [Avid's EUCON Application SDK page](https://developer.avid.com/eucon/) and
  comply with Avid's applicable terms.
- Avid, EUCON, EuControl, Pro Tools and Avid product names are trademarks or
  registered trademarks of their respective owner. This project is independent
  and is not an Avid product.
- The MPL-2.0 license applies only to the Windows Fader Bridge files for which
  the project has the right to grant that license. It grants no rights to Avid
  materials or technology supplied under separate terms.

The independent Mackie Control host has no Avid SDK or runtime dependency.

## Mackie Control and iCON

Mackie Control, MCU and iCON product names identify interoperability targets;
their trademarks belong to their respective owners. This is an independent
implementation, not an endorsed vendor driver. Manufacturer manuals, firmware,
iMAP software, presets and third-party controller implementation source are not
redistributed. The protocol research notes contain acquisition/source links.

## Microsoft Windows

The project calls Windows system APIs and links against Windows system import
libraries supplied by the Windows SDK. Windows and related product names are
trademarks of Microsoft. The Windows SDK and operating-system components are
not distributed by this repository.

## NuGet development dependencies

The managed support projects restore third-party packages from NuGet, including
NAudio and test tooling. Their package metadata and upstream license files are
authoritative for those components. Restored packages and compiled binaries are
not committed to this repository.
