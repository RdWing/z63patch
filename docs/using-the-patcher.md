# Using the patcher

> [!CAUTION]
> **Nothing this patcher makes has been installed on a real camera.** Its output is marked `DO_NOT_FLASH` and is named so that the camera won't pick it up. If a patched firmware fails to start, there is no known way to recover the camera without Nikon service. This page explains how to build and check the file. It doesn't explain how to install it.

The patcher is a command-line tool that runs on your own computer. It reads your copy of Nikon's official Z6III C2.01 firmware file and your key material, builds the patched firmware from them, checks the result and writes a new update file. Nothing is downloaded, and nothing is sent anywhere.

There is one build. It contains every feature in the [feature table](../README.md#features) that is marked 🟢 or 🟡, and the [arm file](#the-arm-file) switches all of it on or off together.

## What you need

- **Nikon's official C2.01 firmware file for the Z6III**, `Z6_3_0201.bin`, unmodified, as Nikon distributes it.
- **Key material for that file:** three values, an AES-256 key (32 bytes), a 16-byte counter block and an 8-byte digest suffix, in one file. The layout doesn't matter: JSON, hex text (plain, byte by byte, or as a C array), base64, base32, a list of decimal byte values or raw binary all work, in any order. This project doesn't supply key material and doesn't say how to get it.
- **Python 3** with `venv` and `pip`. The patcher was checked with Python 3.14.
- **clang**, the LLVM C compiler, with support for the 64-bit Arm target (`--target=aarch64-none-elf`). Apple's clang from the Xcode command line tools works. On Linux, install your distribution's `clang` package.
- About 2 GB of free memory, and 150 MB of free disk space for the result.

## Install the dependencies

From the top folder of this repository:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-container.txt
```

This installs one package, `cryptography`, into a private environment in `.venv`. The commands on this page are for macOS and Linux. The patcher has been checked on macOS.

## Build the patched update file

```sh
.venv/bin/python package.py /path/to/Z6_3_0201.bin \
    --key-file /path/to/your-key-file \
    --recipe arm-time-all --build-gate \
    --output patched
```

- `/path/to/Z6_3_0201.bin` is your copy of Nikon's file. It isn't changed.
- `--key-file` is the file that holds your key material. It is read only from the path you give, and its contents are never printed, logged or written anywhere.
- `--recipe arm-time-all --build-gate` selects the one build this project delivers.
- `--output patched` names a new folder for the result. It must not exist yet; the patcher refuses to write into an existing folder.

A build takes about ten seconds. When it succeeds, the patcher prints a report and the folder holds two files:

| File | What it is |
| --- | --- |
| `Z6_3_0201.bin.DO_NOT_FLASH` | the patched update file |
| `manifest.json` | the same report: hashes of the input and output, what changed and every check that passed |

If clang has another name on your system (for example `clang-18`), add `--compiler clang-18`.

## What the patcher checks and refuses

The patcher stops with a message, and writes nothing, if any of these checks fails:

1. **Your firmware file.** It must be exactly Nikon's C2.01 file for the Z6III, identified by its size and SHA-256 hash. This is checked before the key file is even opened. Other models, other versions and modified files are refused.
2. **Your key material.** The key and the counter block are recognised by a one-way fingerprint of their bytes, and the digest suffix by the check value already in Nikon's file, so the key file can be laid out any way you like. A missing or wrong value is reported by name only (`key`, `iv` or `suffix`), never with its contents. Then the key has to decrypt your file to the known contents of Nikon's C2.01 firmware.
3. **The firmware inside.** The components of the update file must be exactly Nikon's.
4. **The build.** The patched firmware must be byte for byte the reviewed build that this repository describes.
5. **Safety checks on the patched firmware.** An independent verifier must accept it. Every changed byte must lie inside the patch's own reserved area. The patch must be inert until armed. The firmware's update code and the part of the firmware that the camera's startup program reads must be unchanged.
6. **The finished file.** A separate verifier, which doesn't share code with the builder, checks every field. The other components of Nikon's update file must be unchanged, byte for byte.
7. **The expected result.** The new file must match the expected result for Nikon's C2.01 file (see [Check your result](#check-your-result)).

## Why the output is named `DO_NOT_FLASH`

The camera looks for update files named like `Z6_3_???.BIN` on the card. On an exFAT card it also matches a longer name by its first eight characters and its last extension. A name such as `Z6_3_0201.DO_NOT_FLASH.bin` would therefore still be seen as an update file. The patcher's name, `Z6_3_0201.bin.DO_NOT_FLASH`, ends in `.DO_NOT_FLASH`, so the camera won't offer it as an update, even if the file ends up on a card by accident.

## Check your result

The same input always gives the same output, on any computer. Built from Nikon's unmodified C2.01 file, the patched update file has this SHA-256 hash:

```text
4125148a76d869af646a20d44db943d0145aacef8ff4a00993a7a90dd2cde9d1
```

The patcher already compares its result with this value and refuses to write anything else. To check a written file yourself:

```sh
shasum -a 256 patched/Z6_3_0201.bin.DO_NOT_FLASH      # macOS
sha256sum patched/Z6_3_0201.bin.DO_NOT_FLASH          # Linux
```

To run the independent package verifier on a written file, for example after copying it somewhere:

```sh
.venv/bin/python package.py /path/to/Z6_3_0201.bin \
    --key-file /path/to/your-key-file \
    --recipe arm-time-all \
    --verify-package patched/Z6_3_0201.bin.DO_NOT_FLASH
```

It decrypts the file with its own code, checks every field and every unchanged byte, reruns the safety checks on the firmware inside, and prints a report with `"passed": true`. Any difference stops it with a message.

### Checking that a release is genuine

Releases are signed by RdWing with an SSH key. `RELEASE-HASHES.txt` lists the official hashes of the input file, the patched update file, the firmware image inside it and the arm file. `RELEASE-HASHES.txt.sig` is its signature. Release tags are signed with the same key, and GitHub shows them as "Verified".

To check the hashes file and a release tag from a copy of this repository:

```sh
ssh-keygen -Y verify -f .github/allowed_signers \
    -I 19214916+RdWing@users.noreply.github.com -n file \
    -s RELEASE-HASHES.txt.sig < RELEASE-HASHES.txt
git -c gpg.ssh.allowedSignersFile=.github/allowed_signers tag -v TAG
```

Both should report a good signature from `19214916+RdWing@users.noreply.github.com` with key `SHA256:6y4PYobuXsvVWN6oxEEkMLFOSGvj35gLPAMcvoZuFbM`. A copy that has different hashes, or one with no valid signature from this key, is not an official release. If more signing keys are added later, they will be listed in `.github/allowed_signers`, and the signatures that older keys made stay valid.

## The arm file

The patched firmware does nothing until it finds the arm file on the memory card. The arm file is a 64-byte file named `Z63ARM.BIN`. It is the same for everyone who builds from Nikon's C2.01 file, and making it needs neither the firmware nor key material:

```sh
.venv/bin/python tools/make_arm_file.py --output Z63ARM.BIN
```

Copy `Z63ARM.BIN` to the top-level folder of the memory card, not into `DCIM` or any other folder. The firmware reads it from drive A, which should be slot 1, the CFexpress slot. That hasn't been confirmed on a camera yet, so until this page says it has, put a copy on the cards in both slots. A copy that the firmware doesn't read has no effect.

The arm file is an on/off switch for the whole patch. With it on the card, every feature switches on a few seconds after power-on. Without it, the camera behaves as stock. You can't use it to switch on individual features. [How the patch behaves](how-it-works.md) describes switching on and off in detail.

## Going back to stock

- **Stock behaviour, patch still installed:** remove the arm file from the card, or remove the card. At the next power-on the camera behaves as stock Nikon firmware. In photo mode it also returns to stock within a few seconds of the card coming out.
- **Stock firmware:** install Nikon's official C2.01 again through the camera's normal firmware update. The patch refuses to start an update while it is active, so first remove the card with the arm file (or delete the file), switch the camera off with the power switch for at least 10 seconds, switch it on and then update as usual. Installing Nikon's C2.01 over the same version has been done on a stock camera. Installing it over a patched firmware hasn't been tried, because no patched firmware has been installed yet.

## Troubleshooting

| Message | What it means |
| --- | --- |
| `input is not the stock Nikon Z6III C2.01 update file …` | The firmware file isn't Nikon's unmodified C2.01 file for the Z6III. Check that you have the right model and version, and that the download is complete. |
| `local C2.01 material: no value matching the expected fingerprint or digest for …` followed by `iv`, `key` or `suffix` | The key file doesn't contain the named values, or contains them split up or encrypted. A missing or wrong digest suffix is reported here too. Only the names of the missing values are shown. |
| `local C2.01 material file is missing or unreadable` / `… is too large` | The path given with `--key-file` is wrong, or the file is larger than 1 MB. |
| `the key material does not decrypt this file to the known stock C2.01 body (wrong or damaged key file)` | The values were found, but they don't decrypt this file. |
| `missing Log icon …`, `upstream Log icon differs: …` or `unexpected Log icon geometry: …` | A file in `assets/log-icons/` is missing or changed. Use an unmodified copy of this repository. |
| `output path already exists` | Choose a new folder name for `--output`, or remove the old folder yourself. |
| `install requirements-container.txt in an isolated environment` | The `cryptography` package is missing. Run the commands from [Install the dependencies](#install-the-dependencies) and use `.venv/bin/python`. |
| `[Errno 2] No such file or directory: 'clang'` | clang isn't installed, or has another name. See [What you need](#what-you-need) and the `--compiler` option. |
| any other refusal (`… differs`, `… is not stock …`, `verify_gate did not accept the image`, `package differs from the expected package for stock C2.01`) | A safety check failed. With Nikon's unmodified file, the right key material and an unmodified copy of this repository this shouldn't happen. Please open an issue with the message. Don't share your key material. |

## At your own risk: an installable file name

> [!WARNING]
> **Read this section only if you have decided to accept the risk of losing your camera.** Nothing this patcher makes has been installed on a real camera. Whether a Z6III accepts a patched update file is not known. If the patched firmware doesn't start, there is no known way to recover the camera without Nikon service. The project gives no install instructions and doesn't ask anyone to test builds on their own camera.

The patcher can write the update file under the name the camera recognises, `Z6_3_0201.bin`, instead of `Z6_3_0201.bin.DO_NOT_FLASH`. This needs a deliberate option whose value is a statement that you understand the risk:

```sh
--installable-name I-UNDERSTAND-THIS-IS-UNTESTED-AND-CAN-BRICK-THE-CAMERA
```

