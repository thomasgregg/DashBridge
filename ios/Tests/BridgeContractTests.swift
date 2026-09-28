import CoreBluetooth
import XCTest
@testable import DashBridge

final class BridgeContractTests: XCTestCase {
    func testStatusDecodesReleasedBitLayout() throws {
        let status = try BridgeStatus(Data([1, 0b1010_0101, 0b0000_0001]))

        XCTAssertTrue(status.phoneBluetooth)
        XCTAssertFalse(status.notifications)
        XCTAssertTrue(status.phoneCalls)
        XCTAssertFalse(status.internalLink)
        XCTAssertFalse(status.teslaMessages)
        XCTAssertTrue(status.teslaTransport)
        XCTAssertFalse(status.teslaSync)
        XCTAssertTrue(status.teslaCalls)
        XCTAssertTrue(status.phonePairingOpen)
    }

    func testStatusRejectsWrongVersionOrLength() {
        XCTAssertThrowsError(try BridgeStatus(Data([2, 0, 0])))
        XCTAssertThrowsError(try BridgeStatus(Data([1, 0])))
        XCTAssertThrowsError(try BridgeStatus(Data([1, 0, 0, 0])))
    }

    func testCommandsEncodeReleasedOperations() {
        XCTAssertEqual(BridgeCommand.allowApplication("net.example.chat").payload,
                       Data([1]) + Data("net.example.chat".utf8))
        XCTAssertEqual(BridgeCommand.denyApplication("net.example.chat").payload,
                       Data([2]) + Data("net.example.chat".utf8))
        XCTAssertEqual(BridgeCommand.openPhonePairing.payload, Data([3]))
        XCTAssertEqual(BridgeCommand.beginNotificationTest.payload, Data([4]))
    }

    func testEveryPhoneConnectionEnablesClassicTransportBridging() {
        let firstPairing = BridgeBluetooth.connectionOptions(requiresANCS: true)
        XCTAssertEqual(firstPairing[CBConnectPeripheralOptionRequiresANCS] as? Bool, true)
        XCTAssertEqual(firstPairing[CBConnectPeripheralOptionEnableTransportBridgingKey] as? Bool, true)

        let reconnect = BridgeBluetooth.connectionOptions(requiresANCS: false)
        XCTAssertNil(reconnect[CBConnectPeripheralOptionRequiresANCS])
        XCTAssertEqual(reconnect[CBConnectPeripheralOptionEnableTransportBridgingKey] as? Bool, true)
    }

    func testAuthorizedAccessoryReconnectOnlyRepeatsANCSWhenNeeded() {
        XCTAssertTrue(BridgeBluetooth.reconnectRequiresANCS(ancsAuthorized: false))
        XCTAssertFalse(BridgeBluetooth.reconnectRequiresANCS(ancsAuthorized: true))
    }

    func testAutomaticRecoveryCannotAttachToADifferentNearbyBoard() {
        let selected = UUID()

        XCTAssertTrue(BridgeBluetooth.isAuthorizedRecoveryCandidate(
            authorizedID: selected, candidateID: selected))
        XCTAssertFalse(BridgeBluetooth.isAuthorizedRecoveryCandidate(
            authorizedID: selected, candidateID: UUID()))
        XCTAssertFalse(BridgeBluetooth.isAuthorizedRecoveryCandidate(
            authorizedID: nil, candidateID: selected))
    }

    func testReselectionRemovesOnlyTheStaleAuthorizedAccessory() {
        let selected = UUID()

        XCTAssertFalse(BridgeBluetooth.shouldRemoveAccessoryForReselection(
            requiresReselection: false, authorizedID: selected, candidateID: selected))
        XCTAssertTrue(BridgeBluetooth.shouldRemoveAccessoryForReselection(
            requiresReselection: true, authorizedID: selected, candidateID: selected))
        XCTAssertFalse(BridgeBluetooth.shouldRemoveAccessoryForReselection(
            requiresReselection: true, authorizedID: selected, candidateID: UUID()))
        XCTAssertTrue(BridgeBluetooth.shouldRemoveAccessoryForReselection(
            requiresReselection: true, authorizedID: nil, candidateID: selected))
        XCTAssertFalse(BridgeBluetooth.shouldRemoveAccessoryForReselection(
            requiresReselection: true, authorizedID: selected, candidateID: nil))
    }

    func testPhoneSetupRequiresMessagesNotificationsAndCalls() {
        XCTAssertFalse(BridgeStatus(bits: 0b0000_0011).phoneSetupReady)
        XCTAssertFalse(BridgeStatus(bits: 0b0000_0101).phoneSetupReady)
        XCTAssertTrue(BridgeStatus(bits: 0b0000_0111).phoneSetupReady)
    }

    func testTeslaSetupRequiresEveryBoardTransport() {
        XCTAssertFalse(BridgeStatus(bits: 0b0111_1111).teslaSetupReady)
        XCTAssertFalse(BridgeStatus(bits: 0b1111_0111).teslaSetupReady)
        XCTAssertTrue(BridgeStatus(bits: 0b1111_1111).teslaSetupReady)
    }
}
