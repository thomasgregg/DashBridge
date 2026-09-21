import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { runInNewContext } from 'node:vm';

test('board selection updates the full version and preserves an already-open installer identity', async () => {
  const element = () => ({
    attributes: {}, classList: { add() {} },
    setAttribute(key, value) { this.attributes[key] = value; },
    append() {}, replaceChildren(child) { this.child = child; },
  });
  const choices = ['phone', 'car'].map((value, i) => ({
    value, checked: !i,
    dataset: { manifest: `./${value}.json`, version: `0.3.1-alpha+${i ? 'bbbbbbbbbbbb' : 'aaaaaaaaaaaa'}` },
    addEventListener(event, callback) { this.change = callback; },
  }));
  const nodes = Object.fromEntries(['#browser-status', '#install-control', '#board-description',
    '#firmware-version', '.bridge-diagram'].map(key => [key, element()]));
  const source = (await readFile(new URL('./app.js', import.meta.url), 'utf8'))
    .replace("import { successRenderer } from './install-success.js';", '')
    .replace("import { refreshLatest } from './latest.js';", '');
  runInNewContext(source, {
    document: {
      querySelector: key => nodes[key],
      querySelectorAll: key => key === 'input[name="board"]' ? choices : [],
      createElement: element,
    },
    window: { isSecureContext: false }, navigator: {}, console,
    successRenderer: (role, selectOther, version) => ({ role, version }), refreshLatest() {},
  });
  const originalInstaller = nodes['#install-control'].child;
  assert.equal(nodes['#firmware-version'].textContent, `v${choices[0].dataset.version}`);
  choices[0].checked = false; choices[1].checked = true; choices[1].change();
  const selectedInstaller = nodes['#install-control'].child;
  assert.equal(nodes['#firmware-version'].textContent, `v${choices[1].dataset.version}`);
  assert.equal(selectedInstaller.attributes.manifest, choices[1].dataset.manifest);
  assert.equal(selectedInstaller.overrides.renderInstallSuccess.version, choices[1].dataset.version);
  assert.equal(originalInstaller.attributes.manifest, choices[0].dataset.manifest);
  assert.equal(originalInstaller.overrides.renderInstallSuccess.version, choices[0].dataset.version);
});
