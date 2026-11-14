# Notice

## What this repository contains

This repository holds the source code of the patcher, its documentation and screenshots. No Nikon firmware, original or modified, is included or will be included, and neither are firmware keys or extracted executables. The patcher works only on firmware and key material that the user supplies.

## Credits

z63patch is its own project, with its own memory layout, hooks and code for the Z6III. A few components of the patcher are adapted from [XinmiaoZhao/NikonFirmwarePatch](https://github.com/XinmiaoZhao/NikonFirmwarePatch) (Copyright (C) 2026 NikonFirmwarePatch contributors, GPL-3.0-only):

- The ELF linker in `z63patch/linker.py` and `z63patch/gate_linker.py`, adapted from `z8precapture/linker.py` (upstream commit `6d04ca510d6f0be10cade96c2289c41e1474af18`); modified by this project, 2026.
- The method for working out N-RAW bitrate budgets in `native/nraw_quality.c`, from `z8opengate/quality.py` (upstream commit `a21fc8c04b6dd863439c4192d387b755633287f6`); modified by this project, 2026.
- The Log recording tables in `native/arm_time/groups/log_tables.inc` and the Log view-assist tables in `native/arm_time/groups/viewassist.c`, taken from the upstream project's published Log data (`z8log/data.json`, upstream commit `a21fc8c04b6dd863439c4192d387b755633287f6`), with some rows recomputed for the Z6III by this project; modified by this project, 2026.
- The four Log live-view icons in `assets/log-icons/` (`slog3_fill.png`, `slog3_outline.png`, `log3g10_fill.png`, `log3g10_outline.png`), unmodified copies of `z8log/icons/` (upstream commit `a21fc8c04b6dd863439c4192d387b755633287f6`).

Earlier research builds also adapted the upstream NEF writer and pixel-shift merge code. Neither is part of this repository.

## Screenshots

The screenshots in `docs/images/` come from an emulator running the Z6III's own firmware with the patch applied. The menus and icons in them are Nikon's and are shown only to illustrate what the patch changes.

## License and trademarks

Copyright (C) 2026 RdWing. This repository is licensed under [AGPL-3.0-only](LICENSE). The adapted components listed under Credits, including the four Log icons, remain under the upstream [GPL-3.0-only](LICENSES/GPL-3.0-only.txt) license, as their SPDX headers state; GPL-3.0 section 13 and AGPL-3.0 section 13 allow the two to be combined in one work. Every source file starts with an SPDX header naming its license and copyright holders. The license grants no rights to Nikon firmware.

Not affiliated with or endorsed by Nikon. Nikon, Z6III, N-RAW and N-Log are trademarks of Nikon Corporation. ProRes is a trademark of Apple Inc. S-Log3 and S-Gamut3.Cine are trademarks of Sony Group Corporation. Log3G10 and REDWideGamutRGB are trademarks of RED Digital Cinema.
