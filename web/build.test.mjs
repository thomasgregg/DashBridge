import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, writeFile, cp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { prepareFirmware } from './build.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
async function fixture(t) {
  const directory = await mkdtemp(path.join(tmpdir(), 'dashbridge-web-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  for (const folder of ['dist', 'firmware']) await cp(path.join(root, folder), path.join(directory, folder), { recursive: true });
  return { directory, output: path.join(directory, 'site') };
}

test('both installer manifests select their own verified ESP32 image at offset zero', async (t) => {
  const { directory, output } = await fixture(t);
  const config = await prepareFirmware(directory, output);
  for (const [role, label] of [['phone', 'A — iPhone'], ['car', 'B — Tesla']]) {
    const manifest = JSON.parse(await readFile(path.join(output, config[role]), 'utf8'));
    assert.equal(manifest.name, `DashBridge ${label}`);
    assert.equal(manifest.new_install_improv_wait_time, 0);
    assert.deepEqual(manifest.builds.map(b => b.chipFamily), ['ESP32']);
    assert.equal(manifest.builds[0].parts.length, 1);
    const part = manifest.builds[0].parts[0];
    assert.equal(part.offset, 0);
    assert.deepEqual(await readFile(path.join(output, 'firmware', part.path)), await readFile(path.join(directory, 'dist', `${role}-merged.bin`)));
  }
  assert.notEqual(config.phone, config.car);
});

test('a corrupt image cannot be published', async (t) => {
  const { directory, output } = await fixture(t);
  await writeFile(path.join(directory, 'dist/phone-merged.bin'), 'corrupted');
  await assert.rejects(prepareFirmware(directory, output), /checksum or size mismatch/);
});

test('stale firmware cannot be published with changed source', async (t) => {
  const { directory, output } = await fixture(t);
  await writeFile(path.join(directory, 'firmware/main/main.cpp'), '// changed source');
  await assert.rejects(prepareFirmware(directory, output), /Rebuild firmware before publishing/);
});

test('an unsupported target or nonzero flash offset is rejected', async (t) => {
  const { directory, output } = await fixture(t);
  const filename = path.join(directory, 'dist/manifest.json');
  const manifest = JSON.parse(await readFile(filename));
  manifest.target = 'esp32s3';
  await writeFile(filename, JSON.stringify(manifest));
  await assert.rejects(prepareFirmware(directory, output), /only supports original ESP32/);
  manifest.target = 'esp32';
  manifest.images.phone.flash_address = '0x1000';
  await writeFile(filename, JSON.stringify(manifest));
  await assert.rejects(prepareFirmware(directory, output), /Unexpected firmware layout/);
});

test('mixed-version or missing board images cannot be published', async (t) => {
  const { directory, output } = await fixture(t);
  const filename = path.join(directory, 'dist/manifest.json');
  const manifest = JSON.parse(await readFile(filename));
  manifest.images.car.version = 'older-build';
  await writeFile(filename, JSON.stringify(manifest));
  await assert.rejects(prepareFirmware(directory, output), /mismatched firmware version for car/);
  delete manifest.images.car;
  await writeFile(filename, JSON.stringify(manifest));
  await assert.rejects(prepareFirmware(directory, output), /Missing or mismatched firmware version for car/);
});
