import Foundation
import UserNotifications

protocol TestNotificationScheduling {
    func requestAuthorization() async throws -> Bool
    func schedule(previewOnly: Bool) async throws
}

struct SystemTestNotificationService: TestNotificationScheduling {
    private let center: UNUserNotificationCenter

    init(center: UNUserNotificationCenter = .current()) {
        self.center = center
    }

    func requestAuthorization() async throws -> Bool {
        try await center.requestAuthorization(options: [.alert, .sound])
    }

    func schedule(previewOnly: Bool) async throws {
        let content = UNMutableNotificationContent()
        content.title = "DashBridge test"
        content.body = previewOnly
            ? "Your iPhone can show the test. Connect DashBridge to send notifications to your Tesla."
            : "If this message appears on your Tesla, notifications are working."
        content.sound = .default
        let request = UNNotificationRequest(
            identifier: "dashbridge-test-\(UUID().uuidString)",
            content: content,
            trigger: UNTimeIntervalNotificationTrigger(timeInterval: 12, repeats: false)
        )
        try await center.add(request)
    }
}
