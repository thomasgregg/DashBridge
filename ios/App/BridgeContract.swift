import CoreBluetooth
import Foundation

// The app-facing representation of contracts/setup_gatt_v1/contract.json.
// Keep this file free of Core Bluetooth lifecycle behavior so the released
// wire contract can be tested independently from discovery and pairing.
enum BridgeService {
    static let service = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F0")
    static let status = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F1")
    static let policy = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F2")
    static let command = CBUUID(string: "0D9B6B3D-CEB1-4E16-A0C1-3D28C258A6F3")

    static let discoveryNames = ["DashBridge A", "Dash Messages", "DashBridge"]
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
        guard data.count == 3, data[0] == 1 else { throw BridgeError.unsupportedStatus }
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

enum BridgeCommand: Equatable {
    case allowApplication(String)
    case denyApplication(String)
    case openPhonePairing
    case beginNotificationTest

    var operation: UInt8 {
        switch self {
        case .allowApplication: 1
        case .denyApplication: 2
        case .openPhonePairing: 3
        case .beginNotificationTest: 4
        }
    }

    var payload: Data {
        var data = Data([operation])
        switch self {
        case let .allowApplication(id), let .denyApplication(id):
            data.append(contentsOf: id.utf8)
        case .openPhonePairing, .beginNotificationTest:
            break
        }
        return data
    }
}

enum BridgeError: LocalizedError {
    case unsupportedStatus
    case commandUnavailable
    case commandTooLong

    var errorDescription: String? {
        switch self {
        case .unsupportedStatus: "This DashBridge firmware is not supported."
        case .commandUnavailable: "DashBridge is still connecting. Try again in a moment."
        case .commandTooLong: "This app name is too long for DashBridge."
        }
    }
}
