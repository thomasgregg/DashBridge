# Restore the two-board design

The permanent snapshot is Git tag **v0.1.4**, available from:
https://github.com/thomasgregg/DashBridge/releases/tag/v0.1.4

This release preserves source code, README, setup and wiring instructions,
installer source, both compiled firmware images, source/binary checksums,
and an archive of the built installer. It is an experimental release: the
Tesla standalone test message passed; real iPhone-to-Tesla forwarding has
not been verified.

## Restore hardware

1. Download `phone-merged.bin` for Board A and `car-merged.bin` for Board B
   from the release. Use original ESP32 boards with 4 MB flash.
2. Install each merged image at address `0x0`. Erasing/flashing the merged
   image removes saved Bluetooth pairings on that board.
3. Follow `docs/SETUP.md` from the v0.1.4 source archive for pairing and wiring.
   Board B's `test` USB command sends the standalone Tesla test notification.

`manifest.json` records the binary and source SHA-256 hashes.
`SHA256SUMS` additionally covers the built installer archive and this guide.

## Restore source without disturbing newer work

```sh
git fetch origin --tags
git worktree add ../DashBridge-two-board v0.1.4
```

The new folder contains the complete release snapshot. Use a new branch
from that tag if further two-board development is needed. Never move or
replace the release tag to point at newer single-board work.

## Run the archived installer

Download and extract `dashbridge-installer-v0.1.4.zip`, then run:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory dashbridge-installer-v0.1.4
```

Open http://localhost:8765 in desktop Chrome or Edge. The archive includes
its JavaScript, styles and both firmware images, so it does not depend on
the future contents of the live GitHub Pages site. USB flashing still
requires browser permission and a USB data cable.
