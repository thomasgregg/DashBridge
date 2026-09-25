import { currentStatus, disconnectMessage, discoveryNoticeEnded, evaluateChecks,
  unavailableCheckText } from './setup-status.mjs';

const $ = (selector) => document.querySelector(selector);
const supported = window.isSecureContext && 'serial' in navigator;
const boards = [];
const usbLostAt = { phone: 0, car: 0 };
let appsFingerprint = '';
let pendingDiscovery = null;
let discoveryNotice = null;
let disconnectNotice = '';

function feedback(message, ok = true) {
  disconnectNotice = '';
  const node = $('#action-message');
  node.textContent = message;
  node.style.color = ok ? '#176c59' : '#9b4e2c';
}
function showDisconnectNotice(message) {
  feedback(message, false);
  disconnectNotice = message;
}
function clearDisconnectNotice() {
  if (disconnectNotice && $('#action-message').textContent === disconnectNotice) feedback('');
  disconnectNotice = '';
}

class BoardConnection {
  constructor(port) {
    this.port = port;
    this.role = null;
    this.status = null;
    this.apps = null;
    this.lines = '';
    this.closed = false;
    this.writes = Promise.resolve();
  }
  async start() {
    await this.port.open({ baudRate: 115200 });
    clearDisconnectNotice();
    this.readLoop();
    for (const delay of [500, 1800, 3500]) {
      setTimeout(() => { if (!this.closed && !this.status) this.send('setup status'); }, delay);
    }
    this.poll = setInterval(() => {
      if (this.closed) return;
      this.send('setup status');
      if (this.role === 'phone') this.send('setup apps');
    }, 4000);
  }
  async send(command) {
    this.writes = this.writes.then(async () => {
      if (this.closed) return;
      const writer = this.port.writable.getWriter();
      try { await writer.write(new TextEncoder().encode(`${command}\n`)); }
      finally { writer.releaseLock(); }
    }).catch(() => feedback(`Could not send a command to ${this.label()}. Check the USB cable.`, false));
    return this.writes;
  }
  label() {
    return this.role === 'phone' ? 'Board A' : this.role === 'car' ? 'Board B' : 'the board';
  }
  async readLoop() {
    const decoder = new TextDecoder();
    try {
      while (this.port.readable && !this.closed) {
        const reader = this.port.readable.getReader();
        this.reader = reader;
        try {
          for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            this.lines += decoder.decode(value, { stream: true });
            if (this.lines.length > 16000) this.lines = this.lines.slice(-8000);
            let newline;
            while ((newline = this.lines.indexOf('\n')) !== -1) {
              const line = this.lines.slice(0, newline).trim();
              this.lines = this.lines.slice(newline + 1);
              if (!line.startsWith('@DB ')) continue;
              try { this.handle(JSON.parse(line.slice(4))); }
              catch (error) { /* Ignore unrelated or incomplete serial output. */ }
            }
          }
        } finally {
          reader.releaseLock();
          this.reader = null;
        }
      }
    } catch (error) {
      // The finalizer reports the full connection state after removing this board.
    } finally {
      this.closed = true;
      clearInterval(this.poll);
      await this.writes;
      try { await this.port.close(); } catch (error) { /* Already closed on unplug. */ }
      const index = boards.indexOf(this);
      if (index !== -1) boards.splice(index, 1);
      if (pendingDiscovery === this) pendingDiscovery = null;
      if (discoveryNotice?.connection === this) discoveryNotice = null;
      if (this.role === 'phone' || this.role === 'car') usbLostAt[this.role] = Date.now();
      appsFingerprint = '';
      showDisconnectNotice(disconnectMessage(this.role, activeBoards().map(item => item.role)));
      render();
    }
  }
  handle(message) {
    if (!message || typeof message !== 'object') return;
    if (message.type === 'status' && ['phone', 'car'].includes(message.role)) {
      this.role = message.role;
      this.status = message;
      this.statusAt = Date.now();
      if (this.role === 'phone' || this.role === 'car') usbLostAt[this.role] = 0;
      if (message.role === 'phone') this.send('setup apps');
    } else if (message.type === 'apps') {
      this.apps = message;
      this.appsAt = Date.now();
    } else if (message.type === 'result') {
      const text = message.message || (message.ok ? 'Done.' : 'Could not complete that action.');
      if (pendingDiscovery === this) {
        pendingDiscovery = null;
        discoveryNotice = message.ok ? { connection: this, startedAt: Date.now(), message: text } : null;
      } else {
        discoveryNotice = null;
      }
      feedback(text, !!message.ok);
    }
    render();
  }
  async disconnect() {
    this.closed = true;
    clearInterval(this.poll);
    if (this.reader) await this.reader.cancel();
  }
}

function activeBoards() { return boards.filter(item => !item.closed); }
function board(role) { return activeBoards().find(item => item.role === role); }
function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function renderBoards() {
  const container = $('#boards');
  container.replaceChildren();
  const connected = activeBoards();
  if (!connected.length) {
    container.append(element('p', 'empty', 'Neither board is connected to this page. Connect Board A to choose apps, or Board B to check the Tesla side.'));
    return;
  }
  for (const connection of connected) {
    const card = element('div', 'board');
    card.append(element('h3', '', connection.label()));
    card.append(element('p', '', connection.status ? `Firmware ${connection.status.version}` : 'Connecting…'));
    if (connection.status) {
      const pair = element('button', 'secondary', connection.role === 'car' ? 'Open Tesla pairing' : 'Open iPhone pairing');
      pair.addEventListener('click', () => connection.send('setup pair'));
      card.append(pair);
    }
    const disconnect = element('button', 'secondary', 'Disconnect');
    disconnect.style.marginLeft = '7px';
    disconnect.addEventListener('click', () => connection.disconnect());
    card.append(disconnect);
    container.append(card);
  }
}

function renderChecks() {
  const connected = activeBoards();
  const state = evaluateChecks(connected, usbLostAt);
  const checks = [
    ['Boards talking', state.boardLink,
      'Both halves of DashBridge can communicate.', 'Power both boards and check their connection.'],
    ['iPhone Bluetooth', state.phoneBluetooth,
      'Board A is connected to the iPhone.', 'Open iPhone pairing and connect Dash Messages first. Allow notification sharing; Dash Calls appears afterwards.'],
    ['Notification sharing', state.notificationSharing,
      'The iPhone is sharing new notifications.', 'Allow “Share System Notifications” for Dash Messages on your iPhone.'],
    ['Tesla message link', state.teslaTransport,
      'Board B is connected to the Tesla message interface.', 'Connect Dash Tesla from the Tesla Bluetooth screen.'],
    ['Messages ready', state.messagesReady,
      'Tesla message notifications are ready.', state.carSync ? 'Finishing the message notification connection. Check again shortly.' : 'Enable message sync for DashBridge B in the Tesla.'],
    ['Call connection', state.callsReady,
      'Both call connections are ready. Test both audio directions in a real call.', 'Connect the iPhone and Tesla for calls.'],
  ];
  const container = $('#checks');
  container.replaceChildren();
  for (const [title, checkState, readyText, waitingText] of checks) {
    const card = element('div', `check ${checkState === true ? 'ready' : checkState === false ? 'waiting' : ''}`);
    card.append(element('strong', '', `${checkState === true ? '✓ ' : checkState === false ? '○ ' : '– '}${title}`));
    card.append(element('span', '', checkState === true ? readyText : checkState === false ? waitingText :
      unavailableCheckText(title, state, connected.length > 0)));
    container.append(card);
  }
}

function renderApps() {
  const a = board('phone');
  const status = currentStatus(a);
  const discoveryRunning = pendingDiscovery === a ||
    (discoveryNotice?.connection === a && !discoveryNoticeEnded(discoveryNotice, a));
  $('#discover').disabled = !status?.phoneNotifications || discoveryRunning;
  $('#discovery-help').textContent = !a ? 'Connect Board A to choose apps.' :
    !status?.phoneNotifications ? 'Connect your iPhone to Board A and allow notification sharing first.' :
    a.apps?.discovering ? 'Listening for a new notification now. Open the app you want to add.' :
    'To allow an app, start discovery and make it send a new notification.';
  const data = a?.apps;
  const fingerprint = JSON.stringify(data?.allowed) + JSON.stringify(data?.recent);
  if (fingerprint === appsFingerprint) return;
  appsFingerprint = fingerprint;
  const container = $('#apps');
  container.replaceChildren();
  if (!data) return;
  const allowed = new Map((data.allowed || []).map(item => [item.id, item]));
  const all = new Map([...allowed, ...(data.recent || []).map(item => [item.id, item])]);
  for (const [id, item] of all) {
    if (!/^[A-Za-z0-9._-]{1,95}$/.test(id)) continue;
    const row = element('div', 'app');
    const label = element('label');
    const toggle = element('input');
    toggle.type = 'checkbox';
    toggle.checked = allowed.has(id);
    const name = item.name || id.split('.').at(-1);
    label.append(toggle, element('span', '', name));
    const preview = element('select');
    for (const [value, text] of [['0', 'Full preview'], ['1', 'Sender or title only'], ['2', 'App name only']]) {
      const option = element('option', '', text);
      option.value = value;
      preview.append(option);
    }
    preview.value = String(allowed.get(id)?.preview ?? 0);
    preview.disabled = !toggle.checked;
    toggle.addEventListener('change', () => {
      preview.disabled = !toggle.checked;
      a.send(toggle.checked ? `setup allow ${id} ${preview.value}` : `setup deny ${id}`);
    });
    preview.addEventListener('change', () => a.send(`setup allow ${id} ${preview.value}`));
    row.append(label, preview);
    container.append(row);
  }
}

function render() {
  renderBoards();
  renderLiveState();
}

function renderLiveState() {
  renderChecks();
  renderApps();
  if (discoveryNotice && discoveryNoticeEnded(discoveryNotice, discoveryNotice.connection)) {
    const oldMessage = discoveryNotice.message;
    discoveryNotice = null;
    if ($('#action-message').textContent === oldMessage)
      feedback('Discovery has ended. Select “Find an app” to look for another app.', false);
  }
  $('#test').disabled = !currentStatus(board('car'))?.carMessages;
}

$('#connect').disabled = !supported;
$('#browser-message').textContent = supported ?
  'Your browser can connect over USB. Select the board’s USB port when prompted.' :
  'Open this page in Chrome or Edge on a computer. iPhone, iPad and Safari cannot connect to the boards over USB.';
$('#connect').addEventListener('click', async () => {
  let connection;
  try {
    const port = await navigator.serial.requestPort();
    if (boards.some(item => item.port === port)) { feedback('That board is already connected. Choose the other USB port.', false); return; }
    connection = new BoardConnection(port);
    boards.push(connection);
    await connection.start();
    render();
    setTimeout(() => {
      if (!connection.closed && !connection.status)
        feedback('This board did not respond to setup checks. Install the current firmware and try again.', false);
    }, 7000);
  } catch (error) {
    if (connection && !connection.status) {
      connection.closed = true;
      const index = boards.indexOf(connection);
      if (index !== -1) boards.splice(index, 1);
      render();
    }
    if (error.name !== 'NotFoundError') feedback('Could not open that USB port. Close other log windows and try again.', false);
  }
});
$('#discover').addEventListener('click', () => {
  const a = board('phone');
  if (!a) return;
  pendingDiscovery = a;
  a.send('setup discover');
  renderApps();
});
$('#test').addEventListener('click', () => board('car')?.send('setup test'));
render();
setInterval(renderLiveState, 1000);
