import XCTest
@testable import DashBridge

final class SetupFlowTests: XCTestCase {
    private let phoneReady = BridgeStatus(bits: 0b0000_0111)
    private let fullyReady = BridgeStatus(bits: 0b1111_1111)

    func testEveryFreshSessionStartsAtWelcomeEvenForReturningUser() {
        var progress = SetupProgress()
        progress.welcomed = true
        var machine = SetupFlowMachine(progress: progress)

        XCTAssertEqual(machine.handle(.launch), [])
        XCTAssertEqual(machine.state.step, .welcome)
        XCTAssertEqual(machine.handle(.launch), [])

        XCTAssertEqual(machine.handle(.getStarted(connected: false, status: nil)),
                       [.startBluetooth])
        XCTAssertEqual(machine.state.step, .finding)
    }

    func testDiscoveryShowsGuidanceBeforeRequestingANCSConnection() {
        var machine = SetupFlowMachine()
        _ = machine.handle(.getStarted(connected: false, status: nil))

        XCTAssertEqual(machine.handle(.peripheralFound), [])
        XCTAssertEqual(machine.state.step, .checking)
        XCTAssertTrue(machine.state.connectionRequestPending)

        XCTAssertEqual(machine.handle(.connectionGuidancePresented), [.connectFound])
        XCTAssertFalse(machine.state.connectionRequestPending)
        XCTAssertEqual(machine.handle(.connectionGuidancePresented), [])
    }

    func testPhoneSetupRoutesThroughSharingPairingAndApps() {
        var machine = SetupFlowMachine()
        _ = machine.handle(.getStarted(connected: true, status: nil))

        _ = machine.handle(.statusReceived(BridgeStatus(bits: 0b0000_0001)))
        XCTAssertEqual(machine.state.step, .sharing)

        _ = machine.handle(.statusReceived(BridgeStatus(bits: 0b0000_0011)))
        XCTAssertEqual(machine.state.step, .pair)

        XCTAssertEqual(machine.handle(.statusReceived(phoneReady)), [.loadCatalog])
        XCTAssertEqual(machine.state.step, .apps)
    }

    func testCompletedChoicesRouteToCarThenAutomaticallyToTest() {
        var machine = SetupFlowMachine()
        _ = machine.handle(.previewStarted(checking: false, cachedStatus: nil))

        _ = machine.handle(.appsContinued(status: phoneReady))
        XCTAssertEqual(machine.state.step, .car)

        _ = machine.handle(.statusReceived(fullyReady))
        XCTAssertEqual(machine.state.step, .test)

        XCTAssertEqual(machine.handle(.confirmTest), [.loadCatalog])
        XCTAssertEqual(machine.state.step, .ready)
        XCTAssertTrue(machine.state.progress.testConfirmed)
    }

    func testDeferredTeslaSetupStaysDeferredAfterEditingApps() {
        var machine = SetupFlowMachine()
        _ = machine.handle(.previewStarted(checking: false, cachedStatus: nil))
        _ = machine.handle(.appsContinued(status: phoneReady))
        _ = machine.handle(.deferTesla)

        XCTAssertEqual(machine.state.step, .ready)
        XCTAssertTrue(machine.state.progress.teslaDeferred)

        _ = machine.handle(.openAppsFromReady)
        XCTAssertEqual(machine.state.step, .apps)
        _ = machine.handle(.appsContinued(status: phoneReady))
        XCTAssertEqual(machine.state.step, .ready)
    }

    func testDifferentBoardResetsBoardSpecificProgress() {
        var progress = SetupProgress(welcomed: true, choseApps: true,
                                     testConfirmed: true, teslaDeferred: true,
                                     completedDeviceID: "old")
        var machine = SetupFlowMachine(progress: progress)

        _ = machine.handle(.deviceIdentified("new"))

        XCTAssertTrue(machine.state.progress.welcomed)
        XCTAssertFalse(machine.state.progress.choseApps)
        XCTAssertFalse(machine.state.progress.testConfirmed)
        XCTAssertFalse(machine.state.progress.teslaDeferred)
        XCTAssertEqual(machine.state.progress.completedDeviceID, "new")
    }

    func testCheckingTimeoutHasOneDeterministicDestination() {
        var machine = SetupFlowMachine()
        _ = machine.handle(.getStarted(connected: true, status: nil))

        let effects = machine.handle(.checkingTimedOut(connected: true))

        XCTAssertEqual(machine.state.step, .help)
        XCTAssertEqual(machine.state.helpReturnStep, .checking)
        XCTAssertEqual(effects, [.setBluetoothError(
            "Your iPhone connected to DashBridge, but the app couldn't check its setup yet.")])
    }

    func testBackNavigationPreservesEstablishedBehavior() {
        var machine = SetupFlowMachine()
        _ = machine.handle(.previewScreen(.test))
        _ = machine.handle(.back)
        XCTAssertEqual(machine.state.step, .car)
        XCTAssertTrue(machine.state.reviewingCarStep)

        _ = machine.handle(.back)
        XCTAssertEqual(machine.state.step, .apps)
        XCTAssertFalse(machine.state.reviewingCarStep)
    }
}
