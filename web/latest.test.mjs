import test from 'node:test';
import assert from 'node:assert/strict';
import { applyLatest, refreshLatest } from './latest.js';

const payload = {
  release: '0.5.3-alpha',
  releaseNotes: 'https://github.com/thomasgregg/DashBridge/blob/main/docs/FIRMWARE_RELEASE.md',
  phone: { manifest: './firmware/phone-aaaaaaaaaaaaaaaa.json', version: '0.5.3-alpha+aaaaaaaaaaaa' },
  car: { manifest: './firmware/car-bbbbbbbbbbbbbbbb.json', version: '0.5.3-alpha+bbbbbbbbbbbb' },
};
const choices = () => ['phone', 'car'].map(value => ({ value, dataset: {} }));

test('runtime latest pointer replaces embedded A/B versions and manifests', async () => {
  const inputs = choices();
  const links = [{ href: '' }, { href: '' }];
  let requested;
  await refreshLatest(inputs, links, async (url, options) => {
    requested = { url, options };
    return { ok: true, json: async () => payload };
  }, 'https://example.test/DashBridge/');
  assert.equal(requested.url.pathname, '/DashBridge/latest.json');
  assert.match(requested.url.search, /^\?cache=\d+$/);
  assert.deepEqual(requested.options, { cache: 'no-store' });
  assert.equal(inputs[0].dataset.release, payload.release);
  assert.equal(inputs[0].dataset.version, payload.phone.version);
  assert.equal(inputs[1].dataset.manifest, payload.car.manifest);
  assert.equal(links[0].href, payload.releaseNotes);
  assert.equal(links[1].href, payload.releaseNotes);
});

test('runtime pointer rejects missing, cross-path, or malformed firmware entries', () => {
  for (const latest of [
    null,
    { phone: payload.phone },
    { ...payload, releaseNotes: 'https://example.test/release-notes' },
    { ...payload, releaseNotes: 'https://github.com/thomasgregg/DashBridge/blob/main/docs/OTHER.md' },
    { ...payload, car: { ...payload.car, manifest: 'https://example.test/other.json' } },
    { ...payload, phone: { ...payload.phone, version: 'latest' } },
  ]) assert.throws(() => applyLatest(choices(), latest));
});
