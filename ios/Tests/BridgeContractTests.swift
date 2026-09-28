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

    func testAuthorizedAccessorySelectionNeverGuessesBetweenMultipleBoards() {
        let current = UUID()
        let remembered = UUID()
        let other = UUID()

        XCTAssertEqual(BridgeBluetooth.preferredAuthorizedIdentifier(
            currentID: current, rememberedID: remembered,
            candidates: [other, current, remembered]), current)
        XCTAssertEqual(BridgeBluetooth.preferredAuthorizedIdentifier(
            currentID: nil, rememberedID: remembered,
            candidates: [other, remembered]), remembered)
        XCTAssertEqual(BridgeBluetooth.preferredAuthorizedIdentifier(
            currentID: nil, rememberedID: nil, candidates: [other]), other)
        XCTAssertNil(BridgeBluetooth.preferredAuthorizedIdentifier(
            currentID: nil, rememberedID: nil, candidates: [other, UUID()]))
        XCTAssertNil(BridgeBluetooth.preferredAuthorizedIdentifier(
            currentID: current, rememberedID: remembered, candidates: []))
    }

    func testPickerSelectionUsesOnlyTheEventOrOneNewAuthorization() {
        let existing = UUID()
        let selected = UUID()
        let unrelated = UUID()

        XCTAssertEqual(BridgeBluetooth.pickerSelectionIdentifier(
            eventID: selected, authorizedBefore: [existing],
            authorizedAfter: [existing, selected]), selected)
        XCTAssertEqual(BridgeBluetooth.pickerSelectionIdentifier(
            eventID: nil, authorizedBefore: [existing],
            authorizedAfter: [existing, selected]), selected)
        XCTAssertNil(BridgeBluetooth.pickerSelectionIdentifier(
            eventID: nil, authorizedBefore: [existing], authorizedAfter: [existing]))
        XCTAssertNil(BridgeBluetooth.pickerSelectionIdentifier(
            eventID: nil, authorizedBefore: [existing],
            authorizedAfter: [existing, selected, unrelated]))
        XCTAssertEqual(BridgeBluetooth.pickerSelectionIdentifier(
            eventID: unrelated, authorizedBefore: [existing],
            authorizedAfter: [existing, selected]), selected)
    }

    func testPhoneSetupRequiresMessagesNotificationsAndCalls() {
        XCTAssertFalse(BridgeStatus(bits: 0b0000_0011).phoneSetupReady)
        XCTAssertFalse(BridgeStatus(bits: 0b0000_0101).phoneSetupReady)
        XCTAssertTrue(BridgeStatus(bits: 0b0000_0111).phoneSetupReady)
    }

    func testPhoneSetupProgressMaskTracksEachIndependentConnection() {
        XCTAssertEqual(BridgeBluetooth.phoneSetupProgressMask(BridgeStatus(bits: 0)), 0)
        XCTAssertEqual(BridgeBluetooth.phoneSetupProgressMask(BridgeStatus(bits: 0b001)), 1)
        XCTAssertEqual(BridgeBluetooth.phoneSetupProgressMask(BridgeStatus(bits: 0b010)), 2)
        XCTAssertEqual(BridgeBluetooth.phoneSetupProgressMask(BridgeStatus(bits: 0b100)), 4)
        XCTAssertEqual(BridgeBluetooth.phoneSetupProgressMask(BridgeStatus(bits: 0b111)), 7)
    }

    func testTeslaSetupRequiresEveryBoardTransport() {
        XCTAssertFalse(BridgeStatus(bits: 0b0111_1111).teslaSetupReady)
        XCTAssertFalse(BridgeStatus(bits: 0b1111_0111).teslaSetupReady)
        XCTAssertTrue(BridgeStatus(bits: 0b1111_1111).teslaSetupReady)
    }
}
