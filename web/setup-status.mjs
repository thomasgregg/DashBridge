export const statusMaxAgeMs = 10000;
export const discoveryNoticeMaxAgeMs = 60000;
const heartbeatExpiryMs = { phone: 5000, car: 3000 };
const boardNames = { phone: 'Board A', car: 'Board B', single: 'DashBridge' };

export function disconnectMessage(disconnectedRole, activeRoles) {
  const connected = [...new Set(activeRoles)].filter(role => boardNames[role]);
  if (!connected.length)
    return 'Neither board is connected to this page. Connect a board to continue.';
  const names = connected.map(role => boardNames[role]).join(' and ');
  return `${boardNames[disconnectedRole] || 'A board'} disconnected. ${names} ` +
    `${connected.length === 1 ? 'is' : 'are'} still connected to this page.`;
}

export function discoveryNoticeEnded(notice, connection, at = Date.now()) {
  if (!notice) return false;
  return at - notice.startedAt >= discoveryNoticeMaxAgeMs ||
    (connection?.appsAt > notice.startedAt && connection.apps?.discovering === false);
}

export function currentStatus(connection, at = Date.now()) {
  if (!connection || connection.closed || !connection.status ||
      !Number.isFinite(connection.statusAt) || connection.statusAt > at ||
      at - connection.statusAt > statusMaxAgeMs) return null;
  return connection.status;
}

export function evaluateChecks(connections, usbLostAt = {}, at = Date.now()) {
  const aConnection = connections.find(item => item.role === 'phone' || item.role === 'single');
  const bConnection = connections.find(item => item.role === 'car');
  const a = currentStatus(aConnection, at);
  const b = currentStatus(bConnection, at);
  const aAfterLoss = !usbLostAt.car || aConnection?.statusAt > usbLostAt.car + heartbeatExpiryMs.car;
  const bAfterLoss = !usbLostAt.phone || bConnection?.statusAt > usbLostAt.phone + heartbeatExpiryMs.phone;
  const phone = a || (b?.boardLink && bAfterLoss ? b : null);
  const car = b || (a?.boardLink && aAfterLoss ? a : null);
  const link = a && b ? a.boardLink && b.boardLink :
    a ? (aAfterLoss ? a.boardLink : null) :
    b ? (bAfterLoss ? b.boardLink : null) : null;
  return {
    boardLink: link,
    phoneBluetooth: phone?.phoneBluetooth ?? null,
    notificationSharing: phone?.phoneBluetooth ? phone.phoneNotifications : null,
    teslaTransport: car?.carTransport ?? null,
    messagesReady: car?.carTransport ? car.carMessages : null,
    callsReady: link ? phone?.phoneCalls && car?.carCalls : null,
    carSync: car?.carSync ?? null,
  };
}
