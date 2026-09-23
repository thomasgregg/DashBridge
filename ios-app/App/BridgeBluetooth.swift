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
    @Published var error: String?

    private var manager: CBCentralManager?
    private var discovered: CBPeripheral?
    private var peripheral: CBPeripheral?
    private var statusCharacteristic: CBCharacteristic?
    private var policyCharacteristic: CBCharacteristic?
    private var commandCharacteristic: CBCharacteristic?
    private var refreshTimer: Timer?
    private var pendingCommands: [Data] = []
    private var writing = false
    private var lastPolicyRead = Date.distantPast
    private let rememberedKey = "dashbridge.peripheral"
    private var incompatibleIdentifiers: Set<UUID> = []

    func start() {
        if manager == nil {
            manager = CBCentralManager(delegate: self, queue: .main)
        } else if manager?.state == .poweredOn {
            scan()
        }
    }

    func scan(clearError: Bool = true) {
        guard let manager, manager.state == .poweredOn, !connected else { return }
        if clearError { error = nil }
        foundName = nil
        discovered = nil
        scanning = true
        // ANCS service solicitation occupies the advertising packet today.
        // We scan in the foreground, match its local name, and verify our GATT
        // service after connecting. No background scan is claimed.
        manager.scanForPeripherals(withServices: nil)
    }

    func connectFound() {
        guard let discovered, let manager else { return }
        scanning = false
        manager.stopScan()
        peripheral = discovered
        discovered.delegate = self
        manager.connect(discovered)
    }

    func send(_ operation: UInt8, appID: String? = nil) {
        var payload = Data([operation])
        if let appID { payload.append(contentsOf: appID.utf8) }
        guard let peripheral, commandCharacteristic != nil else {
            error = BridgeError.commandUnavailable.localizedDescription
            return
        }
        guard payload.count <= peripheral.maximumWriteValueLength(for: .withResponse) else {
            error = BridgeError.commandTooLong.localizedDescription
            return
        }
        pendingCommands.append(payload)
        sendNext()
    }

    func setAllowed(_ id: String, allowed: Bool) {
        guard !id.isEmpty else { return }
        guard connected, commandCharacteristic != nil, policyLoaded else {
            error = BridgeError.commandUnavailable.localizedDescription
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
        if let policyCharacteristic, Date().timeIntervalSince(lastPolicyRead) > 15 {
            lastPolicyRead = Date()
            peripheral.readValue(for: policyCharacteristic)
        }
    }

    private func clearConnection() {
        refreshTimer?.invalidate()
        refreshTimer = nil
        connected = false
        status = nil
        statusCharacteristic = nil
        policyCharacteristic = nil
        commandCharacteristic = nil
        policyLoaded = false
        pendingCommands.removeAll()
        writing = false
    }
}

extension BridgeBluetooth: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothReady = central.state == .poweredOn
        if bluetoothReady {
            if let rawID = UserDefaults.standard.string(forKey: rememberedKey),
               let id = UUID(uuidString: rawID),
               let saved = central.retrievePeripherals(withIdentifiers: [id]).first {
                discovered = saved
                foundName = saved.name ?? "DashBridge"
                connectFound()
            } else { scan() }
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
        scanning = false
        central.stopScan()
        if UserDefaults.standard.string(forKey: rememberedKey) == peripheral.identifier.uuidString {
            connectFound()
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connected = true
        error = nil
        peripheral.delegate = self
        peripheral.discoverServices([BridgeService.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        clearConnection()
        self.error = error?.localizedDescription ?? "Could not connect to DashBridge."
        scan()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        clearConnection()
        if let error { self.error = error.localizedDescription }
        scan(clearError: error == nil && !incompatibleIdentifiers.contains(peripheral.identifier))
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
            do { status = try BridgeStatus(data) }
            catch { self.error = error.localizedDescription }
        } else if characteristic.uuid == BridgeService.policy {
            let text = String(decoding: data, as: UTF8.self)
            allowedIDs = Set(text.split(separator: "\n").map(String.init))
            policyLoaded = true
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        writing = false
        if !pendingCommands.isEmpty { pendingCommands.removeFirst() }
        if let error { self.error = "DashBridge could not save that change: \(error.localizedDescription)" }
        if let policyCharacteristic { peripheral.readValue(for: policyCharacteristic) }
        sendNext()
    }
}
