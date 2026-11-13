# H.265 bitrate tiers

Status: 🟢 works in the emulator. Never run on a camera.

## What it does

H.265 on the Z6III has a single bitrate for each size and frame rate. z63patch adds four lower tiers, so you can record longer on a card or hand off smaller files:

| Tier | Bitrate |
| --- | --- |
| 100% | Stock |
| 60% | 60% of stock |
| 45% | 45% of stock |
| 30% | 30% of stock |
| 20% | 20% of stock |

For example, 4K 30p 10-bit drops from 225 Mbps to 135 Mbps at 60%.

## How you use it

With H.265 as the file type, a quality row in the video recording menu offers the five tiers. In the current build the options are labelled with their percentage only. An earlier build also showed the bitrate in Mbps (as in the README screenshot), and real figures may come back later. The choice applies to 8-bit and 10-bit H.265, is saved with your settings and survives a restart. Only the full 100% tier keeps the camera's quality star in the recording menu and in playback.

## Design notes

The tiers scale the target bitrate the camera hands to its own H.265 encoder. Size, frame rate, bit depth and colour are unchanged. The encoder still does all the work with a smaller budget. Each file also records which tier made it, so playback shows the star only on full-rate clips.

## Tested so far

All in the emulator, some on an earlier build of the same feature:

- Recordings at 100% and 60%, with Nikon's encoder set to the chosen bitrate and every file decoding cleanly.
- Saving every tier, and keeping the choice through card changes and mode-dial turns.
- Playback showing the star on a 100% clip and not on a 60% clip.

## Still unknown

- Real output bitrates and picture quality on a camera, especially at 20% and 30%.
- Recordings at 45%, 30% and 20% in the emulator.
- Long recordings, audio and remaining-time figures.
