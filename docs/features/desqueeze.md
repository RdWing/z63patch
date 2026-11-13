# Anamorphic desqueeze

Status: 🟢 works in the emulator. Never run on a camera.

## What it does

Anamorphic lenses squeeze a wide image onto the sensor. Without desqueeze, live view shows everyone tall and thin. z63patch stretches the live view back to its real shape, so you can frame and judge the shot properly.

The ratios are 1.25×, 1.30×, 1.33×, 1.50×, 1.60×, 1.80× and 2.00×.

## How you use it

Choose a ratio under **Desqueeze** in the video recording menu. Choose **Off** to turn it off. The new ratio shows when the menu closes, also in movie mode. The setting is saved with your other settings.

Desqueeze only changes what you see. The recorded file is unchanged, and you desqueeze it in post as usual.

## Design notes

The live view is scaled to the chosen ratio and centred, with bars filling the rest of the screen. AF areas, touch focus and the zoom navigator follow the stretched picture, so tapping a face focuses on that face. Desqueeze works together with [open gate](open-gate.md), which suits anamorphic lenses designed for a taller sensor.

## Tested so far

All in the emulator:

- Every ratio on the rear screen, including with open gate.
- AF areas and touch focus following the stretched image.
- A ratio picked in the menu showing when the menu closes, and returning after a move of the photo/movie lever.

## Still unknown

- The electronic viewfinder. Desqueeze is built for it, but the emulator can't show it, so it needs a real camera.
- Playback and previews still show the squeezed image. Desqueeze there is [under investigation](../../README.md#under-investigation).
- HDMI output.

See also [known limits](../known-limits.md): after power-on in movie mode, a stored ratio shows only after the next recording, lever move, card change or changed setting.
