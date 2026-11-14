# How the patch behaves

This page describes how the current build is meant to behave on a camera. All of it has been checked only in an emulator of the Z6III. Nothing has been installed on a real camera yet. See [known limits](known-limits.md) for what is untested or behaves differently than you might expect.

## One build, off until you switch it on

There is one build, and it contains every feature listed in the [README](../README.md#features). You don't choose features when the firmware is made.

Once installed, the firmware behaves like stock Nikon firmware until you opt in. You opt in by putting a small **arm file** on the memory card. [Using the patcher](using-the-patcher.md#the-arm-file) explains how to make it and where it goes.

The arm file is a simple on/off switch. With it, the whole patch is active, with every feature. Without it, the camera is stock. You can't use it to turn on individual features.

- With no card in the camera, or with a card that has no arm file, the camera works as stock.
- With the arm file on the card, the patch switches on a few seconds after power-on. This happens in photo or movie mode.
- The patch waits while a menu, playback or the i-menu is open, or while a recording is running, and switches on once that has ended.

While the patch is active, the microphone input row in the video recording menu shows a short marker text in place of its usual value. If you see it, the patch is on.

## Going back to stock behaviour

Remove the card, or remove the arm file from it.

- In photo mode the camera returns to stock behaviour within a few seconds of the card coming out.
- In movie mode, with a menu open, or during a recording, the patch stays on until you switch to photo mode, close the menu or the recording ends. It never switches off in the middle of a recording.
- Putting the card back doesn't switch the patch on again. It switches on at the next power-on.

The patch's own settings (quality tiers, desqueeze ratio, Log type, the 3:2 frame size choice) stay saved while the patch is off and come back when it is next active. **Reset patch settings**, on the patch's quality page, clears them. The camera's **Reset all settings** does not.

## Settings and menus

The new options appear in Nikon's own menus and are saved with the camera's other settings, through the camera's own settings save. A setting you change in movie mode takes effect there, without a trip to photo mode. A change never takes effect in the middle of a recording.

The new menu text is in English only.

## Firmware updates

While the patch is active, the camera refuses to start a firmware update. The refusal lasts until the camera is switched off with the power switch, even if you remove the card in between. In a power-on where the patch never switched on, updating works exactly as on stock firmware.

To update the firmware, or to install Nikon's official firmware again:

1. Remove the card with the arm file, or delete the arm file from it.
2. Switch the camera off with the power switch and wait at least 10 seconds.
3. Switch it on and run the update from the Setup menu as usual.

What the camera shows when it refuses an update hasn't been checked yet.

## Other modified firmware

The patch replaces the camera's main firmware. It can't be combined with any other modified firmware for the Z6III.
