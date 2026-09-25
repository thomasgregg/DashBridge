import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { patchInstallDialog } from './install-dialog-patch.mjs';

const original = await readFile(new URL('./node_modules/esp-web-tools/dist/install-dialog.js', import.meta.url), 'utf8');
const patched = patchInstallDialog(original);
// Exercise the actual patched upstream methods without a browser or USB port.
// html only needs to stand in for Lit templates; assertions target screen choice.
function method(name, next) {
  const text = patched.slice(patched.indexOf(`    ${name}() {`), patched.indexOf(`    ${next}() {`));
  return new Function('html', 'ERROR_ICON', 'OK_ICON', 'listItemInstallIcon', 'listItemConsole', `return ({${text}})`)((strings) => strings.join(''), 'error', 'ok', '', '')[name];
}
const renderInstall = method('_renderInstall', '_renderLogs');
const renderDashboard = method('_renderDashboardNoImprov', '_renderProvision');
function context(state, client = null, confirmed = true) {
  return {
    _installConfirmed: confirmed, _installState: state, _client: client,
    _manifest: { name: 'DashBridge A — iPhone', version: '0.5.2-alpha' },
    _renderProgress: () => 'progress',
    overrides: { renderInstallSuccess: () => ['Board A installed', 'next steps', true] },
  };
}

test('success requires finished flash, confirmation and completed reinitialization', () => {
  assert.equal(renderInstall.call(context({ state: 'finished' }))[0], 'Board A installed');
  assert.equal(renderInstall.call(context({ state: 'writing', details: { percentage: 100 } }))[0], 'Installing');
  assert.equal(renderInstall.call(context({ state: 'error', message: 'Write failed' }))[0], 'Installation failed');
  const wrappingUp = context({ state: 'finished' });
  wrappingUp._client = undefined;
  assert.equal(renderInstall.call(wrappingUp)[0], 'Installing');
  assert.equal(renderInstall.call(context({ state: 'finished' }, null, false))[0], 'Confirm Installation');
});

test('new connections retain the install menu, but returning from logs keeps success', () => {
  assert.equal(renderDashboard.call(context(undefined, null, false))[0], 'DashBridge A — iPhone');
  assert.equal(renderDashboard.call(context({ state: 'finished' }))[0], 'Board A installed');
  assert.equal(renderDashboard.call(context({ state: 'error' }))[0], 'DashBridge A — iPhone');
});

test('upstream dialogs without a custom success hook retain their original screen', () => {
  const dialog = context({ state: 'finished' });
  dialog.overrides = undefined;
  assert.equal(renderDashboard.call(dialog)[0], 'DashBridge A — iPhone');
});

test('dependency structure changes require review rather than silently losing the hook', () => {
  assert.throws(() => patchInstallDialog(original.replace('_renderInstall() {', '_renderInstallChanged() {')), /needs review/);
});


test('downloading logs saves the text without resetting the connected board', () => {
  const handler = patched.match(/@click=\$\{\(\) => \{\s*(textDownload[\s\S]*?)\}\}/)[1];
  let resets = 0;
  const downloads = [];
  const console = { logs: () => 'connection failed\n', reset: () => { resets++; } };
  new Function('textDownload', handler).call(
    { shadowRoot: { querySelector: () => console } },
    (text, filename) => downloads.push({ text, filename }),
  );
  assert.deepEqual(downloads, [{ text: 'connection failed\n', filename: 'esp-web-tools-logs.txt' }]);
  assert.equal(resets, 0);
  assert.ok(patched.includes('await this.shadowRoot.querySelector("ewt-console").reset();'));
});

test('upstream log-download changes require review', () => {
  assert.throws(() => patchInstallDialog(original.replace('textDownload(', 'otherDownload(')), /log-download hook needs review/);
});
