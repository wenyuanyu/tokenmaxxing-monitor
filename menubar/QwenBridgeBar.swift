//  QwenBridgeBar — macOS menu bar dashboard for Qwen Code token usage.
//  Shows the same data as the ESP32 e-ink display, plus BLE/Bridge controls.
//  Build: see build.sh

import Cocoa
import CoreBluetooth
import Darwin
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
    var timestamp: Double = 0

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
    if h < 5 { return "凌晨好～" }
    if h < 12 { return "早上好～" }
    if h < 18 { return "下午好～" }
    return "晚上好～"
}

// MARK: - Bluetooth Permission Probe

final class BluetoothPermissionProbe: NSObject, CBCentralManagerDelegate {
    static let shared = BluetoothPermissionProbe()
    private var manager: CBCentralManager?

    func start() {
        if manager != nil { return }
        manager = CBCentralManager(
            delegate: self,
            queue: .main,
            options: [CBCentralManagerOptionShowPowerAlertKey: true]
        )
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        print("[ble-permission] state \(central.state.rawValue)")
    }
}

// MARK: - Bridge Process Manager

func findNode() -> String? {
    // Check common paths first, then fall back to PATH lookup
    let bundledNode: String = {
        guard let executablePath = Bundle.main.executablePath else { return "" }
        return ((executablePath as NSString).deletingLastPathComponent as NSString).appendingPathComponent("node")
    }()
    let candidates = [
        bundledNode,
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

func findLaunchAgentNode() -> String? {
    let candidates = [
        "/usr/local/bin/node",
        "/opt/homebrew/bin/node",
        NSHomeDirectory() + "/.nvm/versions/node/v22.22.2/bin/node",
    ]
    for path in candidates {
        if FileManager.default.isExecutableFile(atPath: path) { return path }
    }
    return findNode()
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
    private var bridgeShouldRun = false

    func start() {
        ensureLaunchAtLogin()
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { _ in self.refresh() }
    }

    func stop() {
        bridgeShouldRun = false
        timer?.invalidate()
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

        let statusFresh = status.timestamp > 0 &&
            Date().timeIntervalSince1970 * 1000 - status.timestamp < 15000
        DispatchQueue.main.async {
            self.bridgeRunning = statusFresh
            self.bridgePID = -1
        }
    }

    private func launchAgentPath() -> String {
        let dir = NSHomeDirectory() + "/Library/LaunchAgents"
        try? FileManager.default.createDirectory(atPath: dir, withIntermediateDirectories: true)
        return dir + "/io.github.tokenmaxxing.rlcd-bridge.plist"
    }

    private func writeLaunchAgent() -> Bool {
        guard let nodePath = findLaunchAgentNode() else { return false }
        guard let scriptPath = bridgeScriptPath() else { return false }
        let bridgeDir = (scriptPath as NSString).deletingLastPathComponent
        let name = bleDeviceName.isEmpty ? "QwenToken" : bleDeviceName
        let plist = """
        <?xml version="1.0" encoding="UTF-8"?>
        <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        <plist version="1.0">
        <dict>
            <key>Label</key><string>io.github.tokenmaxxing.rlcd-bridge</string>
            <key>RunAtLoad</key><true/>
            <key>KeepAlive</key><true/>
            <key>ProgramArguments</key>
            <array>
                <string>\(nodePath)</string>
                <string>\(scriptPath)</string>
            </array>
            <key>WorkingDirectory</key><string>\(bridgeDir)</string>
            <key>EnvironmentVariables</key>
            <dict>
                <key>TOKEN_MONITOR_DATASOURCES</key><string>qwen,codex</string>
                <key>QWEN_BLE_DEVICE_NAME</key><string>\(name)</string>
            </dict>
            <key>StandardOutPath</key><string>\(LOG_FILE)</string>
            <key>StandardErrorPath</key><string>\(LOG_FILE)</string>
        </dict>
        </plist>
        """
        do {
            try plist.write(toFile: launchAgentPath(), atomically: true, encoding: .utf8)
            return true
        } catch {
            print("[bridge] write LaunchAgent failed: \(error)")
            return false
        }
    }

    @discardableResult
    private func runLaunchctl(_ args: [String]) -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/launchctl")
        process.arguments = args
        do {
            try process.run()
            process.waitUntilExit()
            return process.terminationStatus == 0
        } catch {
            print("[bridge] launchctl failed: \(error)")
            return false
        }
    }

    func doStart() {
        bridgeShouldRun = true
        guard writeLaunchAgent() else { return }
        let domain = "gui/\(getuid())"
        let plist = launchAgentPath()
        if !runLaunchctl(["bootstrap", domain, plist]) {
            _ = runLaunchctl(["bootout", domain, plist])
            _ = runLaunchctl(["bootstrap", domain, plist])
        }
        _ = runLaunchctl(["kickstart", "-k", "\(domain)/io.github.tokenmaxxing.rlcd-bridge"])
        Thread.sleep(forTimeInterval: 0.5)
        refresh()
    }

    func doStop() {
        bridgeShouldRun = false
        _ = runLaunchctl(["bootout", "gui/\(getuid())", launchAgentPath()])
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
        BluetoothPermissionProbe.shared.start()

        statusItem = NSStatusBar.system.statusItem(withLength: 34)
        if let btn = statusItem.button {
            if let path = Bundle.main.path(forResource: "menubar_icon", ofType: "png"),
               let image = NSImage(contentsOfFile: path) {
                image.isTemplate = true
                image.size = NSSize(width: 28, height: 18)
                btn.image = image
                btn.title = ""
            } else {
                btn.title = "TM"
            }
            btn.toolTip = "TokenMaxxing RLCD Bridge"
            btn.action = #selector(togglePopover)
            btn.target = self
        }

        popover = NSPopover()
        popover.contentSize = NSSize(width: 340, height: 500)
        popover.behavior = .transient
        popover.contentViewController = NSHostingController(rootView: DashboardView(manager: manager))

        manager.start()
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) {
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
