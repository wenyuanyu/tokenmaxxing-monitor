//  QwenBridgeBar — minimal macOS menu bar app to control the BLE bridge LaunchAgent.
//  Build: swiftc -o QwenBridgeBar -framework Cocoa -suppress-warnings QwenBridgeBar.swift

import Cocoa

let LABEL = "com.pomelo.qwen-token-bridge"
let PLIST = "\(NSHomeDirectory())/Library/LaunchAgents/com.pomelo.qwen-token-bridge.plist"
let LOG_FILE = "/tmp/qwen-token-bridge-stdout.log"

func shell(_ args: [String]) -> String {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/bin/launchctl")
    p.arguments = args
    let pipe = Pipe()
    p.standardOutput = pipe
    p.standardError = Pipe()
    do { try p.run() } catch { return "" }
    p.waitUntilExit()
    return String(data: pipe.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
}

func isLoaded() -> Bool {
    return !shell(["list", LABEL]).isEmpty
}

func getPID() -> Int {
    let out = shell(["list", LABEL])
    if out.isEmpty { return -1 }
    let pidStr = out.split(separator: "\t").first ?? ""
    return Int(pidStr) ?? -1
}

class AppDelegate: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var statusMenuItem: NSMenuItem!
    var timer: Timer?

    func applicationDidFinishLaunching(_ n: Notification) {
        NSApp.setActivationPolicy(.accessory)

        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let btn = statusItem.button {
            if let path = Bundle.main.path(forResource: "capybara_icon", ofType: "png"),
               let img = NSImage(contentsOfFile: path) {
                img.size = NSSize(width: 20, height: 20)
                btn.image = img
                btn.image?.isTemplate = false
            } else {
                btn.title = "📡"
            }
        }

        let menu = NSMenu()

        statusMenuItem = NSMenuItem(title: "Checking…", action: nil, keyEquivalent: "")
        menu.addItem(statusMenuItem)

        menu.addItem(.separator())

        let start = NSMenuItem(title: "Start", action: #selector(doStart), keyEquivalent: "s")
        start.target = self
        menu.addItem(start)

        let stop = NSMenuItem(title: "Stop", action: #selector(doStop), keyEquivalent: "x")
        stop.target = self
        menu.addItem(stop)

        let restart = NSMenuItem(title: "Restart", action: #selector(doRestart), keyEquivalent: "r")
        restart.target = self
        menu.addItem(restart)

        menu.addItem(.separator())

        let log = NSMenuItem(title: "Open Log", action: #selector(doLog), keyEquivalent: "l")
        log.target = self
        menu.addItem(log)

        menu.addItem(.separator())

        let quit = NSMenuItem(title: "Quit", action: #selector(doQuit), keyEquivalent: "q")
        quit.target = self
        menu.addItem(quit)

        statusItem.menu = menu
        updateStatus()
        timer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { _ in self.updateStatus() }
    }

    func updateStatus() {
        let loaded = isLoaded()
        let pid = getPID()
        let running = loaded && pid > 0
        statusMenuItem.title = running ? "● Running  (PID \(pid))" : (loaded ? "◐ Loaded (not running)" : "○ Stopped")
    }

    @objc func doStart() {
        shell(["load", PLIST])
        updateStatus()
    }

    @objc func doStop() {
        shell(["unload", PLIST])
        updateStatus()
    }

    @objc func doRestart() {
        shell(["unload", PLIST])
        Thread.sleep(forTimeInterval: 1)
        shell(["load", PLIST])
        updateStatus()
    }

    @objc func doLog() {
        NSWorkspace.shared.open(URL(fileURLWithPath: LOG_FILE))
    }

    @objc func doQuit() {
        timer?.invalidate()
        NSApp.terminate(nil)
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
