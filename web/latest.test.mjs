import test from 'node:test';
import assert from 'node:assert/strict';
import { applyLatest, refreshLatest } from './latest.js';

const payload = {
  release: '0.3.2-alpha',
  phone: { manifest: './firmware/phone-aaaaaaaaaaaaaaaa.json', version: '0.3.2-alpha+aaaaaaaaaaaa' },
  car: { manifest: './firmware/car-bbbbbbbbbbbbbbbb.json', version: '0.3.2-alpha+bbbbbbbbbbbb' },
};
const choices = () => ['phone', 'car'].map(value => ({ value, dataset: {} }));

test('runtime latest pointer replaces embedded A/B versions and manifests', async () => {
  const inputs = choices();
  let requested;
  await refreshLatest(inputs, async (url, options) => {
    requested = { url, options };
    return { ok: true, json: async () => payload };
  }, 'https://example.test/DashBridge/');
  assert.equal(requested.url.pathname, '/DashBridge/latest.json');
  assert.match(requested.url.search, /^\?cache=\d+$/);
  assert.deepEqual(requested.options, { cache: 'no-store' });
  assert.equal(inputs[0].dataset.version, payload.phone.version);
  assert.equal(inputs[1].dataset.manifest, payload.car.manifest);
});

test('runtime pointer rejects missing, cross-path, or malformed firmware entries', () => {
  for (const latest of [
    null,
    { phone: payload.phone },
    { ...payload, car: { ...payload.car, manifest: 'https://example.test/other.json' } },
    { ...payload, phone: { ...payload.phone, version: 'latest' } },
  ]) assert.throws(() => applyLatest(choices(), latest));
});
