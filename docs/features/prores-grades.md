# ProRes 422 grades

Status: 🟡 partly working. Never run on a camera.

## What it does

The Z6III records ProRes 422 in HQ only. z63patch adds the other three standard grades, so you can trade data rate for file size:

| Grade | Use |
| --- | --- |
| HQ | Stock. Highest quality |
| Standard | Most grading work, smaller files |
| LT | Long takes and lighter editing |
| Proxy | Offline editing and review |

## How you use it

With ProRes 422 as the file type, a quality page in the video recording menu offers HQ, Standard, LT and Proxy. The choice is saved with your other settings and survives a restart. The shooting screen and the recording menu show the grade you picked, and HQ keeps the camera's quality star.

ProRes RAW is unchanged.

## Design notes

The camera's ProRes encoder takes a grade on every recording request, and the stock firmware always asks for HQ. The patch passes your chosen grade instead and updates the labels, the remaining-time estimates and the file description to match. Nothing else in the recording path changes.

## Tested so far

- The menu page, saving each grade, and restoring it after a restart.
- The camera's recording start sends the right grade to the encoder for all four choices.

The emulator can't produce ProRes files yet, not even with stock firmware at HQ, so no recorded file has been checked.

## Still unknown

- Whether the hardware encoder accepts every grade. This needs a real camera.
- Actual data rates and remaining-time figures for Standard, LT and Proxy.
- Playback of the new grades on the camera.

See also [known limits](../known-limits.md): a recording started by remote, app or HDMI trigger at the moment the patch switches on is untested.
