import Combine
import Foundation

enum SetupStep: Equatable {
    case welcome, finding, checking, apps
    case car, test, ready, help
}

enum PhoneSetupPhase: Equatable {
    case inactive
    case preparing
    case awaitingUser
    case systemSetup
    case verifying
    case recovering
    case complete
}

enum PhoneSetupFailure: Equatable {
    case accessorySetup
    case bluetoothOff
    case bluetoothPermission
    case bluetoothUnavailable
    case unavailable
    case incompatibleFirmware
    case incompleteService
}

enum PhoneSetupAdapterEvent: Equatable {
    case pickerPresented
    case pairing
    case bridging
    case authorized
    case connecting
    case recovering
    case failed(PhoneSetupFailure)
}

struct PhoneSetupSignal: Equatable {
    let sequence: Int
    let event: PhoneSetupAdapterEvent
}

enum SetupPreviewScreen: String {
    case welcome, finding, checking, retry
    case connectIPhone = "connect-iphone"
    case pairMessages = "pair-messages"
    case pairCalls = "pair-calls"
    case sharing, apps, car, test
    case readyIPhone = "ready-iphone"
    case ready, status
}

struct SetupFlowState: Equatable {
    var step: SetupStep = .welcome
    var progress: SetupProgress
    var initialized = false
    var reviewingPhoneStep = false
    var reviewingCarStep = false
    var appsReturnStep: SetupStep = .welcome
    var helpReturnStep: SetupStep = .car
    var connectionRequestPending = false
    var phoneSetupPhase: PhoneSetupPhase = .inactive
}

enum SetupFlowEvent {
    case launch
    case resetForTesting
    case getStarted(connected: Bool, status: BridgeStatus?)
    case peripheralFound
    case accessorySetupReady
    case accessoryPickerCancelled
    case connectionGuidancePresented
    case phoneSetupEvent(PhoneSetupAdapterEvent)
    case connectionChanged(connected: Bool)
    case deviceIdentified(String)
    case statusReceived(BridgeStatus)
    case openHelp(returnTo: SetupStep)
    case openAppsFromReady
    case phoneReviewContinued
    case appsContinued(status: BridgeStatus?)
    case carReviewContinued
    case deferTesla
    case confirmTest
    case helpDone(status: BridgeStatus?)
    case readyAction(status: BridgeStatus?)
    case back
    case previewStarted(checking: Bool, cachedStatus: BridgeStatus?)
    case previewScreen(SetupPreviewScreen)
}

enum SetupFlowEffect: Equatable {
    case startBluetooth
    case connectFound
    case retryBluetooth
    case loadCatalog
    case clearBluetoothError
    case setBluetoothError(String)
}

struct SetupFlowMachine {
    private(set) var state: SetupFlowState

    init(progress: SetupProgress = SetupProgress()) {
        state = SetupFlowState(progress: progress)
    }

    mutating func handle(_ event: SetupFlowEvent) -> [SetupFlowEffect] {
        let previousStep = state.step
        var effects: [SetupFlowEffect] = []

        switch event {
        case .launch:
            guard !state.initialized else { return [] }
            state.initialized = true
            // A fresh app session always begins at Welcome. The legacy
            // welcomed preference remains readable for migration compatibility,
            // but it no longer skips the first screen or starts Bluetooth.
            state.step = .welcome

        case .resetForTesting:
            state = SetupFlowState(progress: SetupProgress(), initialized: true)

        case let .getStarted(connected, status):
            state.progress.welcomed = true
            if connected {
                state.step = .checking
                state.phoneSetupPhase = .verifying
                if let status { route(status) }
            } else {
                state.step = .finding
                state.phoneSetupPhase = .preparing
                effects.append(.startBluetooth)
            }

        case .peripheralFound:
            guard state.step == .finding else { break }
            state.connectionRequestPending = true
            state.phoneSetupPhase = .awaitingUser
            state.step = .checking

        case .accessorySetupReady:
            guard state.step == .finding else { break }
            state.connectionRequestPending = true
            state.phoneSetupPhase = .awaitingUser
            state.step = .checking

        case .accessoryPickerCancelled:
            guard state.step == .checking else { break }
            state.connectionRequestPending = true
            state.phoneSetupPhase = .awaitingUser

        case .connectionGuidancePresented:
            guard state.step == .checking, state.connectionRequestPending else { break }
            state.connectionRequestPending = false
            state.phoneSetupPhase = .systemSetup
            effects.append(.connectFound)

        case let .phoneSetupEvent(event):
            switch event {
            case .pickerPresented, .pairing, .bridging:
                state.phoneSetupPhase = .systemSetup
            case .authorized, .connecting:
                state.phoneSetupPhase = .verifying
            case .recovering:
                state.phoneSetupPhase = .recovering
            case let .failed(failure):
                guard state.step == .finding || state.step == .checking else { break }
                state.helpReturnStep = .checking
                state.step = .help
                effects.append(.setBluetoothError(Self.message(for: failure)))
            }

        case let .connectionChanged(connected):
            if connected {
                if state.step == .finding { state.step = .checking }
                if state.step == .checking { state.phoneSetupPhase = .verifying }
            } else if state.step == .checking, !state.connectionRequestPending,
                      state.phoneSetupPhase != .systemSetup {
                // Pairing and notification authorization can deliberately
                // rebuild the BLE link. Keep one stable setup screen while
                // the adapter performs bounded automatic recovery.
                state.phoneSetupPhase = .recovering
            }

        case let .deviceIdentified(identifier):
            if !state.progress.completedDeviceID.isEmpty,
               state.progress.completedDeviceID != identifier {
                state.progress.choseApps = false
                state.progress.testConfirmed = false
                state.progress.teslaDeferred = false
            }
            state.progress.completedDeviceID = identifier

        case let .statusReceived(status):
            route(status)

        case let .openHelp(returnTo):
            state.helpReturnStep = returnTo
            state.step = .help

        case .openAppsFromReady:
            state.appsReturnStep = .ready
            state.step = .apps

        case .phoneReviewContinued:
            state.reviewingPhoneStep = false
            state.step = .apps

        case let .appsContinued(status):
            state.progress.choseApps = true
            if state.progress.testConfirmed || state.progress.teslaDeferred {
                state.step = .ready
            } else if status?.teslaSetupReady == true {
                state.step = .test
            } else {
                state.step = .car
            }

        case .carReviewContinued:
            state.reviewingCarStep = false
            state.step = .test

        case .deferTesla:
            state.progress.teslaDeferred = true
            state.reviewingCarStep = false
            state.step = .ready

        case .confirmTest:
            state.progress.testConfirmed = true
            state.progress.teslaDeferred = false
            state.step = .ready

        case let .helpDone(status):
            effects.append(.clearBluetoothError)
            if state.progress.testConfirmed {
                state.step = .ready
            } else if status?.teslaSetupReady == true {
                state.step = .test
            } else if status != nil {
                state.step = .car
            } else {
                state.step = .finding
                effects.append(.retryBluetooth)
            }

        case let .readyAction(status):
            if status?.teslaSetupReady == true {
                state.step = .test
            } else {
                state.reviewingCarStep = false
                state.step = .car
            }

        case .back:
            goBack()

        case let .previewStarted(checking, cachedStatus):
            if checking {
                state.step = .checking
            } else if let cachedStatus {
                state.step = .checking
                route(cachedStatus)
            } else {
                state.appsReturnStep = .welcome
                state.step = .apps
            }

        case let .previewScreen(screen):
            showPreview(screen)
        }

        if state.step != previousStep {
            if state.step == .apps || state.step == .ready {
                effects.append(.loadCatalog)
            }
        }
        return effects
    }

    private mutating func route(_ status: BridgeStatus) {
        if state.step == .checking {
            if state.reviewingPhoneStep { return }
            if status.phoneSetupReady {
                state.phoneSetupPhase = .complete
                if state.progress.choseApps {
                    state.step = state.progress.testConfirmed || state.progress.teslaDeferred
                        ? .ready
                        : (status.teslaSetupReady ? .test : .car)
                } else {
                    state.appsReturnStep = .checking
                    state.step = .apps
                }
                return
            }
            state.phoneSetupPhase = .verifying
        }
        if state.step == .car, !state.reviewingCarStep,
           status.teslaSetupReady {
            state.step = state.progress.testConfirmed ? .ready : .test
        }
    }

    private mutating func goBack() {
        switch state.step {
        case .welcome:
            break
        case .finding, .checking:
            state.reviewingPhoneStep = false
            state.step = .welcome
        case .apps:
            state.reviewingPhoneStep = state.appsReturnStep == .checking
            state.step = state.appsReturnStep
        case .car:
            state.reviewingCarStep = false
            state.step = .apps
        case .test:
            state.reviewingCarStep = true
            state.step = .car
        case .help:
            state.step = state.helpReturnStep == .checking ? .welcome : state.helpReturnStep
        case .ready:
            if state.progress.testConfirmed {
                state.step = .test
            } else {
                state.reviewingCarStep = true
                state.step = .car
            }
        }
    }

    private mutating func showPreview(_ screen: SetupPreviewScreen) {
        switch screen {
        case .welcome: state.step = .welcome
        case .finding: state.step = .finding
        case .connectIPhone:
            state.step = .checking
            state.connectionRequestPending = true
            state.phoneSetupPhase = .awaitingUser
        case .checking:
            state.step = .checking
            state.phoneSetupPhase = .verifying
        case .retry:
            state.helpReturnStep = .checking
            state.step = .help
        case .pairMessages, .sharing, .pairCalls:
            state.step = .checking
            state.phoneSetupPhase = .verifying
        case .apps:
            state.appsReturnStep = .welcome
            state.step = .apps
        case .car: state.step = .car
        case .test: state.step = .test
        case .readyIPhone:
            state.progress.teslaDeferred = true
            state.step = .ready
        case .ready:
            state.progress.testConfirmed = true
            state.step = .ready
        case .status:
            state.helpReturnStep = .ready
            state.step = .help
        }
    }

    private static func message(for failure: PhoneSetupFailure) -> String {
        switch failure {
        case .accessorySetup:
            "iPhone couldn't finish setting up DashBridge. Keep it powered and nearby, then try again."
        case .bluetoothOff:
            "Turn on Bluetooth on your iPhone, then try again."
        case .bluetoothPermission:
            "Allow Bluetooth for DashBridge in iPhone Settings, then try again."
        case .bluetoothUnavailable:
            "Bluetooth isn't available on this iPhone right now. Try again in a moment."
        case .unavailable:
            "The app couldn't reconnect to DashBridge after trying automatically. Keep it powered and nearby, then try again."
        case .incompatibleFirmware:
            "This DashBridge needs the companion-app firmware."
        case .incompleteService:
            "DashBridge's setup connection is incomplete. Restart it and try again."
        }
    }
}

@MainActor
final class SetupFlowStore: ObservableObject {
    @Published private(set) var state: SetupFlowState

    private var machine: SetupFlowMachine
    private let persistence: SetupProgressPersisting

    init(persistence: SetupProgressPersisting = UserDefaultsSetupProgressStore()) {
        self.persistence = persistence
        machine = SetupFlowMachine(progress: persistence.load())
        state = machine.state
    }

    @discardableResult
    func send(_ event: SetupFlowEvent) -> [SetupFlowEffect] {
        let previousProgress = machine.state.progress
        let effects = machine.handle(event)
        state = machine.state
        if state.progress != previousProgress { persistence.save(state.progress) }
        return effects
    }
}
