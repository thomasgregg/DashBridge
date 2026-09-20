import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';

const digest = data => createHash('sha256').update(data).digest('hex');

// Frozen experimental release: sources belong to source_commit, not main.
// The import step verifies every source hash against that exact checkout.
export async function prepareComparison(repository, output) {
  const source = path.join(repository, 'dist/idf555');
  const manifest = JSON.parse(await readFile(path.join(source, 'manifest.json'), 'utf8'));
  const entry = manifest.images?.car;
  if (manifest.target !== 'esp32' || manifest.flash_size !== '4MB' ||
      manifest.esp_idf !== 'v5.5.5' ||
      manifest.esp_idf_commit !== 'b774170ff46c393eeb5e495ea37936038d3f4f4f' ||
      !/^[a-f0-9]{40}$/.test(manifest.source_commit) ||
      !/^https:\/\/github\.com\/thomasgregg\/DashBridge\/actions\/runs\/\d+$/.test(manifest.build_url) ||
      Object.keys(manifest.images).join() !== 'car' ||
      entry.file !== 'car-merged.bin' || entry.flash_address !== '0x0' ||
      entry.esp_idf !== manifest.esp_idf || entry.esp_idf_commit !== manifest.esp_idf_commit) {
    throw new Error('Invalid Board B comparison metadata');
  }
  const data = await readFile(path.join(source, entry.file));
  if (digest(data) !== entry.sha256 || data.length !== entry.bytes) {
    throw new Error('Comparison firmware checksum or size mismatch');
  }
  const identity = JSON.stringify(Object.fromEntries(Object.entries(entry.source_sha256)
    .sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)));
  const version = `${manifest.version}+${digest(identity).slice(0, 12)}`;
  if (entry.version !== version || data.length < 0x10100 ||
      data[0x1000] !== 0xe9 || data.readUInt16LE(0x100c) !== 0 ||
      data[0x10000] !== 0xe9 || data.readUInt32LE(0x10020) !== 0xabcd5432 ||
      data.subarray(0x10030, 0x10050).toString('ascii').split('\0')[0] !== version ||
      data.subarray(0x10090, 0x100b0).toString('ascii').split('\0')[0] !== 'v5.5.5') {
    throw new Error('Comparison firmware descriptor or version mismatch');
  }
  const destination = path.join(output, 'idf-5.5.5/firmware');
  await mkdir(destination, { recursive: true });
  const name = `car-${entry.sha256.slice(0, 16)}`;
  await writeFile(path.join(destination, `${name}.bin`), data);
  await writeFile(path.join(destination, `${name}.json`), JSON.stringify({
    name: 'DashBridge B — Tesla · SDK 5.5.5 test', version,
    new_install_prompt_erase: false, new_install_improv_wait_time: 0,
    builds: [{ chipFamily: 'ESP32', parts: [{ path: `${name}.bin`, offset: 0 }] }],
  }, null, 2) + '\n');
  await writeFile(path.join(destination, 'checksums.json'), JSON.stringify(manifest, null, 2) + '\n');
  return { car: `./firmware/${name}.json`, carVersion: version };
}

export function comparisonPage(html, firmware) {
  return html
    .replace('<title>Install DashBridge</title>', '<title>DashBridge B — Bluetooth update test</title>')
    .replace('USB INSTALLER', 'BLUETOOTH UPDATE TEST')
    .replaceAll('class="board-marker is-selected" data-board="phone"', 'class="board-marker" data-board="phone"')
    .replaceAll('class="board-marker" data-board="car"', 'class="board-marker is-selected" data-board="car"')
    .replace('<h2>Choose your board</h2>', '<h2>Board B — Tesla</h2>')
    .replace('<fieldset>', '<p>ESP-IDF 5.5.5. Automatic reconnect still needs testing in the car.</p><fieldset hidden>')
    .replace(/\s*<label><input[^>]*value="phone"[^>]*>.*?<\/label>/, '')
    .replace('value="car"', 'value="car" checked data-other-installer="../"')
    .replaceAll('{{phoneVersion}}', firmware.carVersion)
    .replaceAll('{{carVersion}}', firmware.carVersion)
    .replaceAll('{{car}}', firmware.car)
    .replace('Receives iPhone calls and notifications.', 'Sends calls and messages to your Tesla.')
    .replace('<div class="next"><p>After installation</p>', '<div class="next"><p>Previous version</p><a href="../">Return to the current installer</a></div><div class="next"><p>After installation</p>')
    .replace('href="./third-party-licenses.txt"', 'href="../third-party-licenses.txt"');
}
