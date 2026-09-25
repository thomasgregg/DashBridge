import SwiftUI
import UserNotifications

private enum Step: Equatable {
    case welcome, finding, checking, pair, sharing, apps
    case car, test, ready, help
}

private enum Theme {
    static let accent = Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark
        ? UIColor(red: 0.48, green: 0.81, blue: 0.65, alpha: 1)
        : UIColor(red: 0.09, green: 0.42, blue: 0.33, alpha: 1) })
    static let background = Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark
        ? UIColor(red: 0.063, green: 0.106, blue: 0.098, alpha: 1)
        : UIColor(red: 0.961, green: 0.969, blue: 0.961, alpha: 1) })
    static let surface = Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark
        ? UIColor(red: 0.110, green: 0.165, blue: 0.153, alpha: 1)
        : .white })
    static let ink = Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark
        ? UIColor(red: 0.933, green: 0.961, blue: 0.941, alpha: 1)
        : UIColor(red: 0.098, green: 0.192, blue: 0.173, alpha: 1) })
    static let muted = Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark
        ? UIColor(red: 0.663, green: 0.725, blue: 0.694, alpha: 1)
        : UIColor(red: 0.424, green: 0.486, blue: 0.459, alpha: 1) })
    static let soft = Color(uiColor: UIColor { traits in traits.userInterfaceStyle == .dark
        ? UIColor(red: 0.141, green: 0.263, blue: 0.200, alpha: 1)
        : UIColor(red: 0.918, green: 0.961, blue: 0.937, alpha: 1) })
}

// The bridge mark is the same 64×64 drawing used by web/favicon.svg and the README banner.
private struct DashBridgeMark: View {
    var body: some View {
        Canvas { context, size in
            let scale = min(size.width, size.height) / 64
            let bounds = CGRect(x: 0, y: 0, width: 64 * scale, height: 64 * scale)
            context.fill(Path(roundedRect: bounds, cornerRadius: 16 * scale),
                         with: .color(Color(red: 16/255, green: 43/255, blue: 50/255)))
            var bridge = Path()
            bridge.move(to: CGPoint(x: 12, y: 46))
            bridge.addLine(to: CGPoint(x: 52, y: 46))
            bridge.move(to: CGPoint(x: 18, y: 46))
            bridge.addLine(to: CGPoint(x: 18, y: 18))
            bridge.addCurve(to: CGPoint(x: 46, y: 18),
                            control1: CGPoint(x: 18, y: 38), control2: CGPoint(x: 46, y: 38))
            bridge.addLine(to: CGPoint(x: 46, y: 46))
            bridge.move(to: CGPoint(x: 26, y: 34))
            bridge.addLine(to: CGPoint(x: 26, y: 46))
            bridge.move(to: CGPoint(x: 38, y: 34))
            bridge.addLine(to: CGPoint(x: 38, y: 46))
            context.stroke(bridge.applying(CGAffineTransform(scaleX: scale, y: scale)),
                           with: .color(Color(red: 121/255, green: 228/255, blue: 199/255)),
                           style: StrokeStyle(lineWidth: 4 * scale, lineCap: .round))
        }
        .accessibilityLabel("DashBridge bridge logo")
    }
}

@main
struct DashBridgeApp: App {
    var body: some Scene {
        WindowGroup { SetupView() }
    }
}

private struct SetupView: View {
    @StateObject private var bridge = BridgeBluetooth()
    @StateObject private var catalog = AppCatalog()
    @AppStorage("dashbridge.welcomed") private var welcomed = false
    @AppStorage("dashbridge.choseApps") private var choseApps = false
    @AppStorage("dashbridge.testConfirmed") private var testConfirmed = false
    @AppStorage("dashbridge.teslaDeferred") private var teslaDeferred = false
    @AppStorage("dashbridge.completedDeviceID") private var completedDeviceID = ""

    @State private var step: Step = .welcome
    @State private var initialized = false
    @State private var showingApps = false
    @State private var isPreview = false
    @State private var previewStatus: BridgeStatus?
    @State private var previewAllowed: Set<String> = []
    @State private var reviewingCarStep = false
    @State private var appsReturnStep: Step = .welcome
    @State private var helpReturnStep: Step = .car
    @State private var copiedConnectionDetails = false
    @State private var checkingStartedAt: Date?
    @State private var connectionRequestPending = false
    @State private var testNotificationPending = false
    @State private var testNotificationMessage: String?

    private var checkingDuration: TimeInterval { AppTestMode.checkingPreview ? 3 : 25 }

    private var status: BridgeStatus? { isPreview ? previewStatus : bridge.status }
    private var selected: Set<String> { isPreview ? previewAllowed : bridge.allowedIDs }
    private var connected: Bool { isPreview || bridge.connected }
    private var showsBackButton: Bool {
        step != .welcome && !(step == .ready && testConfirmed)
    }

    var body: some View {
        NavigationStack {
            content
                .background(Theme.background.ignoresSafeArea())
                .navigationBarTitleDisplayMode(.inline)
                .toolbar(showsBackButton ? .visible : .hidden, for: .navigationBar)
                .toolbar {
                    if showsBackButton {
                        ToolbarItem(placement: .topBarLeading) {
                            Button(action: goBack) {
                                Label("Back", systemImage: "chevron.left")
                            }
                        }
                    }
                    if step == .test {
                        ToolbarItem(placement: .topBarTrailing) {
                            Button("Later") {
                                teslaDeferred = true
                                step = .ready
                            }
                        }
                    }
                }
                .safeAreaInset(edge: .bottom) { actionBar }
                .sheet(isPresented: $showingApps) {
                    InstalledAppsSheet(catalog: catalog, selected: selected, onToggle: toggle)
                }
        }
        .tint(Theme.accent)
        .onAppear {
            guard !initialized else { return }
            initialized = true
            if AppTestMode.enabled {
                welcomed = false
                choseApps = false
                testConfirmed = false
                teslaDeferred = false
                completedDeviceID = ""
                if AppTestMode.savedChoicePreview {
                    previewAllowed = ["net.whatsapp.WhatsAppSMB"]
                }
                if let screen = AppTestMode.screenPreview {
                    showPreviewScreen(screen)
                    return
                }
            }
            if welcomed {
                step = .finding
                bridge.start()
            }
        }
        .onChange(of: bridge.foundName) { _, name in
            if name != nil && step == .finding {
                connectionRequestPending = true
                step = .checking
            }
        }
        .onChange(of: bridge.testWindowGrants) { _, _ in
            guard testNotificationPending else { return }
            Task { await scheduleTestNotification() }
        }
        .onChange(of: bridge.commandError) { _, message in
            if testNotificationPending, let message {
                testNotificationPending = false
                testNotificationMessage = message
            }
        }
        .onChange(of: bridge.connected) { _, value in
            if value && step == .finding { step = .checking }
            if !value && !isPreview && step == .checking && bridge.error == nil { step = .finding }
        }
        .onChange(of: bridge.deviceID) { _, value in
            guard let id = value?.uuidString else { return }
            if !completedDeviceID.isEmpty && completedDeviceID != id {
                choseApps = false
                testConfirmed = false
                teslaDeferred = false
            }
            completedDeviceID = id
        }
        .onChange(of: bridge.status) { _, value in
            if let value { route(for: value) }
        }
        .onChange(of: bridge.error) { _, value in
            if value != nil && step == .checking {
                helpReturnStep = .checking
                step = .help
            }
        }
        .onChange(of: step) { _, value in
            if value == .apps || value == .ready { catalog.loadIfNeeded() }
        }
        .task(id: step) {
            guard step == .checking else {
                checkingStartedAt = nil
                return
            }
            checkingStartedAt = Date()
            if connectionRequestPending {
                connectionRequestPending = false
                // Let SwiftUI present the explanatory screen before asking
                // Core Bluetooth to start an iOS-owned pairing/ANCS prompt.
                // The prompt itself is controlled by iOS and may cover the app.
                do {
                    try await Task.sleep(for: .milliseconds(350))
                } catch { return }
                guard step == .checking else { return }
                bridge.connectFound()
            }
            // A status can arrive while the found screen is still visible.
            // Equatable onChange won't fire again if the next read is identical.
            if let status { route(for: status) }
            guard step == .checking else { return }
            do {
                try await Task.sleep(for: .seconds(checkingDuration))
            } catch { return }
            guard step == .checking else { return }
            bridge.error = connected
                ? "Your iPhone connected to DashBridge, but the app couldn't check its setup yet."
                : "The app couldn't reach DashBridge's setup connection yet."
            helpReturnStep = .checking
            step = .help
        }
    }

    @ViewBuilder
    private var content: some View {
        switch step {
        case .welcome:
            VStack(alignment: .leading, spacing: 0) {
                VStack(alignment: .leading, spacing: 0) {
                    Text("Welcome to").foregroundStyle(Theme.ink)
                    Text("DashBridge.").foregroundStyle(Theme.accent)
                }
                .font(.system(size: 42, weight: .semibold, design: .rounded))
                .tracking(-1.8)
                .padding(.top, 24)
                Text("Bring the iPhone notifications you choose to your Tesla screen.")
                    .font(.system(size: 17))
                    .foregroundStyle(Theme.muted)
                    .padding(.top, 18)
                welcomeArtwork
                    .padding(.top, 44)
                Spacer(minLength: 0)
            }
            .padding(24)
        case .finding:
            VStack(alignment: .leading, spacing: 20) {
                stageHeader("Connecting…",
                            "Looking for DashBridge nearby.",
                            symbol: "dot.radiowaves.left.and.right")
                if bridge.error == nil {
                    Text("Looking nearby…").foregroundStyle(Theme.muted)
                    timeoutBar
                }
                if let error = bridge.error { Text(error).foregroundStyle(Theme.muted) }
                Spacer(minLength: 0)
            }
            .padding(24)
        case .checking:
            VStack(alignment: .leading, spacing: 22) {
                stageHeader("Connecting…",
                            "Keep this app open. iPhone may ask you to pair and share notifications.",
                            symbol: "arrow.triangle.2.circlepath")
                VStack(alignment: .leading, spacing: 12) {
                    Text("Checking your connection…").foregroundStyle(Theme.muted)
                    checkingTimeoutBar
                }
                Spacer(minLength: 0)
            }
            .padding(24)
        case .pair:
            brandedList {
                stageHeaderRow("Connect calls and music.",
                               "In Settings → Bluetooth, tap Dash Calls.",
                               symbol: "phone")
                Section {
                    LabeledContent("Dash Messages", value: "Connected")
                    LabeledContent("Dash Calls", value: status?.phoneCalls == true ? "Connected" : "Waiting")
                } footer: {
                    Text("Dash Messages is ready. We’ll continue automatically when Dash Calls connects.")
                }
                if status?.phonePairingOpen == false {
                    Section {
                        Button("Open pairing for 2 minutes") { bridge.send(3) }
                    }
                }
                if let commandError = bridge.commandError {
                    Section { Text(commandError).foregroundStyle(.red) }
                }
            }
        case .sharing:
            brandedList {
                stageHeaderRow("Approve on iPhone.",
                               "Accept the pairing and notification-sharing prompts. We’ll continue automatically.",
                               symbol: "bell.badge")
                Section {
                    LabeledContent("Dash Messages",
                                   value: status?.phoneBluetooth == true ? "Waiting for approval" : "Connecting")
                    LabeledContent("Notification access",
                                   value: bridge.notificationPermission == false ? "Not allowed"
                                   : bridge.notificationPermission == true ? "Finishing"
                                   : "Waiting for approval")
                } footer: {
                    Text("This is Apple’s system-notification sharing permission, not permission for the DashBridge app to send alerts.")
                }
            }
        case .apps:
            appChoices
        case .car:
            brandedList {
                stageHeaderRow("Connect your Tesla.",
                               "When you’re parked, add Dash Tesla on your Tesla’s Bluetooth screen.",
                               symbol: "car.side")
                Section {
                    LabeledContent("Dash Tesla", value: status?.teslaMessages == true ? "Connected" : "Waiting for car")
                } footer: {
                    Text("We’ll continue when it connects.")
                }
                if status?.internalLink == false {
                    Section { Label("DashBridge needs attention", systemImage: "exclamationmark.triangle") }
                }
            }
        case .test:
            VStack(alignment: .leading, spacing: 28) {
                stageHeader(isPreview ? "Preview the final step." : "Try a notification.",
                            isPreview
                            ? "Send a test, then continue."
                            : "Send a test, then check your Tesla screen.",
                            symbol: "message")
                if let testNotificationMessage {
                    Text(testNotificationMessage).foregroundStyle(Theme.muted)
                }
                if !isPreview {
                    Button("Didn’t see it on Tesla?") {
                        helpReturnStep = .test
                        step = .help
                    }
                    .font(.subheadline)
                }
                Spacer(minLength: 0)
            }
            .padding(24)
        case .ready:
            brandedList {
                completionHeader
                Section("Connections") {
                    LabeledContent("iPhone", value: status?.notifications == true ? "Connected" : "Not nearby")
                    LabeledContent("Tesla", value: status?.teslaMessages == true ? "Connected" : "Not nearby")
                }
                Section("Allowed apps") {
                    if !isPreview && !bridge.policyLoaded {
                        Text("Loading saved choices…")
                            .foregroundStyle(Theme.muted)
                    } else if selectedChoices.isEmpty {
                        Text("No apps selected")
                            .foregroundStyle(Theme.muted)
                    } else {
                        ForEach(selectedChoices) { choice in
                            if let token = choice.token {
                                Label(token).labelStyle(.titleAndIcon)
                            } else {
                                Text(choice.name)
                            }
                        }
                    }
                    Button(selected.isEmpty ? "Choose apps" : "Change apps") {
                        appsReturnStep = .ready
                        step = .apps
                    }
                }
                Section {
                    Button("Connection help") {
                        helpReturnStep = .ready
                        step = .help
                    }
                }
            }
            .contentMargins(.top, 0, for: .scrollContent)
        case .help:
            if helpReturnStep == .checking && status == nil {
                retryView
            } else {
                brandedList {
                    listHeaderRow("Connection status.",
                                  bridge.error ?? "Live status reported by DashBridge.")

                    if let status {
                        Section("iPhone") {
                            LabeledContent("Bluetooth", value: status.phoneBluetooth ? "Connected" : "Disconnected")
                            LabeledContent("Notification sharing", value: status.notifications ? "Ready" : "Not ready")
                            LabeledContent("Calls", value: status.phoneCalls ? "Connected" : "Disconnected")
                        }
                        Section("Tesla") {
                            LabeledContent("Messages", value: status.teslaMessages ? "Connected" : "Disconnected")
                            LabeledContent("Message transport", value: status.teslaTransport ? "Ready" : "Not ready")
                            LabeledContent("Message sync", value: status.teslaSync ? "Ready" : "Not ready")
                            LabeledContent("Calls", value: status.teslaCalls ? "Connected" : "Disconnected")
                        }
                        Section("DashBridge") {
                            LabeledContent("Internal board link", value: status.internalLink ? "Ready" : "Not responding")
                        }
                    } else {
                        Section("Connection check") {
                            LabeledContent("Last step", value: bridge.connectionStage)
                            Text("DashBridge has not reported its connection status yet.")
                                .foregroundStyle(Theme.muted)
                        }
                    }

                    Section {
                        Button(copiedConnectionDetails ? "Diagnostics copied" : "Copy diagnostics") {
                            UIPasteboard.general.string = connectionDiagnostics
                            copiedConnectionDetails = true
                        }
                    }
                }
            }
        }
    }

    private var retryView: some View {
        VStack(alignment: .leading, spacing: 22) {
            stageHeader("Connection interrupted.",
                        "Keep DashBridge powered and nearby, then try again.",
                        symbol: "arrow.clockwise",
                        accessibilityLabel: "Retry connection")
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding(24)
    }

    private func artwork(_ symbol: String) -> some View {
        ZStack {
            RoundedRectangle(cornerRadius: 24)
                .fill(Theme.soft)
            Circle()
                .strokeBorder(Theme.accent.opacity(0.25), lineWidth: 1)
                .frame(width: 108, height: 108)
            Image(systemName: symbol)
                .font(.system(size: 45, weight: .ultraLight))
                .foregroundStyle(Theme.accent)
        }
        .frame(height: 166)
    }

    @ViewBuilder
    private func stageArtwork(_ symbol: String, accessibilityLabel: String?) -> some View {
        if let accessibilityLabel {
            artwork(symbol)
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(accessibilityLabel)
        } else {
            artwork(symbol)
                .accessibilityHidden(true)
        }
    }

    @ViewBuilder
    private var timeoutBar: some View {
        if let started = bridge.timeoutStartedAt, bridge.timeoutDuration > 0 {
            TimelineView(.periodic(from: .now, by: 0.1)) { timeline in
                let elapsed = timeline.date.timeIntervalSince(started)
                let fraction = min(1, max(0, elapsed / bridge.timeoutDuration))
                ProgressView(value: fraction)
                    .tint(Theme.accent)
                    .accessibilityLabel("Connection check progress")
                    .accessibilityValue("\(Int(fraction * 100)) percent")
            }
        }
    }

    @ViewBuilder
    private var checkingTimeoutBar: some View {
        if let started = checkingStartedAt {
            TimelineView(.periodic(from: .now, by: 0.1)) { timeline in
                let fraction = min(1, max(0, timeline.date.timeIntervalSince(started) / checkingDuration))
                ProgressView(value: fraction)
                    .tint(Theme.accent)
                    .accessibilityLabel("Connection check progress")
                    .accessibilityValue("\(Int(fraction * 100)) percent")
            }
        }
    }

    private var welcomeArtwork: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 28)
                .fill(LinearGradient(colors: [Theme.soft, Theme.background],
                                     startPoint: .topLeading, endPoint: .bottomTrailing))
            RoundedRectangle(cornerRadius: 28)
                .strokeBorder(Theme.accent.opacity(0.13), lineWidth: 1)
            HStack(spacing: 0) {
                Image(systemName: "iphone.gen3")
                    .font(.system(size: 28, weight: .ultraLight))
                    .frame(width: 46, height: 46)
                    .accessibilityLabel("iPhone")
                Rectangle()
                    .fill(Theme.accent.opacity(0.3))
                    .frame(height: 1)
                DashBridgeMark()
                    .frame(width: 94, height: 94)
                Rectangle()
                    .fill(Theme.accent.opacity(0.3))
                    .frame(height: 1)
                Image(systemName: "car.side")
                    .font(.system(size: 28, weight: .ultraLight))
                    .frame(width: 46, height: 46)
                    .accessibilityLabel("Tesla")
            }
            .foregroundStyle(Theme.accent)
            .padding(.horizontal, 24)
        }
        .frame(height: 250)
        .accessibilityElement(children: .combine)
    }

    private func heading(_ title: Text, _ subtitle: String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            title
                .font(.system(size: 35, weight: .semibold, design: .rounded))
                .tracking(-1.4)
                .lineLimit(1)
            Text(subtitle)
                .font(.body)
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func stageHeader(_ title: String, _ subtitle: String, symbol: String,
                             accessibilityLabel: String? = nil) -> some View {
        stageHeader(Text(title).foregroundColor(Theme.ink), subtitle, symbol: symbol,
                    accessibilityLabel: accessibilityLabel)
    }

    private func stageHeader(_ title: Text, _ subtitle: String, symbol: String,
                             accessibilityLabel: String? = nil) -> some View {
        VStack(alignment: .leading, spacing: 22) {
            heading(title, subtitle)
                .frame(height: 96, alignment: .topLeading)
            stageArtwork(symbol, accessibilityLabel: accessibilityLabel)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func stageHeaderRow(_ title: String, _ subtitle: String, symbol: String) -> some View {
        stageHeaderRow(Text(title).foregroundColor(Theme.ink), subtitle, symbol: symbol)
    }

    private func stageHeaderRow(_ title: Text, _ subtitle: String, symbol: String) -> some View {
        listHeaderLayout(stageHeader(title, subtitle, symbol: symbol))
    }

    private func listHeaderRow(_ title: String, _ subtitle: String) -> some View {
        listHeaderLayout(heading(Text(title).foregroundColor(Theme.ink), subtitle))
    }

    private func listHeaderLayout<Content: View>(_ content: Content) -> some View {
        content
            .padding(.top, 24)
            .padding(.bottom, 18)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 0, leading: 0, bottom: 0, trailing: 0))
            .listRowSeparator(.hidden)
    }

    private var completionHeader: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(testConfirmed ? "You're all set!" : "iPhone is ready!")
                .font(.system(size: 35, weight: .semibold, design: .rounded))
                .tracking(-1.4)
                .foregroundStyle(Theme.ink)
                .lineLimit(1)
            Text(testConfirmed
                 ? (status?.notifications == true && status?.teslaMessages == true
                    ? "DashBridge is connected. Enjoy the drive."
                    : "Your last test passed. Connect DashBridge to check it again.")
                 : "Nice—your notification choices are saved. Finish in your Tesla when parked.")
                .font(.body)
                .foregroundStyle(Theme.muted)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.top, 24)
        .padding(.bottom, 18)
        .listRowBackground(Color.clear)
        .listRowInsets(EdgeInsets(top: 0, leading: 0, bottom: 0, trailing: 0))
        .listRowSeparator(.hidden)
    }

    private func brandedList<Content: View>(@ViewBuilder _ rows: () -> Content) -> some View {
        List(content: rows)
            .listSectionSpacing(.compact)
            .contentMargins(.top, 0, for: .scrollContent)
            .scrollContentBackground(.hidden)
            .background(Theme.background)
    }

    private var appChoices: some View {
        brandedList {
            listHeaderRow("Choose your apps.",
                          "New notifications from selected apps can appear as messages in your Tesla.")
            Section {
                switch catalog.access {
                case .notRequested:
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Loading apps…")
                    }
                case .loading:
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Loading apps…")
                    }
                case .unavailable:
                    Text(catalog.message ?? "Couldn’t load installed apps.")
                    Button("Try again") { catalog.reload() }
                case .available:
                    EmptyView()
                }
            }
            if catalog.access == .available {
                if catalog.suggested.isEmpty {
                    Section("Suggested") {
                        Text("No suggested messaging apps are installed.")
                            .foregroundStyle(.secondary)
                    }
                } else {
                    Section("Suggested") {
                        ForEach(catalog.suggested) { choice in
                            appToggle(choice)
                        }
                    }
                }
                Section {
                    Button("Browse more apps", systemImage: "square.grid.2x2") {
                        showingApps = true
                    }
                }
                if !savedChoicesMissingFromAppList.isEmpty {
                    Section {
                        ForEach(savedChoicesMissingFromAppList) { choice in
                            appToggle(choice)
                        }
                    } header: {
                        Text("Saved on DashBridge")
                    } footer: {
                        Text("These saved choices are not in the iPhone app list. Turn one off to remove it from DashBridge.")
                    }
                }
            }
            if !isPreview && connected && !bridge.policyLoaded {
                Section {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Loading saved choices…")
                    }
                }
            }
            if let commandError = bridge.commandError {
                Section { Text(commandError).foregroundStyle(.red) }
            }
        }
    }

    private var connectionDiagnostics: String {
        var lines = ["DashBridge connection diagnostics",
                     "Last step: \(bridge.connectionStage)",
                     "Problem: \(bridge.error ?? "None")",
                     "Status received: \(status == nil ? "No" : "Yes")"]
        if let status {
            lines += ["iPhone Bluetooth: \(status.phoneBluetooth)",
                      "Notification sharing: \(status.notifications)",
                      "iPhone calls: \(status.phoneCalls)",
                      "Tesla messages: \(status.teslaMessages)",
                      "Tesla message transport: \(status.teslaTransport)",
                      "Tesla message sync: \(status.teslaSync)",
                      "Tesla calls: \(status.teslaCalls)",
                      "Internal board link: \(status.internalLink)"]
        }
        return lines.joined(separator: "\n")
    }

    @ViewBuilder
    private var actionBar: some View {
        if step == .welcome || step == .finding || step == .apps ||
            step == .car || step == .test || step == .help || (step == .ready && !testConfirmed) {
            VStack(spacing: 8) {
                switch step {
                case .welcome:
                    primary("Get started") {
                        welcomed = true
                        if connected {
                            step = .checking
                            if let status { route(for: status) }
                        } else {
                            step = .finding
                            bridge.start()
                        }
                    }
#if targetEnvironment(simulator)
                    Button("Preview without hardware") {
                        startPreview()
                    }
#endif
                case .finding:
                    if bridge.error != nil {
                        if bridge.bluetoothReady {
                            primary("Try again") {
                                if connected, let status {
                                    step = .checking
                                    route(for: status)
                                } else {
                                    bridge.retry()
                                }
                            }
                        }
#if targetEnvironment(simulator)
                        Button("Preview without hardware") {
                            startPreview()
                        }
#else
                        if !bridge.bluetoothReady {
                            primary("Try again") { bridge.retry() }
                        }
#endif
                    }
                case .apps:
                    primary("Continue") {
                        choseApps = true
                        if testConfirmed || teslaDeferred { step = .ready }
                        else if status?.teslaMessages == true && status?.teslaCalls == true { step = .test }
                        else {
                            step = .car
                        }
                    }
                    .disabled(catalog.access != .available || (!isPreview && !bridge.policyLoaded))
                case .car:
                    if isPreview {
                        primary("Preview connected car") {
                            previewStatus = BridgeStatus(bits: 0b1111_1111)
                            step = .test
                        }
                    } else if reviewingCarStep && status?.teslaMessages == true && status?.teslaCalls == true {
                        primary("Continue") {
                            reviewingCarStep = false
                            step = .test
                        }
                    }
                    Button("Do this later") {
                        teslaDeferred = true
                        reviewingCarStep = false
                        step = .ready
                    }
                case .test:
                    primary("Send test notification") {
                        Task { await sendTestNotification() }
                    }
                    .disabled(testNotificationPending)
                    Button(isPreview ? "Preview ready screen" : "Message appeared on Tesla") {
                        testConfirmed = true
                        teslaDeferred = false
                        step = .ready
                    }
                case .help:
                    primary(helpReturnStep == .checking && status == nil ? "Try again" : "Done") {
                        bridge.error = nil
                        if testConfirmed { step = .ready }
                        else if status?.teslaMessages == true { step = .test }
                        else if status != nil { step = .car }
                        else { step = .finding; bridge.retry() }
                    }
                case .ready:
                    primary(status?.teslaMessages == true && status?.teslaCalls == true
                            ? "Try a notification" : "Finish in the car") {
                        if status?.teslaMessages == true && status?.teslaCalls == true {
                            step = .test
                        } else {
                            reviewingCarStep = false
                            step = .car
                        }
                    }
                default:
                    EmptyView()
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
            .background(Theme.background)
        }
    }

    private func primary(_ text: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text).frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent)
        .controlSize(.large)
    }

    private func appToggle(_ choice: AppChoice) -> some View {
        Toggle(isOn: Binding(
            get: { selected.contains(choice.id) },
            set: { toggle(choice, $0) }
        )) {
            if let token = choice.token {
                Label(token).labelStyle(.titleAndIcon)
            } else {
                Text(choice.name)
            }
        }
        .accessibilityLabel(choice.name)
    }

    private func toggle(_ choice: AppChoice, _ isOn: Bool) {
        if isPreview {
            if isOn { previewAllowed.insert(choice.id) } else { previewAllowed.remove(choice.id) }
        } else {
            bridge.setAllowed(choice.id, allowed: isOn)
        }
    }

    private func startPreview() {
        isPreview = true
        if AppTestMode.checkingPreview {
            step = .checking
        } else if AppTestMode.cachedStatusPreview {
            previewStatus = BridgeStatus(bits: 0b0000_0101)
            step = .checking
        } else {
            previewStatus = BridgeStatus(bits: 0b0000_1111)
            appsReturnStep = .welcome
            step = .apps
        }
    }

    private func showPreviewScreen(_ screen: String) {
        isPreview = true
        switch screen {
        case "welcome":
            isPreview = false
            step = .welcome
        case "finding":
            isPreview = false
            step = .finding
        case "checking":
            step = .checking
        case "retry":
            bridge.error = "Your iPhone connected to DashBridge, but the app couldn't check its setup yet."
            helpReturnStep = .checking
            step = .help
        case "pair-messages":
            previewStatus = BridgeStatus(bits: 0b0000_0001)
            step = .sharing
        case "pair-calls":
            previewStatus = BridgeStatus(bits: 0b0000_0011)
            step = .pair
        case "sharing":
            previewStatus = BridgeStatus(bits: 0b0000_0101)
            step = .sharing
        case "apps":
            previewStatus = BridgeStatus(bits: 0b0000_1111)
            appsReturnStep = .welcome
            step = .apps
        case "car":
            previewStatus = BridgeStatus(bits: 0b0000_1111)
            previewAllowed = ["net.whatsapp.WhatsApp"]
            step = .car
        case "test":
            previewStatus = BridgeStatus(bits: 0b1111_1111)
            previewAllowed = ["net.whatsapp.WhatsApp"]
            step = .test
        case "ready-iphone":
            previewStatus = BridgeStatus(bits: 0b0000_1111)
            previewAllowed = ["net.whatsapp.WhatsApp"]
            teslaDeferred = true
            step = .ready
        case "ready":
            previewStatus = BridgeStatus(bits: 0b1111_1111)
            previewAllowed = ["net.whatsapp.WhatsApp"]
            testConfirmed = true
            step = .ready
        case "status":
            previewStatus = BridgeStatus(bits: 0b1111_1111)
            helpReturnStep = .ready
            step = .help
        default:
            step = .welcome
        }
    }

    private func goBack() {
        switch step {
        case .welcome:
            break
        case .finding:
            step = .welcome
        case .checking, .pair, .sharing:
            step = .welcome
        case .apps:
            step = appsReturnStep
        case .car:
            reviewingCarStep = false
            step = .apps
        case .test:
            reviewingCarStep = true
            step = .car
        case .help:
            if helpReturnStep == .checking {
                step = .welcome
            } else { step = helpReturnStep }
        case .ready:
            if testConfirmed {
                step = .test
            } else {
                reviewingCarStep = true
                step = .car
            }
        }
    }

    private var selectedChoices: [AppChoice] {
        selected.map { catalog.choice(for: $0) }
            .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    private var savedChoicesMissingFromAppList: [AppChoice] {
        guard catalog.access == .available else { return [] }
        let installedIDs = Set(catalog.installed.map(\.id))
        return selected.subtracting(installedIDs).map { catalog.choice(for: $0) }
            .sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }

    private func sendTestNotification() async {
        testNotificationMessage = nil
        if isPreview {
            if AppTestMode.enabled {
                testNotificationMessage = "Connect DashBridge to send a test notification to your Tesla."
                return
            }
            do {
                let allowed = try await UNUserNotificationCenter.current()
                    .requestAuthorization(options: [.alert, .sound])
                guard allowed else {
                    testNotificationMessage = "Allow DashBridge notifications in iPhone Settings to send a test."
                    return
                }
                testNotificationPending = true
                await scheduleTestNotification(previewOnly: true)
            } catch {
                testNotificationMessage = "The iPhone couldn't prepare a test notification. Try again."
            }
            return
        }
        guard status?.notifications == true, status?.teslaMessages == true,
              status?.teslaSync == true else {
            testNotificationMessage = "Connect your iPhone and Tesla before sending a test."
            return
        }
        do {
            let allowed = try await UNUserNotificationCenter.current()
                .requestAuthorization(options: [.alert, .sound])
            guard allowed else {
                testNotificationMessage = "Allow DashBridge notifications in iPhone Settings to send a test."
                return
            }
            guard step == .test else { return }
            testNotificationPending = true
            testNotificationMessage = "Preparing your test…"
            bridge.send(4)
        } catch {
            testNotificationMessage = "The iPhone couldn't prepare a test notification. Try again."
        }
    }

    private func scheduleTestNotification(previewOnly: Bool = false) async {
        let content = UNMutableNotificationContent()
        content.title = "DashBridge test"
        content.body = previewOnly
            ? "Your iPhone can show the test. Connect DashBridge to send notifications to your Tesla."
            : "If this message appears on your Tesla, notifications are working."
        content.sound = .default
        let request = UNNotificationRequest(identifier: "dashbridge-test-\(UUID().uuidString)",
            content: content, trigger: UNTimeIntervalNotificationTrigger(timeInterval: 12, repeats: false))
        do {
            try await UNUserNotificationCenter.current().add(request)
            testNotificationMessage = previewOnly
                ? "A test will appear on this iPhone shortly. Connect DashBridge to test your Tesla."
                : "Test arriving shortly. Lock your iPhone and watch your Tesla screen."
        } catch {
            testNotificationMessage = "The iPhone couldn't send the test notification. Try again."
        }
        testNotificationPending = false
    }

    private func route(for value: BridgeStatus) {
        if step == .checking || step == .pair || step == .sharing {
            if value.phoneCalls && value.notifications {
                if choseApps {
                    step = testConfirmed || teslaDeferred ? .ready
                        : (value.teslaMessages && value.teslaCalls ? .test : .car)
                } else {
                    appsReturnStep = .welcome
                    step = .apps
                }
                return
            }
            step = value.notifications ? .pair : .sharing
        }
        if step == .car && !reviewingCarStep && value.teslaMessages && value.teslaCalls {
            step = testConfirmed ? .ready : .test
        }
    }
}

private struct InstalledAppsSheet: View {
    @Environment(\.dismiss) private var dismiss
    @ObservedObject var catalog: AppCatalog
    let selected: Set<String>
    let onToggle: (AppChoice, Bool) -> Void
    @State private var search = ""

    private var matches: [AppChoice] {
        catalog.installed.filter {
            search.isEmpty || $0.name.localizedCaseInsensitiveContains(search)
                || $0.id.localizedCaseInsensitiveContains(search)
        }
    }

    var body: some View {
        NavigationStack {
            List {
                if matches.isEmpty {
                    ContentUnavailableView.search(text: search)
                } else {
                    Section {
                        ForEach(matches) { choice in
                            Toggle(isOn: Binding(
                                get: { selected.contains(choice.id) },
                                set: { onToggle(choice, $0) }
                            )) {
                                if let token = choice.token {
                                    Label(token).labelStyle(.titleAndIcon)
                                } else {
                                    Text(choice.name)
                                }
                            }
                            .accessibilityLabel(choice.name)
                        }
                    } footer: {
                        Text("iOS may also show system components here.")
                    }
                }
            }
            .searchable(text: $search, prompt: "Search apps")
            .scrollContentBackground(.hidden)
            .background(Theme.background)
            .navigationTitle("Apps")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .tint(Theme.accent)
    }
}
