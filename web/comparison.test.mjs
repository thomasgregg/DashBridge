import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, cp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { prepareComparison, comparisonPage } from './comparison.mjs';
import { runInNewContext } from 'node:vm';

const root = fileURLToPath(new URL('../', import.meta.url));
async function fixture(t) {
  const directory = await mkdtemp(path.join(tmpdir(), 'dashbridge-comparison-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  await cp(path.join(root, 'dist/idf555'), path.join(directory, 'dist/idf555'), { recursive: true });
  return { directory, output: path.join(directory, 'site') };
}

test('comparison installs only the verified B release, with the same version in page and dialog', async t => {
  const { directory, output } = await fixture(t);
  const config = await prepareComparison(directory, output);
  const manifest = JSON.parse(await readFile(path.join(output, 'idf-5.5.5', config.car)));
  assert.equal(manifest.version, config.carVersion);
  assert.equal(manifest.builds[0].parts[0].offset, 0);
  const page = comparisonPage(await readFile(path.join(root, 'web/index.html'), 'utf8'), config);
  assert.equal((page.match(/name="board"/g) || []).length, 1);
  assert.match(page, /value="car" checked/);
  assert.match(page, /data-other-installer="\.\.\/"/);
  assert.ok(page.includes(config.carVersion));
  assert.ok(page.includes(config.car));
  assert.match(page, /Return to the current installer/);
  assert.doesNotMatch(page, /\{\{(?:phone|car)(?:Version)?\}\}/);
});

test('comparison rejects damaged firmware and dishonest embedded SDK versions', async t => {
  const { directory, output } = await fixture(t);
  const binPath = path.join(directory, 'dist/idf555/car-merged.bin');
  const manifestPath = path.join(directory, 'dist/idf555/manifest.json');
  const data = await readFile(binPath);
  data.fill(0, 0x10090, 0x100b0);
  data.write('v5.5.1', 0x10090);
  await writeFile(binPath, data);
  await assert.rejects(prepareComparison(directory, output), /checksum/);
  const manifest = JSON.parse(await readFile(manifestPath));
  manifest.images.car.sha256 = createHash('sha256').update(data).digest('hex');
  await writeFile(manifestPath, JSON.stringify(manifest));
  await assert.rejects(prepareComparison(directory, output), /descriptor or version mismatch/);
});

test('comparison rejects wrong source-derived version, SDK metadata, or role', async t => {
  const { directory, output } = await fixture(t);
  const file = path.join(directory, 'dist/idf555/manifest.json');
  const original = JSON.parse(await readFile(file));
  for (const change of [
    m => { m.images.car.version = '0.3.2-alpha+000000000000'; },
    m => { m.esp_idf = 'v5.5.1'; },
    m => { m.images.phone = m.images.car; },
  ]) {
    const manifest = structuredClone(original); change(manifest);
    await writeFile(file, JSON.stringify(manifest));
    await assert.rejects(prepareComparison(directory, output));
  }
});

test('after B comparison installation, choosing A returns to the normal installer', async () => {
  const element = () => ({
    classList: { add() {} }, setAttribute() {}, append() {}, replaceChildren() {},
  });
  const choice = { value: 'car', checked: true,
    dataset: { manifest: './firmware/test.json', version: '0.3.2-alpha+123456789abc', otherInstaller: '../' },
    addEventListener() {},
  };
  let selectOther, destination;
  const source = (await readFile(new URL('./app.js', import.meta.url), 'utf8'))
    .replace("import { successRenderer } from './install-success.js';", '');
  runInNewContext(source, {
    document: { querySelector: element, querySelectorAll: key => key === 'input[name="board"]' ? [choice] : [], createElement: element },
    window: { isSecureContext: false, location: { assign: url => { destination = url; } } },
    navigator: {}, console,
    successRenderer: (role, callback) => { selectOther = callback; },
  });
  selectOther('phone');
  assert.equal(destination, '../');
});
