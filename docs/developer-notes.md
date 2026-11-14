# Developer notes

This page is for people who want to read or check the patcher's source. To build the update file, see [Using the patcher](using-the-patcher.md).

## Layout

| Path | What it holds |
| --- | --- |
| `package.py` | the command-line patcher: reads Nikon's update file and your key material, builds, checks and writes the patched update file |
| `z63patch/` | the Python side: the firmware builder (`gate.py`, `gate_groups/`), the linker, the independent verifiers (`verify_gate.py`, `verify_arm_time.py`, `package_verify.py`), the update-file reader and writer (`package.py`, `package_gate.py`) and the key-material reader (`package_keys.py`) |
| `native/` | the C source compiled into the patched firmware: the safety gate (`arm_time/gate.c`), one source per feature group (`arm_time/groups/`) and the shared hook sites (`arm_time/sites/`) |
| `profiles/` | the reviewed build (`z6iii-c201-arm-time-s7-all-recipe.json`), the identity of Nikon's C2.01 update file and the expected results (`z6iii-c201-package.json`), and two icon profiles |
| `assets/log-icons/` | the four Log live-view icon files from NikonFirmwarePatch, unmodified (see [NOTICE](../NOTICE.md)); `z63patch/log_icons.py` reads them and checks their hashes. `Z63_LOG_ICON_DIR` can name another folder holding the same four files |
| `tools/` | `build_arm_time.py` and `verify_arm_time.py` (build or check the patched firmware image on its own), `make_arm_file.py` (the arm file) |
| `tests/` | unit tests |

The profiles hold addresses, sizes and hashes, not firmware bytes. Everything taken from the firmware is read from your own copy while the patcher runs.

## Running the tests

Use the environment from [Install the dependencies](using-the-patcher.md#install-the-dependencies) and run one test module at a time from the top folder:

```sh
.venv/bin/python -m unittest tests.test_make_arm_file
.venv/bin/python -m unittest tests.test_package_keys
.venv/bin/python -m unittest tests.test_package_gate
.venv/bin/python -m unittest tests.test_gate_import_graph
```

`tests.test_gate_import_graph` checks that the build path loads only modules in this repository, and that the icon files are read and checked as described above.

Without any firmware these run on synthetic data. Tests that need your own inputs are skipped unless you point to them with environment variables:

| Variable | What it points to |
| --- | --- |
| `Z63_STOCK_PACKAGE` | Nikon's `Z6_3_0201.bin` |
| `Z63_PACKAGE_KEY` | your key-material file |
| `Z63_STOCK_MAIN` | the decrypted main firmware of C2.01, if you have it as a file |
| `Z63_STOCK_BODY` | the decrypted body of the C2.01 update file, if you have it as a file (one test) |

With `Z63_STOCK_MAIN` set, `tests.test_gate_import_graph` also builds and verifies the patched firmware image. With `Z63_STOCK_PACKAGE` and `Z63_PACKAGE_KEY` set, `tests.test_package_gate` builds the update file twice and checks that both builds are identical and match the expected result.
