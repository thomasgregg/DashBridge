import { successRenderer } from './install-success.js';

const status = document.querySelector('#browser-status');
const control = document.querySelector('#install-control');
const supported = window.isSecureContext && 'serial' in navigator;
let ready = false;

function renderInstaller() {
  const installer = document.createElement('esp-web-install-button');
  installer.setAttribute('manifest', control.dataset.manifest);
  installer.overrides = { renderInstallSuccess: successRenderer('single') };
  const button = document.createElement('button');
  button.className = 'install';
  button.slot = 'activate';
  button.textContent = 'Install DashBridge';
  button.disabled = !ready;
  installer.append(button);
  control.replaceChildren(installer);
  control.hidden = !supported;
}
renderInstaller();

async function start() {
  if (!supported) {
    status.textContent = 'Open this page in Chrome or Edge on a computer to install. iPhone, iPad and Safari are not supported.';
    status.classList.add('notice');
    return;
  }
  try {
    // Load the dialog before enabling the button so the first click is ready.
    await import('esp-web-tools/dist/install-dialog.js');
    await import('esp-web-tools/dist/install-button.js');
    ready = true;
    renderInstaller();
    status.textContent = 'Your browser supports USB installation.';
    status.classList.add('ready');
  } catch (error) {
    console.error('Installer could not load', error);
    status.textContent = 'Could not load the installer. Check your connection and reload this page.';
    status.classList.add('notice');
  }
}

start();
