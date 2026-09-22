# DashBridge web installer

Public installer: https://thomasgregg.github.io/DashBridge/

The [USB setup page](https://thomasgregg.github.io/DashBridge/setup.html) lets
Chrome or Edge connect directly to each board for pairing, app choices,
connection checks and a Tesla test message. It does not upload settings or
logs. Board A needs a fresh iPhone notification to discover another app;
WhatsApp and WhatsApp Business are allowed by default. Close any other serial
log window before connecting that board in the setup page.

The static site uses ESP Web Tools 10.4.0 and the repository’s prebuilt images. No backend, accounts or analytics. JavaScript, styles, manifests and firmware are served together by GitHub Pages. Chrome or Edge on a desktop computer is the recommended USB installation route.

The completion instructions use Dash Calls, Dash Messages and Dash Tesla.
The 0.4.1-alpha release adds USB setup to the call, music, recent-notification,
and phonebook preview. The [release notes](../docs/releases/0.4.1-alpha.md)
distinguish implementation from hardware validation; the older
[call validation](../docs/CALL_RELAY_VALIDATION.md) is historical.

## Build and preview

From the repository root, with Node.js 22 or newer:

```sh
npm ci --prefix web
npm test --prefix web
npm run build --prefix web
python3 -m http.server 8765 --bind 127.0.0.1 --directory _site
```

Open http://localhost:8765. Web Serial requires HTTPS or localhost. The generated `_site/` directory is ignored by Git.

The build verifies both image checksums, sizes, bootloader target, flash offset and recorded source checksums. It refuses stale or corrupt firmware. Each role gets a distinct ESP32-only manifest and a fingerprinted binary name. Assets are fingerprinted too. Both boards install at offset zero; the existing merged images include their bootloader and partition table. Initial installation erases the selected board, including its pairing keys. Improv/Wi-Fi provisioning is disabled because DashBridge does not implement it.

Dependencies are pinned in `package-lock.json`. The Material Web override is intentional: ESP Web Tools 10.4.0 imports style module names from Material 2.2.0 that were renamed in 2.5.0. Runtime dependency notices are included in the deployed site.

## Completion screen

`install-dialog-patch.mjs` adds a rendering hook to the pinned ESP Web Tools 10.4.0 dialog during bundling. It changes only the successful-install view and the return from logs, without changing flash operations or serial cleanup. `install-success.js` supplies board-specific instructions, Done, Logs, and a selector for the other board. Selecting the other board does not request a USB port or install anything.

The hook requires a confirmed installation, the flasher’s `finished` state, and completed serial reinitialization. Writing 100%, failures, cancelled confirmations and a newly connected board must never display success. Dependency version or method changes fail the build until the integration is reviewed.

## Publishing

The `Web installer` workflow builds and tests on relevant pull requests. On `main`, it also deploys `_site/` through GitHub Pages. Repository Settings → Pages must use **GitHub Actions** as its source. Firmware changes must include rebuilt `dist/` images and their updated manifest before publishing.

## Validation boundary

Browser checks can validate the page, manifest-to-image mapping, help, fallback states and USB-picker cancellation. They cannot establish that a physical flash completes or that the Tesla and iPhone accept the prototype. Validate those on the actual original ESP32-WROOM-32 boards using `docs/CALL_RELAY.md`.

The main page is a stable A/B installer shell. On each visit it resolves the
current manifests, public release number and matching release-notes URL from
`latest.json` before enabling installation. Firmware-specific test results,
setup requirements and limitations belong in `docs/releases/`, not in the
installer copy.
