import { build } from 'esbuild';
import { prepareComparison, comparisonPage } from './comparison.mjs';
import { installDialogPatch } from './install-dialog-patch.mjs';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir, cp, rm, readdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = fileURLToPath(new URL('../', import.meta.url));
const digest = (data) => createHash('sha256').update(data).digest('hex');

export async function prepareFirmware(repository, output) {
  const manifest = JSON.parse(await readFile(path.join(repository, 'dist/manifest.json'), 'utf8'));
  if (manifest.target !== 'esp32' || manifest.flash_size !== '4MB') {
    throw new Error('Installer only supports original ESP32 with 4 MB flash');
  }
  await mkdir(path.join(output, 'firmware'), { recursive: true });
  const release = (await readFile(path.join(repository, 'firmware/version.txt'), 'utf8')).trim();
  if (manifest.version !== release) throw new Error('Manifest release differs from firmware/version.txt');
  const result = { version: release };
  for (const [role, label] of [['phone', 'A — iPhone'], ['car', 'B — Tesla']]) {
    const entry = manifest.images[role];
    if (!entry) throw new Error(`Missing firmware image for ${role}`);
    if (entry.file !== `${role}-merged.bin` || entry.flash_address !== '0x0') {
      throw new Error(`Unexpected firmware layout for ${role}`);
    }
    const data = await readFile(path.join(repository, 'dist', entry.file));
    if (digest(data) !== entry.sha256 || data.length !== entry.bytes) {
      throw new Error(`Firmware checksum or size mismatch: ${role}`);
    }
    // Merged images must contain the original ESP32 bootloader at 0x1000.
    if (data[0x1000] !== 0xe9 || data.readUInt16LE(0x100c) !== 0) {
      throw new Error(`Unexpected ESP32 bootloader: ${role}`);
    }
    for (const [source, expected] of Object.entries(entry.source_sha256)) {
      if (digest(await readFile(path.join(repository, source))) !== expected) {
        throw new Error(`Rebuild firmware before publishing: ${source}`);
      }
    }
    const sourceIdentity = JSON.stringify(Object.fromEntries(Object.entries(entry.source_sha256)
      .sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0)));
    const expectedVersion = `${release}+${digest(sourceIdentity).slice(0, 12)}`;
    if (entry.version !== expectedVersion) throw new Error(`Mismatched firmware version for ${role}`);
    if (data.length < 0x10050 || data[0x10000] !== 0xe9 || data.readUInt32LE(0x10020) !== 0xabcd5432) {
      throw new Error(`Missing ESP32 application descriptor for ${role}`);
    }
    const embeddedVersion = data.subarray(0x10030, 0x10050).toString('ascii').split('\0')[0];
    if (embeddedVersion !== entry.version) throw new Error(`Embedded firmware version mismatch for ${role}`);
    const name = `${role}-${entry.sha256.slice(0, 16)}`;
    await writeFile(path.join(output, 'firmware', `${name}.bin`), data);
    await writeFile(path.join(output, 'firmware', `${name}.json`), JSON.stringify({
      name: `DashBridge ${label}`,
      version: entry.version,
      new_install_prompt_erase: false,
      new_install_improv_wait_time: 0,
      builds: [{ chipFamily: 'ESP32', parts: [{ path: `${name}.bin`, offset: 0 }] }],
    }, null, 2) + '\n');
    result[role] = `./firmware/${name}.json`;
    result[`${role}Version`] = entry.version;
  }
  return result;
}

async function main() {
  const output = path.join(root, '_site');
  await rm(output, { recursive: true, force: true });
  await mkdir(output, { recursive: true });
  const firmware = await prepareFirmware(root, output);
  const bundle = await build({
    absWorkingDir: path.join(root, 'web'),
    plugins: [installDialogPatch],
    entryPoints: ['app.js'], bundle: true, splitting: true, format: 'esm',
    outdir: path.join(output, 'assets'), entryNames: '[name]-[hash]',
    chunkNames: '[name]-[hash]', minify: true, metafile: true, target: 'es2022',
    legalComments: 'linked',
  });
  const app = Object.entries(bundle.metafile.outputs).find(([, info]) => info.entryPoint === 'app.js')[0];
  let html = await readFile(path.join(root, 'web/index.html'), 'utf8');
  for (const [key, value] of Object.entries({ ...firmware, app: `./${path.relative(output, path.resolve(root, 'web', app))}` })) {
    html = html.replaceAll(`{{${key}}}`, value);
  }
  const pageTemplate = await readFile(path.join(root, 'web/index.html'), 'utf8');
  const css = await readFile(path.join(root, 'web/style.css'));
  const cssName = `style-${digest(css).slice(0, 16)}.css`;
  await writeFile(path.join(output, 'assets', cssName), css);
  html = html.replaceAll('{{css}}', `./assets/${cssName}`);
  if (/\{\{.+?\}\}/.test(html)) throw new Error('Unresolved HTML placeholder');
  await writeFile(path.join(output, 'index.html'), html);
  await cp(path.join(root, 'web/favicon.svg'), path.join(output, 'favicon.svg'));
  await cp(path.join(root, 'dist/manifest.json'), path.join(output, 'firmware/checksums.json'));
  await writeFile(path.join(output, '.nojekyll'), '');
  // Retire the failed message-only experiment, including bookmarks to its URL.
  await mkdir(path.join(output, 'message-only'), { recursive: true });
  await writeFile(path.join(output, 'message-only/index.html'), `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>DashBridge installer</title><meta http-equiv="refresh" content="0;url=../">
<link rel="canonical" href="https://thomasgregg.github.io/DashBridge/"></head>
<body><p>The message-only experiment has been replaced by the two-board installer.</p>
<p><a href="../">Open the DashBridge installer</a></p></body></html>\n`);
  // Distribute notices alongside the bundled code.
  const notices = [];
  for (const file of ['LICENSE', 'third_party/README.md', 'third_party/ESP-IDF-LICENSE', 'third_party/ESP32-controller-LICENSE']) {
    notices.push(`\n=== DashBridge firmware / ${file} ===\n${await readFile(path.join(root, file), 'utf8')}`);
  }
  const lock = JSON.parse(await readFile(path.join(root, 'web/package-lock.json'), 'utf8'));
  for (const [name, pkg] of Object.entries(lock.packages)) {
    if (!name || pkg.dev) continue;
    const dir = path.join(root, 'web', name);
    const files = await readdir(dir);
    for (const file of files.filter((f) => /^(license|notice|copyright)/i.test(f))) {
      notices.push(`\n=== ${name} / ${file} ===\n${await readFile(path.join(dir, file), 'utf8')}`);
    }
  }
  await writeFile(path.join(output, 'third-party-licenses.txt'), notices.join('\n'));
  const comparison = await prepareComparison(root, output);
  let comparisonHtml = comparisonPage(pageTemplate, comparison);
  comparisonHtml = comparisonHtml.replaceAll('{{app}}', `../${path.relative(output, path.resolve(root, 'web', app))}`)
    .replaceAll('{{css}}', `../assets/${cssName}`);
  if (/\{\{.+?\}\}/.test(comparisonHtml)) throw new Error('Unresolved comparison placeholder');
  await writeFile(path.join(output, 'idf-5.5.5/index.html'), comparisonHtml);
  await cp(path.join(root, 'web/favicon.svg'), path.join(output, 'idf-5.5.5/favicon.svg'));
  console.log(`Built DashBridge installer ${firmware.version}; both firmware images verified.`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) await main();
