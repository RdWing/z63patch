# Known limits

These are the limits of the current build that are known and accepted for now. Each feature page lists its own open questions as well.

## Nothing has run on a real camera

Every feature works in an emulator of the Z6III. None has been installed on a camera, and the first install hasn't happened. If an installed firmware fails to start, there is no known way for an owner to recover the camera.

## Checked only in the emulator, or not at all

- **Real-length 3:2 clips.** The emulator can only record a few frames of N-RAW, with stock firmware too. Whether the real sensor and RAW encoder handle full-height readout at video frame rates is the biggest open question.
- **ProRes recording.** The emulator can't record ProRes at all, so no ProRes file has been made with the patch.
- **Standby.** The patch is designed to stay on after the camera wakes from standby. The emulator can't model standby, so this is untested.
- **Auto power-off, low battery, waking the camera remotely** and the first start after a firmware update are untested.
- **Viewfinder and HDMI output.** The emulator shows only the rear screen.
- **Colour, picture quality, data rates, heat and battery life** all need a real camera.

## Power-on in movie mode with a stored 3:2 choice or desqueeze ratio

If you switch the camera on with the lever at movie and a 3:2 frame size or a desqueeze ratio was already selected, the patch switches on but the 3:2 frame and the desqueezed view don't appear straight away. They take effect at the first of these:

- starting a recording,
- moving the photo/movie lever,
- inserting or removing a card,
- changing a setting in a menu. Opening and closing a menu without changing anything doesn't count.

A half-press, AF-ON or DISP doesn't trigger it. Whether the first clip after such a power-on is recorded in 3:2 if you press record straight away hasn't been established.

## The i-menu and 3:2 frame sizes

- In the i-menu list, pressing OK on the highlighted regular frame size doesn't switch 3:2 off. Pick a different entry instead.
- On the i-menu strip, every step onto or off a 3:2 option saves the setting at once. Turning the dial across all six 3:2 options saves up to seven times.
- The 3:2 options come after all the regular entries, not next to their 16:9 equivalents.

## A 3:2 choice can come back

The stored 3:2 choice belongs to one frame size and frame rate. Choosing any other size or rate turns it off. One exception: if you change the size or rate while the patch is off, and later return to exactly the same size and rate, the earlier 3:2 choice is active again the next time the patch is on.

## Recording at the moment the patch switches on

A recording started by remote, app or HDMI trigger at the moment the patch switches on is untested. With ProRes, such a clip could be recorded at an inconsistent grade. The patch waits while the record button is pressed, so this concerns only other ways of starting a recording.

## Display

- The tone mode page still reads N-Log whichever Log type you choose.
- Playback shows anamorphic clips squeezed and labels the new N-RAW tiers as High or Normal.
- In rare cases the live view can be one step behind a frame size change until you next move the lever, record or change cards. This affects only the screen, not the recording.
