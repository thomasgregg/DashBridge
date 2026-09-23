import XCTest

final class SetupFlowUITests: XCTestCase {
    func testCheckingKeepsProgressUntilItOffersRetry() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-checking-preview"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Checking your connection…"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.progressIndicators["Connection check progress"].exists)
        XCTAssertTrue(app.staticTexts["Couldn't check DashBridge yet."].waitForExistence(timeout: 8))
        XCTAssertTrue(app.buttons["Check again"].exists)
        XCTAssertFalse(app.staticTexts["Checking your connection…"].exists)
    }

    func testCheckingUsesStatusAlreadyReceived() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-cached-status-preview"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Dash Messages is connected."].waitForExistence(timeout: 5))
        XCTAssertFalse(app.staticTexts["Checking your connection…"].exists)
    }

    func testDiscoveryProgressReachesItsDeadline() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing", "-dashbridge-ui-progress-preview"]
        app.launch()

        app.buttons["Get started"].tap()
        let progress = app.progressIndicators["Connection check progress"]
        XCTAssertTrue(progress.waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["Looking nearby…"].exists)
        XCTAssertTrue(app.staticTexts["The app can't find DashBridge's setup connection. Check that it's powered, then try again."].waitForExistence(timeout: 18))
        XCTAssertFalse(progress.exists)
        XCTAssertFalse(app.staticTexts["Looking nearby…"].exists)
    }

    func testSimulatorDiscoveryOffersPreviewInsteadOfSpinning() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Get started"].tap()
        XCTAssertTrue(app.buttons["Preview without hardware"].waitForExistence(timeout: 20))
        XCTAssertFalse(app.staticTexts["Looking nearby…"].exists)
    }

    func testBackNavigationKeepsTheExistingConnection() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))

        app.buttons["Back"].tap()
        XCTAssertTrue(app.staticTexts["DashBridge found."].waitForExistence(timeout: 5))
        app.buttons["Back"].tap()
        XCTAssertTrue(app.staticTexts["Welcome to"].waitForExistence(timeout: 5))

        app.buttons["Get started"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        XCTAssertFalse(app.staticTexts["Looking for DashBridge"].exists)
    }

    func testTeslaCanBeDeferredAndFinishedLater() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))

        app.switches["WhatsApp"].coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).tap()
        XCTAssertEqual(app.switches["WhatsApp"].value as? String, "1")

        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Connect your Tesla."].waitForExistence(timeout: 5))

        app.buttons["Do this later"].tap()
        XCTAssertTrue(app.staticTexts["Your iPhone is set up."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Change apps"].exists)
        XCTAssertTrue(app.buttons["Finish in the car"].exists)
        XCTAssertTrue(app.staticTexts["WhatsApp"].exists)
        XCTAssertFalse(app.staticTexts["Allowed apps · WhatsApp"].exists)
        XCTAssertFalse(app.staticTexts.containing(NSPredicate(format: "label CONTAINS 'Unknown ATT error'")).firstMatch.exists)

        app.buttons["Change apps"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Your iPhone is set up."].waitForExistence(timeout: 5))

        app.buttons["Finish in the car"].tap()
        XCTAssertTrue(app.staticTexts["Connect your Tesla."].waitForExistence(timeout: 5))
        app.buttons["Do this later"].tap()
        XCTAssertTrue(app.staticTexts["Your iPhone is set up."].waitForExistence(timeout: 5))
    }

    func testPreviewMakesTheFinalConfirmationClear() {
        let app = XCUIApplication()
        app.launchArguments = ["-dashbridge-ui-testing"]
        app.launch()

        app.buttons["Preview without hardware"].tap()
        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Choose your apps."].waitForExistence(timeout: 5))
        app.switches["WhatsApp"].coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).tap()
        app.buttons["Continue"].tap()
        app.buttons["Preview connected car"].tap()

        XCTAssertTrue(app.staticTexts["Preview the final step."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Preview ready screen"].exists)
        app.buttons["Preview ready screen"].tap()
        XCTAssertTrue(app.staticTexts["Ready to go."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts["WhatsApp"].exists)
    }
}
