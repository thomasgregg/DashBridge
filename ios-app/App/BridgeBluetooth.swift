import CoreBluetooth
import Foundation

// Kept in step with firmware/main/setup_ble.cpp. This is a setup/control channel;
// notification text and call audio never pass through the app.
enum BridgeService {
    static let service = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F0")
    static let status = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F1")
    static let policy = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F2")
    static let command = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F3")
}

struct BridgeStatus: Equatable {
    let phoneBluetooth: Bool
    let notifications: Bool
    let phoneCalls: Bool
    let internalLink: Bool
    let teslaMessages: Bool
    let teslaTransport: Bool
    let teslaSync: Bool
    let teslaCalls: Bool
    let phonePairingOpen: Bool

    init(_ data: Data) throws {
        guard data.count >= 3, data[0] == 1 else { throw BridgeError.unsupportedStatus }
        let bits = UInt16(data[1]) | (UInt16(data[2]) << 8)
        self.init(bits: bits)
    }

    init(bits: UInt16) {
        func has(_ bit: Int) -> Bool { (bits & (1 << bit)) != 0 }
        phoneBluetooth = has(0)
        notifications = has(1)
        phoneCalls = has(2)
        internalLink = has(3)
        teslaMessages = has(4)
        teslaTransport = has(5)
        teslaSync = has(6)
        teslaCalls = has(7)
        phonePairingOpen = has(8)
    }
}

enum BridgeError: LocalizedError {
    case unsupportedStatus
    case commandUnavailable
    case commandTooLong

    var errorDescription: String? {
        switch self {
        case .unsupportedStatus: "This DashBridge needs newer firmware."
        case .commandUnavailable: "DashBridge is still connecting. Try again in a moment."
        case .commandTooLong: "This app name is too long for DashBridge."
        }
    }
}

final class BridgeBluetooth: NSObject, ObservableObject {
    @Published private(set) var bluetoothReady = false
    @Published private(set) var scanning = false
    @Published private(set) var foundName: String?
    @Published private(set) var connected = false
    @Published private(set) var status: BridgeStatus?
    @Published private(set) var allowedIDs: Set<String> = []
    @Published private(set) var policyLoaded = false
    @Published private(set) var deviceID: UUID?
    @Published private(set) var connectionStage = "Looking nearby"
    @Published private(set) var notificationPermission: Bool?
    @Published private(set) var commandError: String?
    @Published var error: String?

    private var manager: CBCentralManager?
    private var discovered: CBPeripheral?
    private var peripheral: CBPeripheral?
    private var statusCharacteristic: CBCharacteristic?
    private var policyCharacteristic: CBCharacteristic?
    private var commandCharacteristic: CBCharacteristic?
    private var refreshTimer: Timer?
    private var connectionTimer: Timer?
    private var scanTimer: Timer?
    private var timedOut = false
    private var pendingCommands: [Data] = []
    private var writing = false
    private var lastPolicyRead = Date.distantPast
    private let rememberedKey = "dashbridge.peripheral"
    private var incompatibleIdentifiers: Set<UUID> = []

    func start() {
        if manager == nil {
            manager = CBCentralManager(delegate: self, queue: .main)
        } else if manager?.state == .poweredOn {
            findExistingOrScan()
        }
    }

    private func findExistingOrScan(clearError: Bool = true, tryRemembered: Bool = true) {
        guard let manager, manager.state == .poweredOn, !connected else { return }
        if clearError { error = nil }
        // A paired ANCS accessory can remain connected to iOS while it is no
        // longer advertising. A scan cannot find it, but Core Bluetooth can
        // attach this app to the existing system connection.
        let connectedPeripherals = manager.retrieveConnectedPeripherals(
            withServices: [BridgeService.service, CBUUID(string: "1800")])
        if let match = connectedPeripherals.first(where: { candidate in
            guard let name = candidate.name else { return false }
            return ["DashBridge A", "Dash Messages", "DashBridge"].contains {
                name.caseInsensitiveCompare($0) == .orderedSame
            }
        }) {
            discovered = match
            foundName = match.name ?? "DashBridge"
            connectFound(requiresANCS: false)
            return
        }
        if tryRemembered,
           let rawID = UserDefaults.standard.string(forKey: rememberedKey),
           let id = UUID(uuidString: rawID),
           let saved = manager.retrievePeripherals(withIdentifiers: [id]).first {
            discovered = saved
            foundName = saved.name ?? "DashBridge"
            connectFound(requiresANCS: false)
            return
        }
        scan(clearError: false)
    }

    func scan(clearError: Bool = true) {
        guard let manager, manager.state == .poweredOn, !connected else { return }
        if clearError { error = nil }
        scanTimer?.invalidate()
        foundName = nil
        discovered = nil
        scanning = true
        connectionStage = "Looking nearby"
        // ANCS service solicitation occupies the advertising packet today.
        // We scan in the foreground, match its local name, and verify our GATT
        // service after connecting. No background scan is claimed.
        manager.scanForPeripherals(withServices: nil)
        scanTimer = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self] _ in
            guard let self, self.scanning else { return }
            self.manager?.stopScan()
            self.scanning = false
            self.error = "The app can't find DashBridge's setup connection. Check that it's powered, then try again."
        }
    }

    func connectFound(requiresANCS: Bool = true) {
        guard let discovered, let manager else { return }
        scanTimer?.invalidate()
        scanTimer = nil
        scanning = false
        manager.stopScan()
        peripheral = discovered
        connectionStage = "Opening Bluetooth link"
        notificationPermission = nil
        discovered.delegate = self
        // Ask iOS to offer ANCS notification permission during pairing in the
        // app, rather than depending on a device-settings button that may not
        // exist for this BLE accessory.
        manager.connect(discovered, options: requiresANCS
                        ? [CBConnectPeripheralOptionRequiresANCS: true] : nil)
        connectionTimer?.invalidate()
        connectionTimer = Timer.scheduledTimer(withTimeInterval: 20, repeats: false) { [weak self] _ in
            guard let self, self.status == nil else { return }
            self.timedOut = true
            self.error = self.connected
                ? "Your iPhone connected to DashBridge, but the app couldn't check it yet."
                : "DashBridge may be connected in iPhone Bluetooth settings, but the app couldn't reach it yet."
            if self.status == nil, let id = self.peripheral?.identifier,
               UserDefaults.standard.string(forKey: self.rememberedKey) == id.uuidString {
                UserDefaults.standard.removeObject(forKey: self.rememberedKey)
            }
            if let peripheral = self.peripheral {
                self.manager?.cancelPeripheralConnection(peripheral)
            }
            self.clearConnection()
        }
    }

    func retry() {
        error = nil
        timedOut = false
        if let peripheral, peripheral.state != .disconnected {
            manager?.cancelPeripheralConnection(peripheral)
        }
        clearConnection()
        findExistingOrScan(tryRemembered: false)
    }

    func send(_ operation: UInt8, appID: String? = nil) {
        var payload = Data([operation])
        if let appID { payload.append(contentsOf: appID.utf8) }
        guard let peripheral, commandCharacteristic != nil else {
            commandError = BridgeError.commandUnavailable.localizedDescription
            return
        }
        guard payload.count <= peripheral.maximumWriteValueLength(for: .withResponse) else {
            commandError = BridgeError.commandTooLong.localizedDescription
            return
        }
        pendingCommands.append(payload)
        sendNext()
    }

    func setAllowed(_ id: String, allowed: Bool) {
        guard !id.isEmpty else { return }
        guard connected, commandCharacteristic != nil, policyLoaded else {
            commandError = BridgeError.commandUnavailable.localizedDescription
            return
        }
        if allowed { allowedIDs.insert(id) } else { allowedIDs.remove(id) }
        send(allowed ? 1 : 2, appID: id)
    }

    private func sendNext() {
        guard !writing, !pendingCommands.isEmpty,
              let peripheral, let commandCharacteristic else { return }
        writing = true
        peripheral.writeValue(pendingCommands[0], for: commandCharacteristic, type: .withResponse)
    }

    private func refresh() {
        guard let peripheral, let statusCharacteristic else { return }
        peripheral.readValue(for: statusCharacteristic)
        // Do not ask for an encrypted app-policy read during initial setup.
        // The user must first pair Dash Messages for notification sharing;
        // otherwise this read can start a competing security exchange.
        if let policyCharacteristic, status?.notifications == true,
           Date().timeIntervalSince(lastPolicyRead) > 15 {
            lastPolicyRead = Date()
            peripheral.readValue(for: policyCharacteristic)
        }
    }

    private func clearConnection() {
        connectionTimer?.invalidate()
        connectionTimer = nil
        refreshTimer?.invalidate()
        refreshTimer = nil
        scanTimer?.invalidate()
        scanTimer = nil
        connected = false
        status = nil
        statusCharacteristic = nil
        policyCharacteristic = nil
        commandCharacteristic = nil
        policyLoaded = false
        pendingCommands.removeAll()
        writing = false
        commandError = nil
        peripheral = nil
    }
}

extension BridgeBluetooth: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothReady = central.state == .poweredOn
        if bluetoothReady {
            findExistingOrScan()
        }
        else {
            scanning = false
            clearConnection()
            if central.state == .unauthorized {
                error = "Allow Bluetooth for DashBridge in iPhone Settings."
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        guard ["DashBridge A", "Dash Messages", "DashBridge"].contains(where: {
            name.caseInsensitiveCompare($0) == .orderedSame
        }), !incompatibleIdentifiers.contains(peripheral.identifier) else { return }
        discovered = peripheral
        foundName = name
        scanTimer?.invalidate()
        scanTimer = nil
        scanning = false
        central.stopScan()
        if UserDefaults.standard.string(forKey: rememberedKey) == peripheral.identifier.uuidString {
            connectFound()
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connected = true
        notificationPermission = peripheral.ancsAuthorized ? true : nil
        connectionStage = "Finding setup service"
        error = nil
        peripheral.delegate = self
        peripheral.discoverServices([BridgeService.service])
    }

    func centralManager(_ central: CBCentralManager,
                        didUpdateANCSAuthorizationFor peripheral: CBPeripheral) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        notificationPermission = peripheral.ancsAuthorized
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        self.error = error?.localizedDescription ?? "Could not connect to DashBridge."
        clearConnection()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        if let error { self.error = error.localizedDescription }
        clearConnection()
        if !timedOut && !incompatibleIdentifiers.contains(peripheral.identifier) {
            findExistingOrScan(tryRemembered: false)
        }
        timedOut = false
    }
}

extension BridgeBluetooth: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { self.error = error.localizedDescription; return }
        guard let service = peripheral.services?.first(where: { $0.uuid == BridgeService.service }) else {
            self.error = "This DashBridge needs the companion-app firmware."
            incompatibleIdentifiers.insert(peripheral.identifier)
            manager?.cancelPeripheralConnection(peripheral)
            return
        }
        connectionStage = "Reading setup service"
        peripheral.discoverCharacteristics([BridgeService.status, BridgeService.policy, BridgeService.command], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let error { self.error = error.localizedDescription; return }
        for characteristic in service.characteristics ?? [] {
            switch characteristic.uuid {
            case BridgeService.status: statusCharacteristic = characteristic
            case BridgeService.policy: policyCharacteristic = characteristic
            case BridgeService.command: commandCharacteristic = characteristic
            default: break
            }
        }
        guard statusCharacteristic != nil, policyCharacteristic != nil,
              commandCharacteristic != nil else {
            self.error = "DashBridge setup service is incomplete."
            return
        }
        UserDefaults.standard.set(peripheral.identifier.uuidString, forKey: rememberedKey)
        deviceID = peripheral.identifier
        connectionStage = "Checking DashBridge"
        refresh()
        refreshTimer?.invalidate()
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            if characteristic.uuid == BridgeService.status { self.error = error.localizedDescription }
            return
        }
        guard let data = characteristic.value else { return }
        if characteristic.uuid == BridgeService.status {
            do {
                status = try BridgeStatus(data)
                connectionStage = "Ready"
                connectionTimer?.invalidate()
                connectionTimer = nil
            }
            catch { self.error = error.localizedDescription }
        } else if characteristic.uuid == BridgeService.policy {
            let text = String(decoding: data, as: UTF8.self)
            allowedIDs = Set(text.split(separator: "\n").map(String.init))
            policyLoaded = true
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard self.peripheral === peripheral else { return }
        writing = false
        let operation = pendingCommands.first?.first
        if !pendingCommands.isEmpty { pendingCommands.removeFirst() }
        if error != nil {
            commandError = operation == 1 || operation == 2
                ? "Couldn't save this app choice. Please try again."
                : "DashBridge couldn't complete that action. Please try again."
        } else {
            commandError = nil
        }
        if let policyCharacteristic { peripheral.readValue(for: policyCharacteristic) }
        sendNext()
    }
}
