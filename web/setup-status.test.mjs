import test from 'node:test';
import assert from 'node:assert/strict';
import { currentStatus, disconnectMessage, discoveryNoticeEnded, discoveryNoticeMaxAgeMs,
  evaluateChecks, statusMaxAgeMs } from './setup-status.mjs';

const at = 100000;
function phone(statusAt = at, changes = {}) {
  return { role: 'phone', statusAt, status: {
    role: 'phone', boardLink: true, phoneBluetooth: true, phoneNotifications: true,
    carTransport: true, carMessages: true, carSync: false, phoneCalls: true, carCalls: true,
    ...changes,
  } };
}
function car(statusAt = at, changes = {}) {
  return { role: 'car', statusAt, status: {
    role: 'car', boardLink: true, phoneBluetooth: true, phoneNotifications: true,
    carTransport: false, carMessages: false, carSync: false, phoneCalls: false, carCalls: false,
    ...changes,
  } };
}

test('direct Board B status wins over the Board A proxy', () => {
  const checks = evaluateChecks([phone(), car()], {}, at);
  assert.equal(checks.boardLink, true);
  assert.equal(checks.notificationSharing, true);
  assert.equal(checks.teslaTransport, false);
  assert.equal(checks.messagesReady, null);
  assert.equal(checks.callsReady, false);
});

test('unplugging B immediately hides old green proxy checks', () => {
  const checks = evaluateChecks([phone()], { car: at - 1000 }, at);
  assert.equal(checks.boardLink, null);
  assert.equal(checks.phoneBluetooth, true);
  assert.equal(checks.notificationSharing, true);
  assert.equal(checks.teslaTransport, null);
  assert.equal(checks.messagesReady, null);
  assert.equal(checks.callsReady, null);
});

test('after B heartbeat expiry a fresh A report shows the lost link', () => {
  const later = at + 4000;
  const checks = evaluateChecks([phone(later, { boardLink: false, carTransport: false,
    carMessages: false, carCalls: false })], { car: at }, later);
  assert.equal(checks.boardLink, false);
  assert.equal(checks.teslaTransport, null);
  assert.equal(checks.callsReady, null);
});

test('one USB data connection can still verify a powered remote board', () => {
  const later = at + 4000;
  const checks = evaluateChecks([phone(later)], { car: at }, later);
  assert.equal(checks.boardLink, true);
  assert.equal(checks.teslaTransport, true);
  assert.equal(checks.messagesReady, true);
  assert.equal(checks.callsReady, true);
});

test('silent USB connections expire rather than staying green', () => {
  const connection = phone();
  assert.equal(currentStatus(connection, at + statusMaxAgeMs)?.role, 'phone');
  assert.equal(currentStatus(connection, at + statusMaxAgeMs + 1), null);
  const checks = evaluateChecks([connection], {}, at + statusMaxAgeMs + 1);
  assert.equal(checks.boardLink, null);
  assert.equal(checks.phoneBluetooth, null);
  assert.equal(checks.notificationSharing, null);
});

test('A USB loss also suppresses a B proxy until A heartbeat expiry', () => {
  assert.equal(evaluateChecks([car()], { phone: at - 1000 }, at).phoneBluetooth, null);
  const later = at + 6000;
  const checks = evaluateChecks([car(later, { boardLink: false })], { phone: at }, later);
  assert.equal(checks.boardLink, false);
  assert.equal(checks.phoneBluetooth, null);
});

test('discovery message ends when a fresh board report says the window closed', () => {
  const notice = { startedAt: at };
  assert.equal(discoveryNoticeEnded(notice, { appsAt: at - 1,
    apps: { discovering: false } }, at + 1000), false);
  assert.equal(discoveryNoticeEnded(notice, { appsAt: at + 4000,
    apps: { discovering: true } }, at + 4000), false);
  assert.equal(discoveryNoticeEnded(notice, { appsAt: at + 8000,
    apps: { discovering: false } }, at + 8000), true);
});

test('discovery message expires after one minute if board reports stop', () => {
  const notice = { startedAt: at };
  assert.equal(discoveryNoticeEnded(notice, null, at + discoveryNoticeMaxAgeMs - 1), false);
  assert.equal(discoveryNoticeEnded(notice, null, at + discoveryNoticeMaxAgeMs), true);
  assert.equal(discoveryNoticeEnded(null, null, at + discoveryNoticeMaxAgeMs), false);
});

test('disconnect notice describes both boards after the second USB connection closes', () => {
  assert.equal(disconnectMessage('car', ['phone']),
    'Board B disconnected. Board A is still connected to this page.');
  assert.equal(disconnectMessage('phone', []),
    'Neither board is connected to this page. Connect a board to continue.');
});

test('disconnect notice works in the opposite order and with one board', () => {
  assert.equal(disconnectMessage('phone', ['car']),
    'Board A disconnected. Board B is still connected to this page.');
  assert.equal(disconnectMessage('car', []),
    'Neither board is connected to this page. Connect a board to continue.');
});
