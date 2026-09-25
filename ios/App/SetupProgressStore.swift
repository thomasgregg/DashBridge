import Foundation

struct SetupProgress: Equatable {
    var welcomed = false
    var choseApps = false
    var testConfirmed = false
    var teslaDeferred = false
    var completedDeviceID = ""
}

protocol SetupProgressPersisting {
    func load() -> SetupProgress
    func save(_ progress: SetupProgress)
}

struct UserDefaultsSetupProgressStore: SetupProgressPersisting {
    private enum Key {
        static let welcomed = "dashbridge.welcomed"
        static let choseApps = "dashbridge.choseApps"
        static let testConfirmed = "dashbridge.testConfirmed"
        static let teslaDeferred = "dashbridge.teslaDeferred"
        static let completedDeviceID = "dashbridge.completedDeviceID"
    }

    let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    func load() -> SetupProgress {
        SetupProgress(
            welcomed: defaults.bool(forKey: Key.welcomed),
            choseApps: defaults.bool(forKey: Key.choseApps),
            testConfirmed: defaults.bool(forKey: Key.testConfirmed),
            teslaDeferred: defaults.bool(forKey: Key.teslaDeferred),
            completedDeviceID: defaults.string(forKey: Key.completedDeviceID) ?? ""
        )
    }

    func save(_ progress: SetupProgress) {
        defaults.set(progress.welcomed, forKey: Key.welcomed)
        defaults.set(progress.choseApps, forKey: Key.choseApps)
        defaults.set(progress.testConfirmed, forKey: Key.testConfirmed)
        defaults.set(progress.teslaDeferred, forKey: Key.teslaDeferred)
        defaults.set(progress.completedDeviceID, forKey: Key.completedDeviceID)
    }
}

protocol RememberedPeripheralPersisting {
    func loadIdentifier() -> UUID?
    func saveIdentifier(_ identifier: UUID)
    func removeIdentifier(ifMatching identifier: UUID)
}

struct UserDefaultsRememberedPeripheralStore: RememberedPeripheralPersisting {
    private let key = "dashbridge.peripheral"
    let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    func loadIdentifier() -> UUID? {
        defaults.string(forKey: key).flatMap(UUID.init(uuidString:))
    }

    func saveIdentifier(_ identifier: UUID) {
        defaults.set(identifier.uuidString, forKey: key)
    }

    func removeIdentifier(ifMatching identifier: UUID) {
        guard loadIdentifier() == identifier else { return }
        defaults.removeObject(forKey: key)
    }
}
