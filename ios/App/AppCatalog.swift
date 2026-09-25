import FamilyControls
import ManagedSettings
import OSLog
import SwiftUI

enum AppTestMode {
    static let enabled: Bool = {
#if DEBUG
        ProcessInfo.processInfo.arguments.contains("-dashbridge-ui-testing")
#else
        false
#endif
    }()

    static let progressPreview = enabled && ProcessInfo.processInfo.arguments.contains(
        "-dashbridge-ui-progress-preview")
    static let checkingPreview = enabled && ProcessInfo.processInfo.arguments.contains(
        "-dashbridge-ui-checking-preview")
    static let cachedStatusPreview = enabled && ProcessInfo.processInfo.arguments.contains(
        "-dashbridge-ui-cached-status-preview")
    static let savedChoicePreview = enabled && ProcessInfo.processInfo.arguments.contains(
        "-dashbridge-ui-saved-choice-preview")
}

struct AppChoice: Identifiable {
    let id: String
    let name: String
    let token: ApplicationToken?

    init(_ id: String, _ name: String, token: ApplicationToken? = nil) {
        self.id = id
        self.name = name
        self.token = token
    }
}

@MainActor
final class AppCatalog: ObservableObject {
    private let logger = Logger(subsystem: "dev.dashbridge.companion", category: "AppCatalog")
    enum Access { case notRequested, loading, available, unavailable }

    @Published private(set) var access: Access = .notRequested
    @Published private(set) var installed: [AppChoice] = []
    @Published private(set) var message: String?
    private let suggestions: [AppChoice] = [
        AppChoice("net.whatsapp.WhatsApp", "WhatsApp"),
        AppChoice("net.whatsapp.WhatsAppSMB", "WhatsApp Business"),
        AppChoice("org.whispersystems.signal", "Signal"),
        AppChoice("ph.telegra.Telegraph", "Telegram"),
        AppChoice("com.tinyspeck.chatlyio", "Slack"),
        AppChoice("com.facebook.Messenger", "Messenger"),
        AppChoice("com.tencent.xin", "WeChat"),
        AppChoice("com.tencent.mqq", "QQ"),
        AppChoice("jp.naver.line", "LINE"),
        AppChoice("com.iwilab.KakaoTalk", "KakaoTalk"),
        AppChoice("vn.com.vng.zingalo", "Zalo"),
        AppChoice("com.viber", "Viber"),
        AppChoice("imoimiphone", "imo"),
        AppChoice("com.alibaba.dingtalklite", "DingTalk"),
        AppChoice("com.microsoft.skype.teams", "Microsoft Teams")
    ]

    var suggested: [AppChoice] {
        guard access == .available else { return [] }
        let installedByID = Dictionary(installed.map { ($0.id, $0) }, uniquingKeysWith: { first, _ in first })
        return suggestions.compactMap { installedByID[$0.id] }
            .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    func choice(for id: String) -> AppChoice {
        if let installedChoice = installed.first(where: { $0.id == id }) { return installedChoice }
        if let familiarChoice = suggestions.first(where: { $0.id == id }) { return familiarChoice }
        let fallback = id.split(separator: ".").last.map(String.init) ?? id
        return AppChoice(id, fallback)
    }

    func loadIfNeeded() {
        if access == .notRequested { reload() }
    }

    func reload() {
        guard access != .loading else { return }
        access = .loading
        message = nil
        if AppTestMode.enabled {
            installed = [suggestions[0], suggestions[2]]
            access = .available
            return
        }
        Task {
            do {
                if AuthorizationCenter.shared.authorizationStatus != .approvedWithDataAccess {
                    try await AuthorizationCenter.shared.requestAuthorization(for: .individual)
                }
                guard AuthorizationCenter.shared.authorizationStatus == .approvedWithDataAccess else {
                    access = .unavailable
                    message = "Allow App & Website Usage to show apps installed on this iPhone."
                    return
                }
                var applications = try await FamilyActivityData.shared.installedApplications
                for _ in 0..<3 where applications.isEmpty {
                    try await Task.sleep(for: .milliseconds(500))
                    applications = try await FamilyActivityData.shared.installedApplications
                }
                installed = applications.compactMap { application in
                    guard let id = application.bundleIdentifier else { return nil }
                    let familiar = suggestions.first(where: { $0.id == id })?.name
                        ?? (id == "com.apple.MobileSMS" ? "Messages" : nil)
                    let fallback = id.split(separator: ".").last.map(String.init) ?? id
                    return AppChoice(id, application.localizedDisplayName ?? familiar ?? fallback,
                                     token: application.token)
                }.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
                access = .available
                message = nil
            } catch {
                logger.error("Installed-app lookup failed: \(String(describing: error), privacy: .public)")
                access = .unavailable
#if targetEnvironment(simulator)
                message = "This simulator can’t read installed apps. Rebuild DashBridge with Xcode signing enabled, then try again."
#else
                message = "Couldn’t load apps on this iPhone. Try again in a moment."
#endif
            }
        }
    }
}
