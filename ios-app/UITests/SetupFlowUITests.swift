import XCTest

final class SetupFlowUITests: XCTestCase {
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

        app.buttons["Continue"].tap()
        XCTAssertTrue(app.staticTexts["Connect your Tesla."].waitForExistence(timeout: 5))

        app.buttons["Do this later"].tap()
        XCTAssertTrue(app.staticTexts["Your iPhone is set up."].waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["Change apps"].exists)
        XCTAssertTrue(app.buttons["Finish in the car"].exists)
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
}
