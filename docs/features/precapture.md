# RAW pre-capture

> [!NOTE]
> **Not included in the current build.** RAW pre-capture was explored in an earlier build and worked in the emulator, as described below. The current single build doesn't contain it, and installing the current build won't give you this feature. This page is kept as a record of that work.

Status: ⚪ not in the current build. Explored earlier in the emulator. Never run on a camera.

## What it does

The C15 and C30 release modes include pre-release capture: the camera keeps buffering frames while you half-press, then saves the ones from just before the full press. On the stock Z6III these bursts are JPEG only.

With z63patch, C15 and C30 bursts can be saved as RAW, RAW+JPEG or JPEG.

## How you use it

Select C15 or C30 in the release-mode bar, then set **Image quality** to the format you want. The burst follows that setting. Stock firmware uses a separate **Image quality (HSFC)** setting in these modes instead.

## Design notes

The patch keeps the RAW frames the burst captures and writes each one as a normal NEF, alongside the JPEG when you ask for both. Files use the camera's usual names and folders, and playback shows them like any other RAW file.

## Tested so far

- 94 complete RAW+JPEG pairs across all six JPEG grades.
- C15 RAW, C15 JPEG and C30 RAW bursts, with every file decoding and a clean card afterwards.
- One C30 burst wrote 17 NEFs, and playback showed the last one as RAW L, 6048×4032.

## Still unknown

- How many RAW frames a real camera can buffer, and how fast it can write them to the card. The emulator's timing isn't the camera's.
- Battery and heat during long pre-capture sessions.
- Sustained C30/C60 RAW bursts without pre-capture are a separate item [under investigation](../../README.md#under-investigation).
