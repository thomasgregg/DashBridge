import test from 'node:test';
import assert from 'node:assert/strict';
import { currentStatus, evaluateChecks, statusMaxAgeMs } from './setup-status.mjs';

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
