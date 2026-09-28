import AccessorySetupKit
import CoreBluetooth
import Foundation
import UIKit

final class BridgeBluetooth: NSObject, ObservableObject {
    static let notFoundMessage = "The app can't find DashBridge. Make sure it's powered and not connected to another iPhone, then try again."
    static let discoveryCompanyIdentifier = ASBluetoothCompanyIdentifier(rawValue: 0x02E5)
    static let discoverySignature = Data([0x44, 0x42, 0x01])

    @Published private(set) var bluetoothReady = false
    @Published private(set) var accessorySetupReady = false
    @Published private(set) var accessoryPickerCancellations = 0
    @Published private(set) var scanning = false
    @Published private(set) var foundName: String?
    @Published private(set) var connected = false
    @Published private(set) var status: BridgeStatus?
    @Published private(set) var allowedIDs: Set<String> = []
    @Published private(set) var policyLoaded = false
    @Published private(set) var deviceID: UUID?
    @Published private(set) var connectionStage = "Looking nearby"
    @Published private(set) var notificationPermission: Bool?
    @Published private(set) var commandError: String?
    @Published private(set) var timeoutStartedAt: Date?
    @Published private(set) var timeoutDuration: TimeInterval = 15
    @Published private(set) var testWindowGrants = 0
    @Published private(set) var phoneSetupSignal: PhoneSetupSignal?
    @Published private(set) var canReplaceAccessoryAuthorization = false
    @Published var error: String?

    private var manager: CBCentralManager?
    private var discovered: CBPeripheral?
    private var peripheral: CBPeripheral?
    private var statusCharacteristic: CBCharacteristic?
    private var policyCharacteristic: CBCharacteristic?
    private var commandCharacteristic: CBCharacteristic?
    private var refreshTimer: Timer?
    private var connectionTimer: Timer?
    private var scanTimer: Timer?
    private var presenceTimer: Timer?
    private var availabilityTimer: Timer?
    private var phoneSetupProgressTimer: Timer?
    private var lastPhoneSetupProgressMask: UInt8?
    private var appActive = true
    private enum RecoveryStage { case none, scanning }
    private var recoveryStage: RecoveryStage = .none
    private var setupSignalSequence = 0
    private var authorizedPeripheralID: UUID?
    private var requiresAccessoryReselection = false
#if !targetEnvironment(simulator)
    private var accessorySession: ASAccessorySession?
    private var accessorySessionReady = false
    private var pickerAccessory: ASAccessory?
    private var pickerPresentationRequested = false
    private var pickerPresented = false
    private var authorizedIDsBeforePicker: Set<UUID> = []
    private var staleAccessoryRemovalInProgress = false
    private var accessoryReplacementRequested = false
#endif
    private struct PendingCommand {
        let command: BridgeCommand
        let payload: Data
    }

    private var pendingCommands: [PendingCommand] = []
    private var writing = false
    private var lastPolicyRead = Date.distantPast
    private let rememberedPeripheralStore: RememberedPeripheralPersisting
    private var incompatibleIdentifiers: Set<UUID> = []

    static func connectionOptions(requiresANCS: Bool) -> [String: Any] {
        var options: [String: Any] = [
            CBConnectPeripheralOptionEnableTransportBridgingKey: true
        ]
        if requiresANCS {
            options[CBConnectPeripheralOptionRequiresANCS] = true
        }
        return options
    }

    static func reconnectRequiresANCS(ancsAuthorized: Bool) -> Bool {
        !ancsAuthorized
    }

    static func accessoryDiscoveryDescriptor() -> ASDiscoveryDescriptor {
        let descriptor = ASDiscoveryDescriptor()
        descriptor.bluetoothCompanyIdentifier = discoveryCompanyIdentifier
        descriptor.bluetoothManufacturerDataBlob = discoverySignature
        descriptor.bluetoothManufacturerDataMask = Data(
            repeating: 0xff, count: discoverySignature.count)
        descriptor.bluetoothRange = .immediate
        descriptor.supportedOptions = [.bluetoothPairingLE, .bluetoothTransportBridging]
        return descriptor
    }

    static func isAuthorizedRecoveryCandidate(authorizedID: UUID?, candidateID: UUID) -> Bool {
        authorizedID == candidateID
    }

    static func preferredAuthorizedIdentifier(currentID: UUID?, rememberedID: UUID?,
                                              candidates: [UUID]) -> UUID? {
        if let currentID, candidates.contains(currentID) { return currentID }
        if let rememberedID, candidates.contains(rememberedID) { return rememberedID }
        return candidates.count == 1 ? candidates[0] : nil
    }

    static func pickerSelectionIdentifier(eventID: UUID?, authorizedBefore: Set<UUID>,
                                          authorizedAfter: [UUID]) -> UUID? {
        if let eventID, authorizedAfter.contains(eventID) { return eventID }
        let newlyAuthorized = authorizedAfter.filter { !authorizedBefore.contains($0) }
        return newlyAuthorized.count == 1 ? newlyAuthorized[0] : nil
    }

    static func phoneSetupProgressMask(_ status: BridgeStatus) -> UInt8 {
        (status.phoneBluetooth ? 1 : 0) |
        (status.notifications ? 2 : 0) |
        (status.phoneCalls ? 4 : 0)
    }

    private func emitPhoneSetup(_ event: PhoneSetupAdapterEvent) {
        setupSignalSequence += 1
        phoneSetupSignal = PhoneSetupSignal(sequence: setupSignalSequence, event: event)
    }

    private func failPhoneSetup(_ failure: PhoneSetupFailure) {
        recoveryStage = .none
        endTimedCheck()
        emitPhoneSetup(.failed(failure))
    }

    init(rememberedPeripheralStore: RememberedPeripheralPersisting = UserDefaultsRememberedPeripheralStore()) {
        self.rememberedPeripheralStore = rememberedPeripheralStore
        super.init()
    }

    func start() {
        if AppTestMode.progressPreview {
            error = nil
            beginTimedCheck(seconds: 15)
            scanTimer?.invalidate()
            scanTimer = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self] _ in
                self?.endTimedCheck()
                self?.error = Self.notFoundMessage
            }
            return
        }
#if !targetEnvironment(simulator)
        startAccessorySession()
#else
        if manager == nil {
            waitForBluetooth()
            manager = CBCentralManager(delegate: self, queue: .main)
        } else if manager?.state == .poweredOn {
            findExistingOrScan()
        } else if let state = manager?.state {
            reportBluetoothState(state)
        }
#endif
    }

#if !targetEnvironment(simulator)
    private func startAccessorySession() {
        if accessorySessionReady {
            accessorySetupReady = true
            return
        }
        guard accessorySession == nil else { return }
        error = nil
        beginTimedCheck(seconds: 15)
        let session = ASAccessorySession()
        accessorySession = session
        session.activate(on: .main) { [weak self] event in
            self?.handleAccessoryEvent(event)
        }
    }

    private func handleAccessoryEvent(_ event: ASAccessoryEvent) {
        switch event.eventType {
        case .activated:
            accessorySessionReady = true
            accessorySetupReady = true
            endTimedCheck()
            refreshAccessoryReplacementCapability()
            if !removeAuthorizedAccessoryForReplacementIfReady() {
                presentAccessoryPickerIfReady()
            }
        case .accessoryAdded, .accessoryChanged:
            refreshAccessoryReplacementCapability()
            if pickerPresented, let accessory = event.accessory,
               accessory.state == .authorized {
                pickerAccessory = accessory
                emitPhoneSetup(.authorized)
            }
        case .migrationComplete:
            refreshAccessoryReplacementCapability()
            if pickerPresented, let session = accessorySession {
                let authorized = session.accessories.filter {
                    $0.state == .authorized && $0.bluetoothIdentifier != nil
                }
                let identifier = Self.pickerSelectionIdentifier(
                    eventID: pickerAccessory?.bluetoothIdentifier,
                    authorizedBefore: authorizedIDsBeforePicker,
                    authorizedAfter: authorized.compactMap(\.bluetoothIdentifier))
                pickerAccessory = identifier.flatMap { selected in
                    authorized.first { $0.bluetoothIdentifier == selected }
                }
            }
        case .pickerDidDismiss:
            pickerPresented = false
            pickerPresentationRequested = false
            let authorized = accessorySession?.accessories.filter {
                $0.state == .authorized && $0.bluetoothIdentifier != nil
            } ?? []
            let selectedIdentifier = Self.pickerSelectionIdentifier(
                eventID: pickerAccessory?.bluetoothIdentifier,
                authorizedBefore: authorizedIDsBeforePicker,
                authorizedAfter: authorized.compactMap(\.bluetoothIdentifier))
            authorizedIDsBeforePicker = []
            pickerAccessory = nil
            guard let selectedIdentifier else {
                accessoryPickerCancellations += 1
                return
            }
            guard let accessory = authorized.first(where: {
                $0.bluetoothIdentifier == selectedIdentifier
            }) else {
                failPhoneSetup(.accessorySetup)
                return
            }
            authorizedPeripheralID = selectedIdentifier
            requiresAccessoryReselection = false
            foundName = accessory.displayName
            emitPhoneSetup(.authorized)
            beginCoreBluetooth()
        case .pickerSetupFailed:
            pickerPresented = false
            pickerPresentationRequested = false
            failPhoneSetup(.accessorySetup)
        case .invalidated:
            accessorySessionReady = false
            accessorySetupReady = false
            accessorySession = nil
            pickerPresented = false
            pickerPresentationRequested = false
            pickerAccessory = nil
            authorizedIDsBeforePicker = []
            staleAccessoryRemovalInProgress = false
            accessoryReplacementRequested = false
            canReplaceAccessoryAuthorization = false
            failPhoneSetup(.accessorySetup)
        case .pickerDidPresent:
            pickerPresented = true
            pickerPresentationRequested = false
            emitPhoneSetup(.pickerPresented)
        case .pickerSetupPairing:
            emitPhoneSetup(.pairing)
        case .pickerSetupBridging:
            emitPhoneSetup(.bridging)
        case .accessoryRemoved:
            refreshAccessoryReplacementCapability()
        case .pickerSetupRename, .accessoryDiscovered, .unknown:
            break
        @unknown default:
            break
        }
    }

    @discardableResult
    private func prepareAuthorizedAccessory() -> Bool {
        guard let session = accessorySession else { return false }
        let accessories = session.accessories.filter {
            $0.state == .authorized && $0.bluetoothIdentifier != nil
        }
        guard !accessories.isEmpty else { return false }
        let identifiers = accessories.compactMap(\.bluetoothIdentifier)
        guard let identifier = Self.preferredAuthorizedIdentifier(
            currentID: authorizedPeripheralID,
            rememberedID: rememberedPeripheralStore.loadIdentifier(),
            candidates: identifiers),
              let accessory = accessories.first(where: { $0.bluetoothIdentifier == identifier }) else {
            failPhoneSetup(.multipleAccessories)
            return true
        }
        authorizedPeripheralID = identifier
        foundName = accessory.displayName
        emitPhoneSetup(.reconnecting)
        beginCoreBluetooth()
        return true
    }

    private func beginCoreBluetooth() {
        if manager == nil {
            waitForBluetooth()
            manager = CBCentralManager(delegate: self, queue: .main)
        } else if manager?.state == .poweredOn {
            findExistingOrScan()
        } else if let state = manager?.state {
            reportBluetoothState(state)
        }
    }

    private func requestAccessoryPicker() {
        guard !pickerPresented else { return }
        pickerPresentationRequested = true
        error = nil
        recoveryStage = .none
        if accessorySession == nil {
            startAccessorySession()
        }
        presentAccessoryPickerIfReady()
    }

    @discardableResult
    private func removeAuthorizedAccessoryForReplacementIfReady() -> Bool {
        guard accessoryReplacementRequested, !staleAccessoryRemovalInProgress,
              accessorySessionReady, let session = accessorySession else { return false }
        let accessories = session.accessories.filter {
            $0.state == .authorized && $0.bluetoothIdentifier != nil
        }
        guard !accessories.isEmpty else {
            accessoryReplacementRequested = false
            requiresAccessoryReselection = false
            requestAccessoryPicker()
            return true
        }
        let identifiers = accessories.compactMap(\.bluetoothIdentifier)
        guard let identifier = Self.preferredAuthorizedIdentifier(
            currentID: authorizedPeripheralID,
            rememberedID: rememberedPeripheralStore.loadIdentifier(),
            candidates: identifiers),
              let accessory = accessories.first(where: { $0.bluetoothIdentifier == identifier }) else {
            accessoryReplacementRequested = false
            failPhoneSetup(.multipleAccessories)
            return true
        }

        staleAccessoryRemovalInProgress = true
        session.removeAccessory(accessory) { [weak self] removalError in
            guard let self else { return }
            self.staleAccessoryRemovalInProgress = false
            self.accessoryReplacementRequested = false
            if let removalError {
                self.pickerPresentationRequested = false
                self.error = removalError.localizedDescription
                self.failPhoneSetup(.accessorySetup)
                return
            }
            self.rememberedPeripheralStore.removeIdentifier(ifMatching: identifier)
            if self.authorizedPeripheralID == identifier {
                self.authorizedPeripheralID = nil
            }
            self.pickerAccessory = nil
            self.requiresAccessoryReselection = false
            self.refreshAccessoryReplacementCapability()
            self.pickerPresentationRequested = true
            self.presentAccessoryPickerIfReady()
        }
        return true
    }

    private func refreshAccessoryReplacementCapability() {
        guard let session = accessorySession else {
            canReplaceAccessoryAuthorization = false
            return
        }
        let identifiers = session.accessories.compactMap { accessory in
            accessory.state == .authorized ? accessory.bluetoothIdentifier : nil
        }
        canReplaceAccessoryAuthorization = Self.preferredAuthorizedIdentifier(
            currentID: authorizedPeripheralID,
            rememberedID: rememberedPeripheralStore.loadIdentifier(),
            candidates: identifiers) != nil
    }

    private func presentAccessoryPickerIfReady() {
        guard pickerPresentationRequested, !pickerPresented,
              !staleAccessoryRemovalInProgress,
              let session = accessorySession, accessorySessionReady else {
            return
        }
        let descriptor = Self.accessoryDiscoveryDescriptor()

        let image = Self.pickerProductImage()
        let display = ASPickerDisplayItem(name: "DashBridge", productImage: image,
                                          descriptor: descriptor)
        var items: [ASPickerDisplayItem] = [display]
        if let remembered = rememberedPeripheralStore.loadIdentifier() {
            let migration = ASMigrationDisplayItem(name: "DashBridge", productImage: image,
                                                   descriptor: descriptor)
            migration.peripheralIdentifier = remembered
            // Keep the ordinary discovery item too. A migration-only picker
            // shows Apple's migration information screen and can strand a
            // freshly-reset board behind a stale Core Bluetooth identifier.
            items.append(migration)
        }
        pickerAccessory = nil
        authorizedIDsBeforePicker = Set(session.accessories.compactMap { accessory in
            accessory.state == .authorized ? accessory.bluetoothIdentifier : nil
        })
        pickerPresented = true
        session.showPicker(for: items) { [weak self] error in
            if let error {
                self?.pickerPresented = false
                self?.pickerPresentationRequested = false
                self?.error = error.localizedDescription
                self?.failPhoneSetup(.accessorySetup)
            }
        }
    }

    private static func pickerProductImage() -> UIImage {
        let format = UIGraphicsImageRendererFormat()
        format.scale = 3
        format.opaque = false
        return UIGraphicsImageRenderer(size: CGSize(width: 180, height: 120), format: format).image { _ in
            let card = UIBezierPath(roundedRect: CGRect(x: 18, y: 8, width: 144, height: 104),
                                    cornerRadius: 24)
            UIColor(red: 0.918, green: 0.961, blue: 0.937, alpha: 1).setFill()
            card.fill()
            let configuration = UIImage.SymbolConfiguration(pointSize: 46, weight: .ultraLight)
            guard let symbol = UIImage(systemName: "powerplug.fill", withConfiguration: configuration)?
                .withTintColor(UIColor(red: 0.09, green: 0.42, blue: 0.33, alpha: 1),
                               renderingMode: .alwaysOriginal) else { return }
            symbol.draw(at: CGPoint(x: 90 - symbol.size.width / 2,
                                    y: 60 - symbol.size.height / 2))
        }
    }
#endif

    private func reportBluetoothState(_ state: CBManagerState) {
        availabilityTimer?.invalidate()
        availabilityTimer = nil
        switch state {
        case .poweredOn:
            endTimedCheck()
            error = nil
        case .poweredOff:
            endTimedCheck()
            error = "Turn on Bluetooth in iPhone Settings, then try again."
#if !targetEnvironment(simulator)
            emitPhoneSetup(.failed(.bluetoothOff))
#endif
        case .unauthorized:
            endTimedCheck()
            error = "Allow Bluetooth for DashBridge in iPhone Settings."
#if !targetEnvironment(simulator)
            emitPhoneSetup(.failed(.bluetoothPermission))
#endif
        case .unsupported:
            endTimedCheck()
#if targetEnvironment(simulator)
            error = "The simulator can't connect to Bluetooth accessories. Use Preview without hardware."
#else
            error = "Bluetooth accessories aren't available on this iPhone."
#endif
#if !targetEnvironment(simulator)
            emitPhoneSetup(.failed(.bluetoothUnavailable))
#endif
        case .unknown, .resetting:
            waitForBluetooth()
        @unknown default:
            endTimedCheck()
            error = "Bluetooth isn't available right now. Try again in a moment."
#if !targetEnvironment(simulator)
            emitPhoneSetup(.failed(.bluetoothUnavailable))
#endif
        }
    }

    private func waitForBluetooth() {
        availabilityTimer?.invalidate()
        error = nil
        beginTimedCheck(seconds: 15)
        availabilityTimer = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self] _ in
            guard let self, self.manager?.state != .poweredOn else { return }
            self.endTimedCheck()
            self.error = "Bluetooth didn't become ready. Try again in a moment."
#if !targetEnvironment(simulator)
            self.emitPhoneSetup(.failed(.bluetoothUnavailable))
#endif
        }
    }

    private func beginTimedCheck(seconds: TimeInterval) {
        timeoutDuration = seconds
        timeoutStartedAt = Date()
    }

    private func endTimedCheck() {
        timeoutStartedAt = nil
    }

    private func updatePhoneSetupDeadline(_ current: BridgeStatus) {
        if current.phoneSetupReady {
            phoneSetupProgressTimer?.invalidate()
            phoneSetupProgressTimer = nil
            lastPhoneSetupProgressMask = nil
            return
        }
        let mask = Self.phoneSetupProgressMask(current)
        guard phoneSetupProgressTimer == nil || mask != lastPhoneSetupProgressMask else { return }
        phoneSetupProgressTimer?.invalidate()
        lastPhoneSetupProgressMask = mask
        phoneSetupProgressTimer = Timer.scheduledTimer(withTimeInterval: 30, repeats: false) {
            [weak self] _ in
            guard let self, self.appActive, let status = self.status,
                  !status.phoneSetupReady,
                  Self.phoneSetupProgressMask(status) == self.lastPhoneSetupProgressMask else { return }
            self.phoneSetupProgressTimer = nil
            self.failPhoneSetup(.incompletePhoneSetup)
        }
    }

    private func findExistingOrScan(clearError: Bool = true, tryRemembered: Bool = true) {
        guard appActive, let manager, manager.state == .poweredOn, !connected,
              peripheral == nil else { return }
        if clearError { error = nil }
#if !targetEnvironment(simulator)
        if let identifier = authorizedPeripheralID,
           let selected = manager.retrievePeripherals(withIdentifiers: [identifier]).first {
            discovered = selected
            foundName = selected.name ?? "DashBridge"
            // Authorization survives a temporary app interruption. Always
            // reconnect the selected accessory; requiring ANCS again is only
            // necessary until iOS reports that notification sharing is ready.
            connectFound(requiresANCS: Self.reconnectRequiresANCS(
                ancsAuthorized: selected.ancsAuthorized))
            return
        }
        // AccessorySetupKit owns first-time discovery. Core Bluetooth is used
        // only after the system has authorized one selected accessory.
        if accessorySession != nil, authorizedPeripheralID == nil {
            foundName = "DashBridge"
            return
        }
#endif
        // A paired ANCS accessory can remain connected to iOS while it is no
        // longer advertising. A scan cannot find it, but Core Bluetooth can
        // attach this app to the existing system connection.
        let connectedPeripherals = manager.retrieveConnectedPeripherals(
            withServices: [BridgeService.service, CBUUID(string: "1800")])
        if let match = connectedPeripherals.first(where: { candidate in
            guard let name = candidate.name else { return false }
            return BridgeService.discoveryNames.contains {
                name.caseInsensitiveCompare($0) == .orderedSame
            }
        }) {
            discovered = match
            if match.ancsAuthorized {
                connectFound(requiresANCS: false)
            } else {
                foundName = match.name ?? "Dash Messages"
            }
            return
        }
        if tryRemembered,
           let id = rememberedPeripheralStore.loadIdentifier(),
           let saved = manager.retrievePeripherals(withIdentifiers: [id]).first {
            // This is only a cached identifier, not evidence that the board
            // is powered or nearby. The connection must prove it is live.
            discovered = saved
            if saved.ancsAuthorized {
                connectFound(requiresANCS: false)
            } else {
                foundName = saved.name ?? "Dash Messages"
            }
            return
        }
        if authorizedPeripheralID != nil {
            recoveryStage = .scanning
            emitPhoneSetup(.recovering)
        }
        scan(clearError: false)
    }

    func scan(clearError: Bool = true) {
        guard appActive, let manager, manager.state == .poweredOn, !connected else { return }
        if clearError { error = nil }
        scanTimer?.invalidate()
        presenceTimer?.invalidate()
        foundName = nil
        discovered = nil
        scanning = true
        connectionStage = "Looking nearby"
        beginTimedCheck(seconds: 15)
        // ANCS service solicitation occupies the advertising packet today.
        // We scan in the foreground, match its local name, and verify our GATT
        // service after connecting. No background scan is claimed.
        manager.stopScan()
        manager.scanForPeripherals(withServices: nil,
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        scanTimer = Timer.scheduledTimer(withTimeInterval: 15, repeats: false) { [weak self] _ in
            guard let self, self.scanning else { return }
            self.manager?.stopScan()
            self.scanning = false
            self.endTimedCheck()
            if self.recoveryStage == .scanning {
                self.requiresAccessoryReselection = true
                self.failPhoneSetup(.unavailable)
            } else {
                self.error = Self.notFoundMessage
            }
        }
    }

    func connectFound(requiresANCS: Bool = true) {
        guard appActive, let discovered, let manager else {
#if !targetEnvironment(simulator)
            if prepareAuthorizedAccessory() { return }
            requestAccessoryPicker()
#endif
            return
        }
        scanTimer?.invalidate()
        scanTimer = nil
        presenceTimer?.invalidate()
        presenceTimer = nil
        scanning = false
        manager.stopScan()
        beginTimedCheck(seconds: 20)
        peripheral = discovered
        connectionStage = "Opening Bluetooth link"
        notificationPermission = nil
        emitPhoneSetup(.connecting)
        discovered.delegate = self
        // Ask iOS to offer ANCS notification permission during pairing and to
        // activate the authorized Dash Calls Classic profiles over the same
        // system-owned setup. Declaring bridging in AccessorySetupKit only
        // authorizes it; this Core Bluetooth option performs it.
        manager.connect(discovered, options: Self.connectionOptions(requiresANCS: requiresANCS))
        scheduleConnectionTimeout()
    }

    private func scheduleConnectionTimeout() {
        connectionTimer?.invalidate()
        connectionTimer = Timer.scheduledTimer(withTimeInterval: 20, repeats: false) { [weak self] _ in
            guard let self, self.appActive, self.status == nil else { return }
            self.recoverAuthorizedConnection()
        }
    }

    private func recoverAuthorizedConnection() {
        guard appActive, authorizedPeripheralID != nil else {
            requiresAccessoryReselection = true
            failPhoneSetup(.unavailable)
            return
        }
        guard recoveryStage == .none else {
            requiresAccessoryReselection = true
            failPhoneSetup(.unavailable)
            return
        }
        recoveryStage = .scanning
        emitPhoneSetup(.recovering)
        if let peripheral { manager?.cancelPeripheralConnection(peripheral) }
        clearConnection()
        scan(clearError: false)
    }

    func setAppActive(_ active: Bool) {
        appActive = active
        if !active {
            // Time spent in Apple's setup UI or Bluetooth Settings must not
            // consume the foreground connection deadline.
            connectionTimer?.invalidate()
            connectionTimer = nil
            scanTimer?.invalidate()
            scanTimer = nil
            presenceTimer?.invalidate()
            presenceTimer = nil
            availabilityTimer?.invalidate()
            availabilityTimer = nil
            phoneSetupProgressTimer?.invalidate()
            phoneSetupProgressTimer = nil
            manager?.stopScan()
            scanning = false
            endTimedCheck()
            return
        }

        guard manager?.state == .poweredOn else { return }
        error = nil
        if connected {
            if let status { updatePhoneSetupDeadline(status) }
            if status == nil { scheduleConnectionTimeout() }
            refresh()
        } else {
            if recoveryStage == .scanning {
                emitPhoneSetup(.recovering)
                scan(clearError: false)
            } else {
                findExistingOrScan(tryRemembered: false)
            }
        }
    }

    func retry() {
        error = nil
        recoveryStage = .none
        requiresAccessoryReselection = false
#if !targetEnvironment(simulator)
        if manager == nil {
            if !prepareAuthorizedAccessory() { requestAccessoryPicker() }
            return
        }
#endif
        if let state = manager?.state, state != .poweredOn {
            reportBluetoothState(state)
            return
        }
        if let peripheral, peripheral.state != .disconnected {
            manager?.cancelPeripheralConnection(peripheral)
        }
        clearConnection()
        findExistingOrScan(tryRemembered: false)
    }

    func replaceAccessoryAuthorization() {
        error = nil
        recoveryStage = .none
        requiresAccessoryReselection = true
#if !targetEnvironment(simulator)
        accessoryReplacementRequested = true
        // Stop the old Core Bluetooth attachment before asking iOS to remove
        // its authorization. Its disconnect callback must not start a recovery
        // scan that races the replacement picker.
        if let peripheral, peripheral.state != .disconnected {
            manager?.cancelPeripheralConnection(peripheral)
        }
        clearConnection()
        if accessorySession == nil { startAccessorySession() }
        _ = removeAuthorizedAccessoryForReplacementIfReady()
#else
        failPhoneSetup(.accessorySetup)
#endif
    }

    func send(_ command: BridgeCommand) {
        let payload = command.payload
        guard let peripheral, commandCharacteristic != nil else {
            commandError = BridgeError.commandUnavailable.localizedDescription
            return
        }
        guard payload.count <= peripheral.maximumWriteValueLength(for: .withResponse) else {
            commandError = BridgeError.commandTooLong.localizedDescription
            return
        }
        pendingCommands.append(PendingCommand(command: command, payload: payload))
        sendNext()
    }

    func setAllowed(_ id: String, allowed: Bool) {
        guard !id.isEmpty else { return }
        guard connected, commandCharacteristic != nil, policyLoaded else {
            commandError = BridgeError.commandUnavailable.localizedDescription
            return
        }
        if allowed { allowedIDs.insert(id) } else { allowedIDs.remove(id) }
        send(allowed ? .allowApplication(id) : .denyApplication(id))
    }

    private func sendNext() {
        guard !writing, !pendingCommands.isEmpty,
              let peripheral, let commandCharacteristic else { return }
        writing = true
        peripheral.writeValue(pendingCommands[0].payload, for: commandCharacteristic, type: .withResponse)
    }

    private func refresh() {
        guard let peripheral, let statusCharacteristic else { return }
        peripheral.readValue(for: statusCharacteristic)
        // Do not ask for an encrypted app-policy read during initial setup.
        // The app-owned ANCS handshake must finish first; otherwise this read
        // can start a competing security exchange.
        if let policyCharacteristic, status?.notifications == true,
           Date().timeIntervalSince(lastPolicyRead) > 15 {
            lastPolicyRead = Date()
            peripheral.readValue(for: policyCharacteristic)
        }
    }

    private func clearConnection() {
        let testWasPending = pendingCommands.contains { $0.command == .beginNotificationTest }
        connectionTimer?.invalidate()
        connectionTimer = nil
        refreshTimer?.invalidate()
        refreshTimer = nil
        scanTimer?.invalidate()
        scanTimer = nil
        presenceTimer?.invalidate()
        presenceTimer = nil
        phoneSetupProgressTimer?.invalidate()
        phoneSetupProgressTimer = nil
        lastPhoneSetupProgressMask = nil
        scanning = false
        foundName = nil
        discovered = nil
        connected = false
        status = nil
        statusCharacteristic = nil
        policyCharacteristic = nil
        commandCharacteristic = nil
        policyLoaded = false
        pendingCommands.removeAll()
        writing = false
        commandError = testWasPending ? "DashBridge disconnected before the test could start. Try again when it reconnects." : nil
        endTimedCheck()
        peripheral = nil
    }
}

extension BridgeBluetooth: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothReady = central.state == .poweredOn
        if bluetoothReady {
            availabilityTimer?.invalidate()
            availabilityTimer = nil
            findExistingOrScan()
        }
        else {
            scanning = false
            clearConnection()
            reportBluetoothState(central.state)
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        guard BridgeService.discoveryNames.contains(where: {
            name.caseInsensitiveCompare($0) == .orderedSame
        }), !incompatibleIdentifiers.contains(peripheral.identifier) else { return }
        if recoveryStage == .scanning,
           !Self.isAuthorizedRecoveryCandidate(authorizedID: authorizedPeripheralID,
                                               candidateID: peripheral.identifier) {
            return
        }
        guard discovered == nil || discovered?.identifier == peripheral.identifier else { return }
        discovered = peripheral
        foundName = name
        scanTimer?.invalidate()
        scanTimer = nil
        endTimedCheck()
        // Keep listening while the Connect screen is shown. A cached result
        // must never stay "nearby" after its advertisements disappear.
        presenceTimer?.invalidate()
        presenceTimer = Timer.scheduledTimer(withTimeInterval: 8, repeats: false) { [weak self] _ in
            guard let self, self.peripheral == nil else { return }
            self.foundName = nil
            self.discovered = nil
            self.scan(clearError: false)
        }
        if recoveryStage == .scanning {
            connectFound(requiresANCS: Self.reconnectRequiresANCS(
                ancsAuthorized: peripheral.ancsAuthorized))
        } else if rememberedPeripheralStore.loadIdentifier() == peripheral.identifier,
           peripheral.ancsAuthorized {
            connectFound(requiresANCS: false)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connected = true
        notificationPermission = peripheral.ancsAuthorized ? true : nil
        connectionStage = "Finding setup service"
        error = nil
        emitPhoneSetup(.connecting)
        peripheral.delegate = self
        peripheral.discoverServices([BridgeService.service])
    }

    func centralManager(_ central: CBCentralManager,
                        didUpdateANCSAuthorizationFor peripheral: CBPeripheral) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        notificationPermission = peripheral.ancsAuthorized
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        clearConnection()
        if appActive { recoverAuthorizedConnection() }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        guard self.peripheral?.identifier == peripheral.identifier else { return }
        clearConnection()
        if appActive, !incompatibleIdentifiers.contains(peripheral.identifier) {
            recoverAuthorizedConnection()
        }
    }
}

extension BridgeBluetooth: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if error != nil { recoverAuthorizedConnection(); return }
        guard let service = peripheral.services?.first(where: { $0.uuid == BridgeService.service }) else {
            incompatibleIdentifiers.insert(peripheral.identifier)
            failPhoneSetup(.incompatibleFirmware)
            manager?.cancelPeripheralConnection(peripheral)
            return
        }
        connectionStage = "Reading setup service"
        peripheral.discoverCharacteristics([BridgeService.status, BridgeService.policy, BridgeService.command], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if error != nil { recoverAuthorizedConnection(); return }
        for characteristic in service.characteristics ?? [] {
            switch characteristic.uuid {
            case BridgeService.status: statusCharacteristic = characteristic
            case BridgeService.policy: policyCharacteristic = characteristic
            case BridgeService.command: commandCharacteristic = characteristic
            default: break
            }
        }
        guard statusCharacteristic != nil, policyCharacteristic != nil,
              commandCharacteristic != nil else {
            failPhoneSetup(.incompleteService)
            return
        }
        deviceID = peripheral.identifier
        connectionStage = "Checking DashBridge"
        refresh()
        refreshTimer?.invalidate()
        refreshTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            self?.refresh()
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        if error != nil {
            if characteristic.uuid == BridgeService.status { recoverAuthorizedConnection() }
            return
        }
        guard let data = characteristic.value else { return }
        if characteristic.uuid == BridgeService.status {
            do {
                status = try BridgeStatus(data)
                recoveryStage = .none
                if let status { updatePhoneSetupDeadline(status) }
                if status?.notifications == true {
                    rememberedPeripheralStore.saveIdentifier(peripheral.identifier)
                }
                connectionStage = "Ready"
                connectionTimer?.invalidate()
                connectionTimer = nil
                endTimedCheck()
            }
            catch { failPhoneSetup(.incompatibleFirmware) }
        } else if characteristic.uuid == BridgeService.policy {
            let text = String(decoding: data, as: UTF8.self)
            allowedIDs = Set(text.split(separator: "\n").map(String.init))
            policyLoaded = true
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard self.peripheral === peripheral else { return }
        writing = false
        let command = pendingCommands.first?.command
        if !pendingCommands.isEmpty { pendingCommands.removeFirst() }
        if error != nil {
            commandError = command?.operation == 1 || command?.operation == 2
                ? "Couldn't save this app choice. Please try again."
                : command == .beginNotificationTest
                ? "DashBridge couldn't start the test. Its firmware may be unsupported, or the Tesla connection isn't ready."
                : "DashBridge couldn't complete that action. Please try again."
        } else {
            commandError = nil
            if command == .beginNotificationTest { testWindowGrants += 1 }
        }
        if let policyCharacteristic { peripheral.readValue(for: policyCharacteristic) }
        sendNext()
    }
}
