# Notice

## What this repository contains

For now this repository holds documentation and screenshots only. The patch builder isn't published yet and will be added at a later point. No Nikon firmware, original or modified, is included or will be included, and neither are firmware keys or extracted executables.

## Credits

z63patch is its own project, with its own memory layout, hooks and code for the Z6III. A few components of the builder are adapted from [XinmiaoZhao/NikonFirmwarePatch](https://github.com/XinmiaoZhao/NikonFirmwarePatch) (Copyright (C) 2026 NikonFirmwarePatch contributors, GPL-3.0):

- The ELF linker, adapted from `z8precapture/linker.py`.
- The NEF writer, whose TIFF/NEF construction derives from `z8precapture/firmware.c`.
- The pixel-shift merge code, adapted from `z8pixelshift`.
- The method for working out N-RAW bitrate budgets, from `z8opengate/quality.py`.
- The reference Log curves and coefficients used by the offline colour tools.

When the builder is published, each adapted file will keep its upstream license header and name the upstream commit it came from.

## Screenshots

The screenshots in `docs/images/` come from an emulator running the Z6III's own firmware with the patch applied. The menus and icons in them are Nikon's and are shown only to illustrate what the patch changes.

## License and trademarks

This repository is licensed under [GPL-3.0](LICENSE). The license grants no rights to Nikon firmware.

Not affiliated with or endorsed by Nikon. Nikon, Z6III, N-RAW and N-Log are trademarks of Nikon Corporation. ProRes is a trademark of Apple Inc. S-Log3 and S-Gamut3.Cine are trademarks of Sony Group Corporation. Log3G10 and REDWideGamutRGB are trademarks of RED Digital Cinema.
