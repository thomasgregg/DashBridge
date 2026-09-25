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
}
