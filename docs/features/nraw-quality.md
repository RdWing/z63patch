# N-RAW quality tiers

Status: 🟢 works in the emulator. Never run on a camera.

## What it does

The Z6III offers N-RAW in High and Normal. z63patch adds Fine, between them, and Basic, below Normal:

| Tier | Bits per pixel | 6K 30p | 4K 30p |
| --- | ---: | ---: | ---: |
| High | 3.0 | 1,898 Mbps | 858 Mbps |
| Fine (new) | 2.0 | 1,279 Mbps | 586 Mbps |
| Normal | 1.5 | 968 Mbps | 448 Mbps |
| Basic (new) | 1.0 | 660 Mbps | 311 Mbps |

The rates are nominal budgets for 16:9 recording. In [6K open gate](open-gate.md), High uses 2.5 bits per pixel.

## How you use it

With N-RAW as the file type, the quality setting in the video recording menu offers all four tiers. The choice is saved with your settings and survives a restart. Until you pick a tier, N-RAW keeps the High or Normal setting you had in Nikon's menu.

N-RAW needs an exFAT-formatted card. This is the camera's own rule.

## Design notes

N-RAW's encoder already supports several compression levels. Nikon uses two of them for High and Normal. Fine and Basic select two of the others, with budgets worked out the same way Nikon works out its own. Files stay standard N-RAW, so the camera and editing software read them like any other N-RAW clip. Fine and High share the full-quality marking in the file. Normal and Basic share the lower one.

## Tested so far

Some of these checks were made on an earlier build of the same feature.

- Emulator recordings at all four tiers, with each file closing correctly and the camera returning to standby.

The emulator stands in for the RAW encoder, so picture quality can't be judged.

## Still unknown

- Picture quality and real data rates on a camera, especially at Basic.
- How editing software handles Fine and Basic clips in practice.
- Playback shows the tier as High or Normal. Showing names such as "N-RAW FINE" is [under investigation](../../README.md#under-investigation).
