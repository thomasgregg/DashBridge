import Combine
import Foundation

enum SetupStep: Equatable {
    case welcome, finding, checking, pair, sharing, apps
    case car, test, ready, help
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
}

enum SetupFlowEvent {
    case launch
    case resetForTesting
    case getStarted(connected: Bool, status: BridgeStatus?)
    case peripheralFound
    case accessorySetupReady
    case accessoryPickerCancelled
    case connectionGuidancePresented
    case connectionChanged(connected: Bool, hasError: Bool)
    case deviceIdentified(String)
    case statusReceived(BridgeStatus)
    case connectionFailedDuringCheck
    case checkingTimedOut(connected: Bool)
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
    case openPhonePairing
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
                if let status { route(status) }
            } else {
                state.step = .finding
                effects.append(.startBluetooth)
            }

        case .peripheralFound:
            guard state.step == .finding else { break }
            state.connectionRequestPending = true
            state.step = .checking

        case .accessorySetupReady:
            guard state.step == .finding else { break }
            state.connectionRequestPending = true
            state.step = .checking

        case .accessoryPickerCancelled:
            guard state.step == .checking else { break }
            state.connectionRequestPending = true

        case .connectionGuidancePresented:
            guard state.step == .checking, state.connectionRequestPending else { break }
            state.connectionRequestPending = false
            effects.append(.connectFound)

        case let .connectionChanged(connected, hasError):
            if connected, state.step == .finding { state.step = .checking }
            if !connected, state.step == .checking, !hasError { state.step = .finding }

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

        case .connectionFailedDuringCheck:
            guard state.step == .checking else { break }
            state.helpReturnStep = .checking
            state.step = .help

        case let .checkingTimedOut(connected):
            guard state.step == .checking else { break }
            let message = connected
                ? "Your iPhone connected to DashBridge, but the app couldn't check its setup yet."
                : "The app couldn't reach DashBridge's setup connection yet."
            state.helpReturnStep = .checking
            state.step = .help
            effects.append(.setBluetoothError(message))

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
            } else if status?.teslaMessages == true, status?.teslaCalls == true {
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
            } else if status?.teslaMessages == true {
                state.step = .test
            } else if status != nil {
                state.step = .car
            } else {
                state.step = .finding
                effects.append(.retryBluetooth)
            }

        case let .readyAction(status):
            if status?.teslaMessages == true, status?.teslaCalls == true {
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
            if state.step == .pair, !state.reviewingPhoneStep {
                // Refresh the board's finite pairing window at the moment the
                // system picker is actually ready to finish Dash Calls.
                effects.append(.openPhonePairing)
            }
            if state.step == .apps || state.step == .ready {
                effects.append(.loadCatalog)
            }
        }
        return effects
    }

    private mutating func route(_ status: BridgeStatus) {
        if state.step == .checking || state.step == .pair || state.step == .sharing {
            if state.reviewingPhoneStep { return }
            if status.phoneCalls && status.notifications {
                if state.progress.choseApps {
                    state.step = state.progress.testConfirmed || state.progress.teslaDeferred
                        ? .ready
                        : (status.teslaMessages && status.teslaCalls ? .test : .car)
                } else {
                    state.appsReturnStep = .pair
                    state.step = .apps
                }
                return
            }
            state.step = status.notifications ? .pair : .sharing
        }
        if state.step == .car, !state.reviewingCarStep,
           status.teslaMessages, status.teslaCalls {
            state.step = state.progress.testConfirmed ? .ready : .test
        }
    }

    private mutating func goBack() {
        switch state.step {
        case .welcome:
            break
        case .finding, .checking, .pair, .sharing:
            state.reviewingPhoneStep = false
            state.step = .welcome
        case .apps:
            state.reviewingPhoneStep = state.appsReturnStep == .pair
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
        case .checking: state.step = .checking
        case .retry:
            state.helpReturnStep = .checking
            state.step = .help
        case .pairMessages, .sharing: state.step = .sharing
        case .pairCalls: state.step = .pair
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
