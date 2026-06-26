//  QwenBridgeBar — macOS menu bar dashboard for Qwen Code token usage.
//  Shows the same data as the ESP32 e-ink display, plus BLE/Bridge controls.
//  Build: see build.sh

import Cocoa
import ServiceManagement
import SwiftUI

// MARK: - Constants

let LOG_FILE = "/tmp/qwen-token-bridge-stdout.log"
let STATUS_FILE = "/tmp/qwen-token-status.json"
let GOAL_TARGET: Double = 100_000_000

// MARK: - Data Model

struct TokenStatus: Codable {
    var todayTotal: Int64 = 0
    var todayInput: Int64 = 0
    var todayOutput: Int64 = 0
    var cacheRate: Int = 0
    var activeMinutes: Int = 0
    var sessionsToday: Int = 0
    var weekTotal: Int64 = 0
    var models: [ModelEntry] = []
    var updatedAt: String = "--:--"
    var ageSec: Int = 0
    var bleConnected: Bool = false
    var bleDevice: String = ""

    struct ModelEntry: Codable {
        var model: String = "--"
        var pct: Int = 0
    }
}

// MARK: - Formatting

func fmtTokens(_ t: Int64) -> String {
    let f = NumberFormatter()
    f.numberStyle = .decimal
    return f.string(from: NSNumber(value: t)) ?? "\(t)"
}

func fmtShort(_ t: Int64) -> String {
    if t >= 1_000_000_000 { return String(format: "%.1fB", Double(t) / 1e9) }
    if t >= 10_000_000 { return String(format: "%.0fM", Double(t) / 1e6) }
    if t >= 1_000_000 { return String(format: "%.1fM", Double(t) / 1e6) }
    if t >= 1_000 { return String(format: "%.0fk", Double(t) / 1e3) }
    return "\(t)"
}

func fmtActive(_ mins: Int) -> String {
    if mins < 60 { return "\(mins)m" }
    let h = mins / 60
    let m = mins % 60
    return m > 0 ? "\(h)h\(String(format: "%02d", m))" : "\(h)h"
}

func greetingText() -> String {
    let h = Calendar.current.component(.hour, from: Date())
    if h >= 5 && h < 12 { return "早上好～" }
    if h < 18 { return "下午好～" }
    return "晚上好～"
}

// MARK: - Bridge Process Manager

func findNode() -> String? {
    // Check common paths first, then fall back to PATH lookup
    let candidates = [
        "/usr/local/bin/node",
        "/opt/homebrew/bin/node",
        NSHomeDirectory() + "/.nvm/versions/node/v22.22.2/bin/node",
    ]
    for path in candidates {
        if FileManager.default.isExecutableFile(atPath: path) { return path }
    }
    // Fallback: use `which`
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/which")
    p.arguments = ["node"]
    let pipe = Pipe()
    p.standardOutput = pipe
    p.standardError = FileHandle.nullDevice
    do { try p.run(); p.waitUntilExit() } catch { return nil }
    let out = String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines)
    return (out != nil && FileManager.default.isExecutableFile(atPath: out!)) ? out : nil
}

func bridgeScriptPath() -> String? {
    return Bundle.main.path(forResource: "index", ofType: "js", inDirectory: "Bridge")
        ?? Bundle.main.path(forResource: "index", ofType: "js")
}

// MARK: - Status Manager

class StatusManager: ObservableObject {
    @Published var status = TokenStatus()
    @Published var bridgeRunning = false
    @Published var bridgePID: Int = -1
    @Published var fileExists = false
    @Published var bleDeviceName: String = UserDefaults.standard.string(forKey: "bleDeviceName") ?? ""
    @Published var launchAtLogin: Bool = {
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .enabled
        }
        return false
    }()

    private var timer: Timer?
    private var bridgeProcess: Process?
    private var bridgeShouldRun = false
    private var sleepObserver: NSObjectProtocol?
    private var wakeObserver: NSObjectProtocol?

    func start() {
        ensureLaunchAtLogin()
        installPowerObservers()
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { _ in self.refresh() }
    }

    func stop() {
        bridgeShouldRun = false
        timer?.invalidate()
        removePowerObservers()
    }

    private func installPowerObservers() {
        guard sleepObserver == nil && wakeObserver == nil else { return }
        let center = NSWorkspace.shared.notificationCenter
        sleepObserver = center.addObserver(
            forName: NSWorkspace.willSleepNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            print("[power] will sleep, stopping bridge")
            self?.doStop()
        }
        wakeObserver = center.addObserver(
            forName: NSWorkspace.didWakeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            print("[power] did wake, restarting bridge")
            self?.bridgeShouldRun = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                self?.doStart()
            }
        }
    }

    private func removePowerObservers() {
        let center = NSWorkspace.shared.notificationCenter
        if let sleepObserver {
            center.removeObserver(sleepObserver)
            self.sleepObserver = nil
        }
        if let wakeObserver {
            center.removeObserver(wakeObserver)
            self.wakeObserver = nil
        }
    }

    private func refresh() {
        // Read status file
        if let data = try? Data(contentsOf: URL(fileURLWithPath: STATUS_FILE)) {
            fileExists = true
            if let decoded = try? JSONDecoder().decode(TokenStatus.self, from: data) {
                DispatchQueue.main.async { self.status = decoded }
            }
        } else {
            DispatchQueue.main.async { self.fileExists = false }
        }

        // Check if bridge process is alive
        let pid = bridgeProcess?.processIdentifier ?? -1
        let alive = pid > 0 && (bridgeProcess?.isRunning ?? false)
        DispatchQueue.main.async {
            self.bridgeRunning = alive
            self.bridgePID = alive ? Int(pid) : -1
        }
    }

    func doStart() {
        bridgeShouldRun = true
        guard bridgeProcess?.isRunning != true else { return }
        guard let nodePath = findNode() else { return }
        guard let scriptPath = bridgeScriptPath() else { return }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: nodePath)
        process.arguments = [scriptPath]
        process.currentDirectoryURL = URL(fileURLWithPath: (scriptPath as NSString).deletingLastPathComponent)

        // Pass BLE device name as env var
        var env = ProcessInfo.processInfo.environment
        env["TOKEN_MONITOR_DATASOURCES"] = env["TOKEN_MONITOR_DATASOURCES"] ?? "qwen,codex"
        env["QWEN_BLE_DEVICE_NAME"] = bleDeviceName.isEmpty ? "QwenToken" : bleDeviceName
        process.environment = env

        // Redirect stdout/stderr to log file via pipe
        let outPipe = Pipe()
        process.standardOutput = outPipe
        process.standardError = outPipe
        FileManager.default.createFile(atPath: LOG_FILE, contents: nil)
        let logFH = FileHandle(forWritingAtPath: LOG_FILE)
        outPipe.fileHandleForReading.readabilityHandler = { fh in
            let data = fh.availableData
            if !data.isEmpty { logFH?.write(data) }
        }

        process.terminationHandler = { [weak self] _ in
            DispatchQueue.main.async {
                let shouldRestart = self?.bridgeShouldRun ?? false
                self?.bridgeProcess = nil
                self?.refresh()
                if shouldRestart {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
                        self?.doStart()
                    }
                }
            }
        }

        do {
            try process.run()
            bridgeProcess = process
        } catch {
            print("[bridge] start failed: \(error)")
        }
        Thread.sleep(forTimeInterval: 0.5)
        refresh()
    }

    func doStop() {
        bridgeShouldRun = false
        guard let process = bridgeProcess, process.isRunning else { return }
        // Send SIGTERM for clean BLE disconnect
        kill(process.processIdentifier, SIGTERM)
        // Force kill after 3 seconds if still alive
        DispatchQueue.global().asyncAfter(deadline: .now() + 3) { [weak self] in
            if let p = self?.bridgeProcess, p.isRunning {
                p.terminate()
            }
        }
        Thread.sleep(forTimeInterval: 0.5)
        refresh()
    }

    func doRestart() {
        bridgeShouldRun = false
        doStop()
        Thread.sleep(forTimeInterval: 1)
        doStart()
    }

    func applyBleDeviceName() {
        UserDefaults.standard.set(bleDeviceName, forKey: "bleDeviceName")
        doRestart()
    }

    func toggleLaunchAtLogin() {
        if #available(macOS 13.0, *) {
            do {
                if launchAtLogin {
                    try SMAppService.mainApp.unregister()
                    launchAtLogin = false
                } else {
                    try SMAppService.mainApp.register()
                    launchAtLogin = true
                }
            } catch {
                print("[launch] failed: \(error)")
            }
        }
    }

    func ensureLaunchAtLogin() {
        if #available(macOS 13.0, *) {
            do {
                if SMAppService.mainApp.status != .enabled {
                    try SMAppService.mainApp.register()
                }
                launchAtLogin = SMAppService.mainApp.status == .enabled
            } catch {
                print("[launch] auto-register failed: \(error)")
            }
        }
    }
}

// MARK: - SwiftUI Components

struct StatCard: View {
    let title: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .font(.system(size: 10))
                .foregroundStyle(.secondary)
            Text(value)
                .font(.system(size: 16, design: .rounded).weight(.medium))
                .monospacedDigit()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 10)
        .padding(.vertical, 7)
        .background(.quaternary.opacity(0.4))
        .clipShape(RoundedRectangle(cornerRadius: 6))
    }
}

struct StatusDot: View {
    let on: Bool

    var body: some View {
        Circle()
            .fill(on ? Color.green : Color.orange)
            .frame(width: 7, height: 7)
    }
}

struct DashboardView: View {
    @ObservedObject var manager: StatusManager

    private var s: TokenStatus { manager.status }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // ── Header ──
            HStack(spacing: 8) {
                if let path = Bundle.main.path(forResource: "avatar", ofType: "png"),
                   let img = NSImage(contentsOfFile: path) {
                    Image(nsImage: img)
                        .resizable()
                        .frame(width: 32, height: 32)
                        .clipShape(RoundedRectangle(cornerRadius: 8))
                }
                Text(greetingText())
                    .font(.system(size: 15))
                Spacer()
                Text(s.updatedAt)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.secondary)
            }
            .padding(.bottom, 8)

            Divider()

            // ── Today Tokens + Progress ──
            VStack(alignment: .leading, spacing: 2) {
                Text("TODAY TOKENS")
                    .font(.system(size: 10))
                    .foregroundStyle(.secondary)
                HStack(alignment: .firstTextBaseline, spacing: 4) {
                    Text(fmtTokens(s.todayTotal))
                        .font(.system(size: 30, design: .rounded).weight(.semibold))
                        .monospacedDigit()
                    Text("tokens")
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                }

                // Progress bar toward 100M goal
                HStack(spacing: 6) {
                    GeometryReader { geo in
                        ZStack(alignment: .leading) {
                            RoundedRectangle(cornerRadius: 3)
                                .fill(.quaternary.opacity(0.5))
                            RoundedRectangle(cornerRadius: 3)
                                .fill(Color.accentColor)
                                .frame(width: max(2, geo.size.width * min(1, CGFloat(s.todayTotal) / GOAL_TARGET)))
                        }
                    }
                    .frame(height: 7)

                    Text(String(format: "%.1f%%", Double(s.todayTotal) / GOAL_TARGET * 100))
                        .font(.system(size: 10, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .frame(width: 42, alignment: .trailing)
                }
                .padding(.top, 2)
            }
            .padding(.vertical, 10)

            // ── Top Models ──
            if !s.models.isEmpty {
                VStack(alignment: .leading, spacing: 3) {
                    Text("TOP MODELS")
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                    ForEach(Array(s.models.prefix(3).enumerated()), id: \.offset) { _, m in
                        HStack {
                            Text(m.model)
                                .font(.system(size: 12))
                                .lineLimit(1)
                            Spacer()
                            Text("\(m.pct)%")
                                .font(.system(size: 12, design: .monospaced))
                                .foregroundStyle(.secondary)
                        }
                    }
                }
                .padding(.bottom, 10)
            }

            Divider()

            // ── Stats Grid ──
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 6) {
                StatCard(title: "Sessions", value: "\(s.sessionsToday)")
                StatCard(title: "Active", value: fmtActive(s.activeMinutes))
                StatCard(title: "Input", value: fmtShort(s.todayInput))
                StatCard(title: "Output", value: fmtShort(s.todayOutput))
                StatCard(title: "Cache Rate", value: "\(s.cacheRate)%")
                StatCard(title: "7 Days", value: fmtShort(s.weekTotal))
            }
            .padding(.vertical, 10)

            Divider()

            // ── Status Bar ──
            HStack(spacing: 16) {
                HStack(spacing: 4) {
                    StatusDot(on: s.bleConnected)
                    Text("BLE:")
                        .font(.system(size: 11))
                    Text(s.bleConnected ? (s.bleDevice.isEmpty ? "Connected" : s.bleDevice) : "Scanning…")
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                }
                HStack(spacing: 4) {
                    StatusDot(on: manager.bridgeRunning)
                    Text("Bridge:")
                        .font(.system(size: 11))
                    Text(manager.bridgeRunning ? "Running" : "Stopped")
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding(.vertical, 8)

            // ── BLE Device Name ──
            Divider()

            HStack(spacing: 8) {
                Text("BLE Name:")
                    .font(.system(size: 11))
                TextField("QwenToken", text: $manager.bleDeviceName)
                    .font(.system(size: 11))
                    .textFieldStyle(.roundedBorder)
                Button("Apply") { manager.applyBleDeviceName() }
                    .font(.system(size: 11))
            }
            .padding(.vertical, 6)

            // ── Launch at Login ──
            HStack {
                Toggle("Launch at Login", isOn: Binding(
                    get: { manager.launchAtLogin },
                    set: { _ in manager.toggleLaunchAtLogin() }
                ))
                .font(.system(size: 11))
                Spacer()
            }

            // ── Controls ─
            HStack(spacing: 8) {
                if manager.bridgeRunning {
                    Button("Stop") { manager.doStop() }
                    Button("Restart") { manager.doRestart() }
                } else {
                    Button("Start") { manager.doStart() }
                }
                Spacer()
                Button("Open Log") {
                    NSWorkspace.shared.open(URL(fileURLWithPath: LOG_FILE))
                }
                Button("Quit") {
                    manager.doStop()
                    manager.stop()
                    NSApp.terminate(nil)
                }
            }
            .padding(.bottom, 2)
        }
        .padding(.horizontal, 16)
        .padding(.top, 14)
        .padding(.bottom, 14)
        .frame(width: 340)
    }
}

// MARK: - App Delegate

class AppDelegate: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var popover: NSPopover!
    let manager = StatusManager()

    func applicationDidFinishLaunching(_ n: Notification) {
        NSApp.setActivationPolicy(.accessory)

        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let btn = statusItem.button {
            btn.title = "RLCD"
            btn.toolTip = "TokenMaxxing RLCD Bridge"
            btn.action = #selector(togglePopover)
            btn.target = self
        }

        popover = NSPopover()
        popover.contentSize = NSSize(width: 340, height: 500)
        popover.behavior = .transient
        popover.contentViewController = NSHostingController(rootView: DashboardView(manager: manager))

        manager.start()
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) {
            self.manager.doStart()
        }
    }

    @objc func togglePopover() {
        if popover.isShown {
            closePopover()
        } else {
            showPopover()
        }
    }

    func showPopover() {
        if let btn = statusItem.button {
            popover.show(relativeTo: btn.bounds, of: btn, preferredEdge: .minY)
        }
    }

    func closePopover() {
        popover.performClose(nil)
    }

    func applicationWillTerminate(_ n: Notification) {
        manager.doStop()
        manager.stop()
    }
}

// MARK: - Entry Point

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
