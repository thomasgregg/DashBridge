const status = document.querySelector('#browser-status');
const hosts = [...document.querySelectorAll('esp-web-install-button')];

async function start() {
  if (!window.isSecureContext || !('serial' in navigator)) {
    status.textContent = 'To install, open this page in Chrome or Edge on a computer and connect your board by USB. Installation is not available on iPhone or iPad.';
    status.classList.add('notice');
    for (const host of hosts) host.hidden = true;
    return;
  }
  try {
    // Load the dialog before enabling its buttons, so the first click is ready.
    await import('esp-web-tools/dist/install-dialog.js');
    await import('esp-web-tools/dist/install-button.js');
    for (const host of hosts) host.querySelector('button').disabled = false;
    status.textContent = 'Your browser is ready. Connect one board to begin.';
    status.classList.add('ready');
  } catch (error) {
    console.error('Installer could not load', error);
    status.textContent = 'The installer could not load. Check your connection and reload this page, or use the manual setup guide below.';
    status.classList.add('notice');
  }
}

start();
