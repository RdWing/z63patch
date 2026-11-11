# Open gate RAW

Status: 🟢 works in the emulator. Never run on a camera.

## What it does

The Z6III records video from a 16:9 window of its sensor. Open gate records the full 3:2 sensor height instead, so you get more image above and below the frame to reframe, stabilise or crop for vertical delivery later.

Two sizes are added, both in N-RAW:

| Menu entry | Recorded size | Frame rates |
| --- | --- | --- |
| [FX 3:2] 6048x4032 | 6K, full sensor | 30p, 25p, 24p |
| [FX 3:2] 4032x2688 | 4K, scaled from the full sensor | 30p, 25p, 24p |

## How you use it

Choose one of the `[FX 3:2]` entries under **Frame size/frame rate** in the video recording menu, with N-RAW as the file type. Choosing any ordinary entry turns open gate off again.

When open gate is on:

- Live view switches to a 3:2 picture, and the red recording border surrounds the whole 3:2 frame.
- AF areas and touch focus cover the 3:2 frame.
- Recorded files carry the full-frame size and crop information, so editing software sees a 3:2 clip.
- It combines with [anamorphic desqueeze](desqueeze.md).

## Design notes

Open gate reuses the sensor's full-height readout, which the camera already has for stills, and feeds it into the normal N-RAW recording path. The patch then updates the menus, the i menu, live view, AF and playback to handle the taller frame.

In 6K open gate the frame is about 18% taller than 16:9 6K, so the data per second would go up by the same amount. To stay within the rates the camera already handles, High quality uses 2.5 bits per pixel in 6K open gate instead of 3.0. The other [N-RAW tiers](nraw-quality.md) are unchanged.

## Tested so far

- Emulator recordings at both sizes, with the correct size and crop metadata in the file.
- Live view, the recording border, AF areas and touch in 3:2.

The emulator stands in for the image sensor and the RAW encoder, so it can't judge the picture itself.

## Still unknown

- Whether the real sensor and RAW encoder accept full-height readout at video frame rates. This is the biggest open question, and only a camera can answer it.
- Heat and sustained recording time.
- Remaining-time and bitrate figures in the menus haven't been checked against real files.
- Menu labels are English only.
- Open gate in H.265 and ProRes, and at 50p and 60p, isn't built yet. On paper the sensor timing and data rate for 50p/60p look sufficient.
