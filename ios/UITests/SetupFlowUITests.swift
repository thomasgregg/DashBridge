import XCTest

final class SetupFlowUITests: XCTestCase {
    func testPhoneGuidanceWaitsForExplicitConnectAction() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-screen-preview", "connect-iphone"]
        app.launch()

        XCTAssertTrue(app.staticTexts["Connect your iPhone."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["When you’re ready, tap Connect and follow the prompts on your iPhone."].exists)
        XCTAssertFalse(app.staticTexts["Connecting your iPhone."].exists)

        app.buttons["Connect my iPhone"].tap()

        XCTAssertTrue(app.staticTexts["Connecting your iPhone."].waitForExistence(timeout: 5))
    }

    func testWelcomeExplainsPowerBeforeScanning() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        let instruction = app.staticTexts["Plug in DashBridge and keep it near your iPhone."]
        let getStarted = app.buttons["It's plugged in"]
        XCTAssertTrue(instruction.waitForExistence(timeout: 5))
        XCTAssertTrue(getStarted.exists)
        XCTAssertLessThan(instruction.frame.minY, getStarted.frame.minY)
    }

    func testSavedChoiceMissingFromAppListCanBeRemoved() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-saved-choice-preview"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Saved on DashBridge"].waitForExistence(timeout: 5))
        let choice = app.switches["WhatsApp Business"]
        XCTAssertEqual(choice.value as? String, "1")
        choice.coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).tap()
        let removed = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: choice)
        XCTAssertEqual(XCTWaiter.wait(for: [removed], timeout: 3), .completed)
    }

    func testTestScreenOffersNotificationButton() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        app.buttons["Preview connected car"].tap()
        let sendTest = app.buttons["Send test notification"]
        XCTAssertTrue(sendTest.waitForExistence(timeout: 5))
        XCTAssertGreaterThan(sendTest.frame.midY, app.frame.height * 0.75)
        XCTAssertTrue(app.buttons["Later"].exists)
        sendTest.tap()
        XCTAssertTrue(app.staticTexts["Connect DashBridge to send a test notification to your Tesla."].exists)
    }

    func testCompletionCelebrationKeepsReadyActionsInteractive() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        app.buttons["Preview connected car"].tap()
        app.buttons["Preview ready screen"].tap()

        let help = app.buttons["Connection help"]
        XCTAssertTrue(help.waitForExistence(timeout: 5))
        help.tap()
        XCTAssertTrue(app.staticTexts["Connection status."].waitForExistence(timeout: 5))
    }

    func testCheckingKeepsProgressUntilItOffersRetry() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-checking-preview"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Connecting your iPhone."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["Bluetooth pairing"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.staticTexts["Board found"].exists)
        XCTAssertFalse(app.staticTexts["Checking your connection…"].exists)
        XCTAssertTrue(app.staticTexts["Connection interrupted."].waitForExistence(timeout: 8))
        XCTAssertTrue(app.otherElements["Retry connection"].exists)
        XCTAssertTrue(app.buttons["Try again"].exists)
        XCTAssertFalse(app.buttons["Copy diagnostics"].exists)
        XCTAssertFalse(app.staticTexts["Bluetooth pairing"].exists)
    }

    func testCheckingUsesStatusAlreadyReceived() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-cached-status-preview"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Connecting your iPhone."].waitForExistence(timeout: 5))
        XCTAssertFalse(app.staticTexts["Checking your connection…"].exists)
    }

    func testRetryBackReturnsToWelcomeWithoutStartingAnotherCheck() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-checking-preview"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Connection interrupted."].waitForExistence(timeout: 8))

        app.buttons["Back"].tap()
        XCTAssertTrue(app.staticTexts["Welcome to"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.staticTexts["Checking your connection…"].exists)
    }

    func testDiscoveryProgressReachesItsDeadline() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-progress-preview"]
        app.launch()

        app.buttons["It's plugged in"].tap()
        let progress = app.progressIndicators["Connection check progress"]
        XCTAssertTrue(progress.waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["Connecting…"].exists)
        XCTAssertTrue(app.staticTexts["Looking nearby…"].exists)
        XCTAssertTrue(app.staticTexts["The app can't find DashBridge. Make sure it's powered and not connected to another iPhone, then try again."].waitForExistence(timeout: 18))
        XCTAssertFalse(progress.exists)
        XCTAssertFalse(app.staticTexts["Looking nearby…"].exists)
    }

    func testSimulatorDiscoveryOffersPreviewInsteadOfSpinning() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["It's plugged in"].tap()
        XCTAssertTrue(app.buttons["Preview without hardware"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.staticTexts["Looking nearby…"].exists)
    }

    func testBackNavigationKeepsTheExistingConnection() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))

        app.buttons["Back"].tap()
        XCTAssertTrue(app.staticTexts["Welcome to"].waitForExistence(timeout: 5))

        app.buttons["It's plugged in"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        XCTAssertFalse(app.staticTexts["Connecting…"].exists)
    }

    func testTeslaCanBeDeferredAndFinishedLater() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))

        app.switches["WhatsApp"].coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).tap()
        XCTAssertEqual(app.switches["WhatsApp"].value as? String, "1")

        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Connect your Tesla."].waitForExistence(timeout: 5))

        app.buttons["Do this later"].tap()
        XCTAssertTrue(app.staticTexts["iPhone is ready!"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Back"].exists)
        XCTAssertTrue(app.buttons["Change apps"].exists)
        XCTAssertTrue(app.buttons["Finish in the car"].exists)
        XCTAssertTrue(app.staticTexts["WhatsApp"].exists)
        XCTAssertFalse(app.staticTexts["Allowed apps · WhatsApp"].exists)
        XCTAssertFalse(app.staticTexts.containing(NSPredicate(format: "label CONTAINS 'Unknown ATT error'")).firstMatch.exists)

        app.buttons["Change apps"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        app.buttons["Back"].tap()
        XCTAssertTrue(app.staticTexts["iPhone is ready!"].waitForExistence(timeout: 5))
        app.buttons["Change apps"].tap()
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["iPhone is ready!"].waitForExistence(timeout: 5))

        app.buttons["Finish in the car"].tap()
        XCTAssertTrue(app.staticTexts["Connect your Tesla."].waitForExistence(timeout: 5))
        app.buttons["Do this later"].tap()
        XCTAssertTrue(app.staticTexts["iPhone is ready!"].waitForExistence(timeout: 5))
    }

    func testPreviewMakesTheFinalConfirmationClear() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        app.switches["WhatsApp"].coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).tap()
        app.buttons["Continue"].tap()
        app.buttons["Preview connected car"].tap()

        XCTAssertTrue(app.staticTexts["Preview the final step."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Preview ready screen"].exists)
        app.buttons["Preview ready screen"].tap()
        XCTAssertTrue(app.staticTexts["You're all set!"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["Back"].exists)
        XCTAssertTrue(app.staticTexts["WhatsApp"].exists)
    }

    func testConnectionStatusIsComprehensiveAndUserOrdered() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        app.buttons["Continue"].tap()
        app.buttons["Do this later"].tap()
        app.buttons["Connection help"].tap()

        XCTAssertTrue(app.staticTexts["Connection status."].waitForExistence(timeout: 5))
        let iPhone = app.staticTexts["iPhone"]
        let tesla = app.staticTexts["Tesla"]
        let dashBridge = app.staticTexts["DashBridge"]
        XCTAssertTrue(iPhone.exists)
        XCTAssertTrue(tesla.exists)
        XCTAssertTrue(dashBridge.exists)
        XCTAssertLessThan(iPhone.frame.minY, tesla.frame.minY)
        XCTAssertLessThan(tesla.frame.minY, dashBridge.frame.minY)
        XCTAssertTrue(app.buttons["Copy diagnostics"].exists)
        XCTAssertFalse(app.buttons["Connection details"].exists)
    }
}
