// macOS CoreBluetooth L2CAP CoC throughput client.
//
// Alternative *central* for the BLE 5 L2CAP throughput benchmark: uses the
// Mac's built-in Bluetooth (Apple controller + CoreBluetooth) instead of
// Linux/BlueZ + the RTL8761 dongle, to isolate whether the bottleneck is the
// central stack/controller or the nRF peripheral.
//
// Connects to the nRF peripheral (advertised name "zenoh-nrf-l2cap"), opens an
// L2CAP channel on PSM 0x0080, and runs the same phases as test-l2cap-echo.py:
//   1. correctness echoes (11/20/100/480 bytes)
//   2. echo throughput (100 x 480-byte stop-and-wait roundtrips)
//   3. SINK throughput (200 x 480-byte, host -> device)
//
// NOTE: a CBL2CAPChannel is a BYTE stream (no SDU boundaries), so all logic is
// byte-counted, not message-counted.
//
// Build:  swiftc l2cap_throughput.swift -o l2cap_throughput \
//                -framework CoreBluetooth -framework Foundation
// Run:    ./l2cap_throughput           (grant Bluetooth permission when asked)

import Foundation
import CoreBluetooth

let TARGET_NAME = "zenoh-nrf-l2cap"
let PSM: CBL2CAPPSM = 0x0080
let SDU = 480
let ECHO_ROUNDTRIPS = 100
let SINK_SDUS = 200
let DEBUG = false   // set true for per-SDU tx/rx tracing

enum Phase { case correctness, echoTP, sinkSetup, sink, restore, done }

final class Client: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate, StreamDelegate {
    var central: CBCentralManager!
    var peripheral: CBPeripheral?
    var input: InputStream?
    var output: OutputStream?

    var phase: Phase = .correctness
    var didStart = false

    // pending outbound write (buffer + offset), so partial writes are handled
    var outBuf = [UInt8]()
    var outOff = 0

    // inbound accumulation
    var inCount = 0          // bytes received toward the current expected echo
    var expected = 0         // bytes expected back for the outstanding message

    // correctness
    let corrSizes = [11, 20, 100, 480]
    var corrIdx = 0

    // echo throughput
    var echoDone = 0
    var echoStart: Date?

    // sink
    var sinkSent = 0
    var sinkStart: Date?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // ---- CBCentralManager ----

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn:
            print("Bluetooth on. Scanning for \(TARGET_NAME)...")
            c.scanForPeripherals(withServices: nil, options: nil)
        case .unauthorized:
            print("Bluetooth permission denied. Grant it in System Settings > Privacy > Bluetooth.")
            exit(1)
        case .poweredOff:
            print("Bluetooth is off."); exit(1)
        default:
            print("Bluetooth state: \(c.state.rawValue)")
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? p.name ?? ""
        guard name == TARGET_NAME else { return }
        print("Found \(name)  [\(p.identifier)]  RSSI \(RSSI)")
        c.stopScan()
        peripheral = p
        p.delegate = self
        c.connect(p, options: nil)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        print("Connected (GATT). Opening L2CAP PSM 0x\(String(PSM, radix: 16))...")
        p.openL2CAPChannel(PSM)
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        print("Connect failed: \(error?.localizedDescription ?? "unknown")")
        exit(1)
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        print("Disconnected: \(error?.localizedDescription ?? "clean")")
        if phase != .done { exit(1) }
    }

    // ---- L2CAP channel ----

    func peripheral(_ p: CBPeripheral, didOpen channel: CBL2CAPChannel?, error: Error?) {
        if let e = error { print("openL2CAPChannel error: \(e.localizedDescription)"); exit(1) }
        guard let ch = channel else { print("no channel returned"); exit(1) }
        print("L2CAP channel open (PSM \(ch.psm)). Starting tests.\n")
        input = ch.inputStream
        output = ch.outputStream
        // Run the L2CAP stream I/O on a dedicated thread with its OWN run loop.
        // Stream events are not delivered on the main run loop alongside the
        // CoreBluetooth event source; a private run loop fixes that.
        let t = Thread { [weak self] in self?.ioMain() }
        t.stackSize = 1 << 20
        t.start()
    }

    func ioMain() {
        input?.delegate = self
        output?.delegate = self
        input?.schedule(in: .current, forMode: .default)
        output?.schedule(in: .current, forMode: .default)
        input?.open()
        output?.open()
        // The test is kicked off by the first .hasSpaceAvailable event (see the
        // StreamDelegate handler), then driven entirely on this thread's loop.
        RunLoop.current.run()
    }

    // ---- outbound helpers ----

    func queue(_ bytes: [UInt8], expect: Int) {
        outBuf = bytes; outOff = 0
        expected = expect; inCount = 0
        pump()
    }

    // Write as much of outBuf as the stream will take right now.
    func pump() {
        guard let out = output else { return }
        var wrote = 0
        while outOff < outBuf.count && out.hasSpaceAvailable {
            let n = outBuf[outOff...].withUnsafeBufferPointer {
                out.write($0.baseAddress!, maxLength: outBuf.count - outOff)
            }
            if n <= 0 { break }
            outOff += n; wrote += n
            if phase == .sink { sinkProgress(wrote: n) }
        }
        if DEBUG && wrote > 0 { print("[tx \(wrote)B, off \(outOff)/\(outBuf.count)]") }
        // In SINK we refill the buffer as it drains (see sinkProgress).
    }

    // ---- test phases ----

    func startCorrectness() {
        phase = .correctness
        corrIdx = 0
        sendCorrectness()
    }

    func sendCorrectness() {
        let size = corrSizes[corrIdx]
        print("Correctness: sending \(size) bytes...", terminator: " ")
        queue([UInt8](repeating: 0x41, count: size), expect: size)
    }

    func startEchoTP() {
        phase = .echoTP
        echoDone = 0
        print("\nEcho throughput: \(ECHO_ROUNDTRIPS) x \(SDU)-byte stop-and-wait...")
        echoStart = Date()
        queue([UInt8](repeating: 0x54, count: SDU), expect: SDU)
    }

    func startSink() {
        // Put peripheral in SINK (receive-only) mode, then blast.
        phase = .sinkSetup
        queue(Array("SINK\u{0}\u{0}".utf8), expect: 0)  // no echo expected
        Timer.scheduledTimer(withTimeInterval: 0.4, repeats: false) { [weak self] _ in
            guard let self = self else { return }
            self.phase = .sink
            print("\nSINK throughput: \(SINK_SDUS) x \(SDU) bytes (host -> device)...")
            self.sinkSent = 0
            self.sinkStart = Date()
            self.outBuf = [UInt8](repeating: 0x53, count: SDU); self.outOff = 0
            self.pump()
        }
    }

    func sinkProgress(wrote n: Int) {
        if outOff >= outBuf.count {
            sinkSent += 1
            if sinkSent >= SINK_SDUS {
                let dt = Date().timeIntervalSince(sinkStart!)
                let kbs = Double(sinkSent * SDU) / dt / 1024.0
                print(String(format: "  %d/%d SDUs in %.2fs -> %.1f KB/s (unidirectional)",
                             sinkSent, SINK_SDUS, dt, kbs))
                // restore echo mode and finish
                phase = .restore
                queue(Array("ECHO\u{0}\u{0}".utf8), expect: 0)
                Timer.scheduledTimer(withTimeInterval: 0.3, repeats: false) { [weak self] _ in
                    self?.finish()
                }
            } else {
                outOff = 0  // next SDU reuses the same buffer
            }
        }
    }

    func finish() {
        phase = .done
        print("\nDone.")
        exit(0)
    }

    // ---- inbound ----

    func onEchoComplete() {
        switch phase {
        case .correctness:
            print("OK")
            corrIdx += 1
            if corrIdx < corrSizes.count { sendCorrectness() }
            else { startEchoTP() }
        case .echoTP:
            echoDone += 1
            if echoDone >= ECHO_ROUNDTRIPS {
                let dt = Date().timeIntervalSince(echoStart!)
                let kbs = Double(echoDone * SDU * 2) / dt / 1024.0
                print(String(format: "  %d roundtrips in %.2fs -> %.1f KB/s (bidirectional), %.1f ms/rt",
                             echoDone, dt, kbs, dt / Double(echoDone) * 1000))
                startSink()
            } else {
                queue([UInt8](repeating: 0x54, count: SDU), expect: SDU)
            }
        default:
            break
        }
    }

    // ---- StreamDelegate ----

    func stream(_ aStream: Stream, handle eventCode: Stream.Event) {
        switch eventCode {
        case .hasSpaceAvailable:
            if aStream === output {
                pump()
                if !didStart {   // first time the output is writable -> begin
                    didStart = true
                    if DEBUG { print("[output writable; starting in 0.8s]") }
                    Timer.scheduledTimer(withTimeInterval: 0.8, repeats: false) { [weak self] _ in
                        self?.startCorrectness()
                    }
                }
            }
        case .hasBytesAvailable:
            guard aStream === input, let inp = input else { return }
            var tmp = [UInt8](repeating: 0, count: 1024)
            while inp.hasBytesAvailable {
                let n = inp.read(&tmp, maxLength: tmp.count)
                if n <= 0 { break }
                if DEBUG { print("[rx \(n)B, expect \(expected), have \(inCount + n)]") }
                if expected > 0 {
                    inCount += n
                    if inCount >= expected {
                        // one message fully echoed back (byte-counted)
                        inCount = 0
                        onEchoComplete()
                    }
                }
                // if expected == 0 (command with no echo) we just drain.
            }
        case .errorOccurred:
            print("Stream error: \(aStream.streamError?.localizedDescription ?? "?")")
            exit(1)
        case .endEncountered:
            if phase != .done { print("Stream closed early."); exit(1) }
        default:
            break
        }
    }
}

setbuf(stdout, nil)   // unbuffered: show progress lines immediately
let client = Client()
RunLoop.main.run()
