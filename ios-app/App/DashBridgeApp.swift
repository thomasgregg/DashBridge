import SwiftUI

private enum Step: Equatable {
    case welcome, finding, found, checking, pair, sharing, apps
    case car, test, ready, later, help
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
    @AppStorage("dashbridge.completedDeviceID") private var completedDeviceID = ""

    @State private var step: Step = .welcome
    @State private var showingApps = false
    @State private var showingDetails = false
    @State private var testSent = false
    @State private var isPreview = false
    @State private var previewStatus: BridgeStatus?
    @State private var previewAllowed: Set<String> = []
    @State private var reviewingCarStep = false
    @State private var helpReturnStep: Step = .car
    @State private var copiedConnectionDetails = false

    private var status: BridgeStatus? { isPreview ? previewStatus : bridge.status }
    private var selected: Set<String> { isPreview ? previewAllowed : bridge.allowedIDs }
    private var connected: Bool { isPreview || bridge.connected }

    var body: some View {
        NavigationStack {
            content
                .background(Theme.background.ignoresSafeArea())
                .navigationBarTitleDisplayMode(.inline)
                .toolbar(step == .welcome ? .hidden : .visible, for: .navigationBar)
                .toolbar {
                    if step != .welcome && step != .ready {
                        ToolbarItem(placement: .topBarLeading) {
                            Button(action: goBack) {
                                Label("Back", systemImage: "chevron.left")
                            }
                        }
                    }
                }
                .navigationDestination(isPresented: $showingDetails) {
                    details
                        .navigationTitle("Connection details")
                        .navigationBarTitleDisplayMode(.inline)
                        .toolbar(.visible, for: .navigationBar)
                }
                .safeAreaInset(edge: .bottom) { actionBar }
                .sheet(isPresented: $showingApps) {
                    InstalledAppsSheet(catalog: catalog, selected: selected, onToggle: toggle)
                }
        }
        .tint(Theme.accent)
        .onAppear {
            if welcomed {
                step = .finding
                bridge.start()
            }
        }
        .onChange(of: bridge.foundName) { _, name in
            if name != nil && step == .finding { step = .found }
        }
        .onChange(of: bridge.connected) { _, value in
            if value && (step == .found || step == .finding) { step = .checking }
            if !value && !isPreview && step == .checking && bridge.error == nil { step = .finding }
        }
        .onChange(of: bridge.deviceID) { _, value in
            guard let id = value?.uuidString else { return }
            if !completedDeviceID.isEmpty && completedDeviceID != id {
                choseApps = false
                testConfirmed = false
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
            if value == .apps { catalog.loadIfNeeded() }
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
                artwork("dot.radiowaves.left.and.right")
                heading("Looking for DashBridge", "Plug it in nearby. We’ll find it automatically.")
                HStack(spacing: 12) {
                    ProgressView()
                    Text("Looking nearby…").foregroundStyle(Theme.muted)
                }
                if let error = bridge.error { Text(error).foregroundStyle(Theme.muted) }
                Spacer(minLength: 0)
            }
            .padding(24)
        case .found:
            brandedList {
                headerRow("DashBridge found.", "Ready to connect.")
                Section {
                    Label("DashBridge nearby", systemImage: "dot.radiowaves.left.and.right")
                }
            }
        case .checking:
            VStack(spacing: 16) {
                ProgressView()
                Text("Checking your connection…").foregroundStyle(Theme.muted)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        case .pair:
            brandedList {
                headerRow("Connect your iPhone.", "In Settings → Bluetooth, tap Dash Calls and Dash Messages.")
                Section {
                    LabeledContent("Dash Calls", value: status?.phoneCalls == true ? "Connected" : "Waiting")
                    LabeledContent("Dash Messages", value: status?.phoneBluetooth == true ? "Connected" : "Waiting")
                } footer: {
                    Text("Dash Messages may briefly disappear while Dash Calls pairs. Wait for it to reappear; we’ll continue when both connect.")
                }
                if status?.phonePairingOpen == false {
                    Section {
                        Button("Make DashBridge discoverable again") { bridge.send(3) }
                    }
                }
            }
        case .sharing:
            brandedList {
                headerRow("Dash Messages is connected.", "If iPhone asks to share notifications, tap Allow. DashBridge will continue when messages are ready.")
                Section {
                    LabeledContent("Bluetooth pairing", value: "Connected")
                    LabeledContent("iPhone permission", value: bridge.notificationPermission == true ? "Allowed" : bridge.notificationPermission == false ? "Not allowed" : "Waiting")
                    LabeledContent("Notification link", value: "Waiting")
                } footer: {
                    Text("A Bluetooth connection alone does not mean notifications are available yet.")
                }
            }
        case .apps:
            appChoices
        case .car:
            brandedList {
                headerRow("Connect your Tesla.", "When you’re parked, add Dash Tesla on your Tesla’s Bluetooth screen.")
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
            intro(symbol: "message", title: testSent ? "Did it appear?" : "Try a message.",
                  subtitle: testSent ? "Check the Tesla screen." : "DashBridge can send a test message to your Tesla.")
        case .ready:
            brandedList {
                headerRow("Ready to go.", "DashBridge is working. You can close the app.")
                Section {
                    LabeledContent("iPhone", value: status?.notifications == true ? "Connected" : "Not nearby")
                    LabeledContent("Tesla", value: status?.teslaMessages == true ? "Connected" : "Not nearby")
                }
                Section("Notifications") {
                    LabeledContent("Allowed apps", value: selectedNames)
                    Button("Change apps") { step = .apps }
                }
                Section {
                    Button("Connection help") {
                        helpReturnStep = .ready
                        step = .help
                    }
                }
            }
        case .later:
            brandedList {
                headerRow("Finish in the car.", "Your app choices are saved. Connect Dash Tesla when you’re parked.")
                Section {
                    Label("Your app choices are saved", systemImage: "checkmark.circle")
                }
                Section {
                    Button("Change apps") { step = .apps }
                }
            }
        case .help:
            brandedList {
                if helpReturnStep == .checking {
                    headerRow("Couldn't check DashBridge yet.", bridge.error ?? "The app couldn't reach DashBridge yet.")
                    Section {
                        Label("Tap Check again below. No reset or re-pairing is needed.", systemImage: "arrow.clockwise")
                    }
                } else {
                    headerRow("Connection help.", bridge.error ?? "Check that Dash Tesla is selected on the Tesla Bluetooth screen.")
                    Section {
                        LabeledContent("iPhone", value: status?.notifications == true ? "Connected" : "Check connection")
                        LabeledContent("Tesla", value: status?.teslaMessages == true ? "Connected" : "Not connected")
                    }
                }
                Section {
                    Button("Connection details") { showingDetails = true }
                }
            }
        }
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
        .accessibilityHidden(true)
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

    private func heading(_ title: String, _ subtitle: String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title)
                .font(.system(size: 35, weight: .semibold, design: .rounded))
                .tracking(-1.4)
                .foregroundStyle(Theme.ink)
            Text(subtitle)
                .font(.body)
                .foregroundStyle(Theme.muted)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func intro(symbol: String, title: String, subtitle: String) -> some View {
        VStack(alignment: .leading, spacing: 28) {
            artwork(symbol)
            heading(title, subtitle)
            Spacer(minLength: 0)
        }
        .padding(24)
    }

    private func headerRow(_ title: String, _ subtitle: String) -> some View {
        heading(title, subtitle)
            .padding(.top, 10)
            .padding(.bottom, 18)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 0, leading: 0, bottom: 0, trailing: 0))
            .listRowSeparator(.hidden)
    }

    private func brandedList<Content: View>(@ViewBuilder _ rows: () -> Content) -> some View {
        List(content: rows)
            .listSectionSpacing(.compact)
            .scrollContentBackground(.hidden)
            .background(Theme.background)
    }

    private var appChoices: some View {
        brandedList {
            headerRow("Choose your apps.", "New notifications from selected apps can appear as messages in your Tesla.")
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
            }
            if !isPreview && connected && !bridge.policyLoaded {
                Section {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text("Loading saved choices…")
                    }
                }
            }
        }
    }

    private var details: some View {
        brandedList {
            if let status {
                Section("iPhone") {
                    LabeledContent("Bluetooth", value: status.phoneBluetooth ? "Connected" : "Disconnected")
                    LabeledContent("Notification sharing", value: status.notifications ? "Ready" : "Not ready")
                }
                Section("DashBridge") {
                    LabeledContent("Internal link", value: status.internalLink ? "Ready" : "Not responding")
                }
                Section("Tesla") {
                    LabeledContent("Bluetooth", value: status.teslaCalls ? "Connected" : "Disconnected")
                }
            } else {
                Section {
                    LabeledContent("Last step", value: bridge.connectionStage)
                    if let error = bridge.error { Text(error) }
                } header: {
                    Text("iPhone setup")
                } footer: {
                    Text("The app couldn't check the iPhone, DashBridge, or Tesla connections yet.")
                }
            }
            Section {
                Button(copiedConnectionDetails ? "Details copied" : "Copy details") {
                    let summary = ["DashBridge connection details",
                                   "Last step: \(bridge.connectionStage)",
                                   "Problem: \(bridge.error ?? "None")",
                                   "Status received: \(status == nil ? "No" : "Yes")"]
                    UIPasteboard.general.string = summary.joined(separator: "\n")
                    copiedConnectionDetails = true
                }
            }
        }
    }

    @ViewBuilder
    private var actionBar: some View {
        if step == .welcome || step == .finding || step == .found || step == .apps ||
            step == .car || step == .test || step == .later || step == .help {
            VStack(spacing: 8) {
                switch step {
                case .welcome:
                    primary("Get started") {
                        welcomed = true
                        step = .finding
                        bridge.start()
                    }
#if targetEnvironment(simulator)
                    Button("Preview without hardware") {
                        isPreview = true
                        step = .found
                    }
#endif
                case .finding:
                    if bridge.error != nil {
                        primary("Try again") { bridge.start() }
                    }
                case .found:
                    primary(connected ? "Continue" : "Connect") {
                        if isPreview {
                            previewStatus = BridgeStatus(bits: 0b0000_1111)
                            step = .apps
                        } else if connected {
                            step = status?.phoneCalls == true && status?.notifications == true
                                ? .apps : .checking
                        } else {
                            step = .checking
                            bridge.connectFound()
                        }
                    }
                case .apps:
                    primary("Continue") {
                        choseApps = true
                        if testConfirmed { step = .ready }
                        else if status?.teslaMessages == true && status?.teslaCalls == true { step = .test }
                        else {
                            step = .car
                            if !isPreview { bridge.send(4) }
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
                    Button("Do this later") { step = .later }
                case .test:
                    if testSent {
                        primary("I can see it") {
                            testConfirmed = true
                            step = .ready
                        }
                        Button("Nothing appeared") {
                            helpReturnStep = .test
                            step = .help
                        }
                    } else {
                        primary("Send test message") {
                            if !isPreview { bridge.send(5) }
                            testSent = true
                        }
                    }
                case .later:
                    primary("Connect Tesla") {
                        step = .car
                        if !isPreview { bridge.send(4) }
                    }
                case .help:
                    primary("Check again") {
                        bridge.error = nil
                        if testConfirmed { step = .ready }
                        else if status?.teslaMessages == true { step = .test }
                        else if status != nil { step = .car }
                        else { step = .finding; bridge.retry() }
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

    private func goBack() {
        switch step {
        case .welcome:
            break
        case .finding:
            step = .welcome
        case .found:
            step = connected ? .welcome : .finding
            if !connected { bridge.scan() }
        case .checking, .pair, .sharing, .apps:
            step = .found
        case .car:
            reviewingCarStep = false
            step = .apps
        case .test, .later:
            reviewingCarStep = true
            step = .car
        case .help:
            if helpReturnStep == .checking {
                step = .finding
                bridge.retry()
            } else { step = helpReturnStep }
        case .ready:
            step = .test
        }
    }

    private var selectedNames: String {
        let byID = Dictionary((catalog.installed + catalog.suggested).map { ($0.id, $0.name) },
                              uniquingKeysWith: { first, _ in first })
        let names = selected.map { byID[$0] ?? $0.split(separator: ".").last.map(String.init) ?? $0 }
        return names.isEmpty ? "None" : names.sorted().joined(separator: " · ")
    }

    private func route(for value: BridgeStatus) {
        if step == .checking || step == .pair || step == .sharing {
            if value.phoneCalls && value.notifications {
                step = choseApps ? (testConfirmed ? .ready
                       : (value.teslaMessages && value.teslaCalls ? .test : .car)) : .apps
                return
            }
            step = value.phoneCalls && value.phoneBluetooth ? .sharing : .pair
        }
        if ((step == .car && !reviewingCarStep) || step == .later)
            && value.teslaMessages && value.teslaCalls {
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
