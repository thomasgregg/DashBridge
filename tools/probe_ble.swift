import CoreBluetooth
import Foundation

private func report(_ message: String) {
    print(message)
    fflush(stdout)
}

private final class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var device: CBPeripheral?
    private let setupService = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F0")
    private let statusCharacteristic = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F1")
    private let scanOnly = CommandLine.arguments.contains("--scan-only")

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        report("Mac Bluetooth state: \(central.state.rawValue)")
        if central.state == .poweredOn {
            central.scanForPeripherals(withServices: nil)
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        guard name == "DashBridge A" || name == "Dash Messages" else { return }
        if scanOnly {
            report("Found \(name) advertising; no connection attempted")
            central.stopScan()
            exit(0)
        }
        report("Found \(name); connecting")
        device = peripheral
        peripheral.delegate = self
        central.stopScan()
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        report("Connected; discovering setup service")
        peripheral.discoverServices([setupService])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        report("Connection failed: \(error?.localizedDescription ?? "unknown")")
        exit(1)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        report("Service discovery: \(error?.localizedDescription ?? "ok")")
        let services = (peripheral.services ?? []).map { $0.uuid.uuidString }
        report("Services: \(services)")
        guard let service = peripheral.services?.first(where: { $0.uuid == setupService }) else {
            central.cancelPeripheralConnection(peripheral)
            exit(2)
        }
        peripheral.discoverCharacteristics([statusCharacteristic], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        guard error == nil,
              let characteristic = service.characteristics?.first(where: { $0.uuid == statusCharacteristic }) else {
            report("Status characteristic missing: \(error?.localizedDescription ?? "unknown")")
            exit(2)
        }
        peripheral.readValue(for: characteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if let error {
            report("Status read failed: \(error.localizedDescription)")
            exit(2)
        }
        let bytes = Array(characteristic.value ?? Data())
        report("Status bytes: \(bytes)")
        central.cancelPeripheralConnection(peripheral)
        exit(bytes.count >= 3 && bytes[0] == 1 ? 0 : 2)
    }
}

private let probe = Probe()
RunLoop.current.run(until: Date(timeIntervalSinceNow: 30))
report("Timed out before discovering the setup service")
exit(3)
