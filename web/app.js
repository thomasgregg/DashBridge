const status = document.querySelector('#browser-status');
const control = document.querySelector('#install-control');
const description = document.querySelector('#board-description');
const choices = [...document.querySelectorAll('input[name="board"]')];
const supported = window.isSecureContext && 'serial' in navigator;
let ready = false;

function selectBoard() {
  const choice = choices.find(input => input.checked);
  const label = choice.value === 'phone' ? 'A' : 'B';
  description.textContent = label === 'A'
    ? 'Board A receives notifications from your iPhone.'
    : 'Board B sends notifications to your Tesla.';
  // Give each selection a new element. A pending USB chooser keeps the old
  // element and manifest, so changing the selector cannot change its firmware.
  const installer = document.createElement('esp-web-install-button');
  installer.setAttribute('manifest', choice.dataset.manifest);
  const button = document.createElement('button');
  button.className = 'install';
  button.slot = 'activate';
  button.textContent = `Install Board ${label}`;
  button.setAttribute('aria-label', `Install Board ${label}`);
  button.disabled = !ready;
  installer.append(button);
  control.replaceChildren(installer);
  control.hidden = !supported;
}

for (const choice of choices) choice.addEventListener('change', selectBoard);
selectBoard();

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
    selectBoard();
    status.textContent = 'Your browser supports USB installation.';
    status.classList.add('ready');
  } catch (error) {
    console.error('Installer could not load', error);
    status.textContent = 'Could not load the installer. Check your connection and reload this page.';
    status.classList.add('notice');
  }
}

start();
