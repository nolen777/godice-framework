//
//  BluetoothController.swift
//  LifespanTester
//
//  Created by Dan Crosby on 11/30/22.
//

import Foundation
import CoreBluetooth

public class GoDiceBLEController: NSObject {
    public enum ConnectionState: UInt32 {
        case unknown = 0
        case discovered = 1
        case connecting = 2
        case subscribing = 3
        case ready = 4
        case disconnecting = 5
        case disconnected = 6
        case retryWait = 7
    }

    public enum ConnectionReason: UInt32 {
        case none = 0
        case requested = 1
        case linkLoss = 2
        case connectionFailed = 3
        case protocolError = 4
        case adapterUnavailable = 5
        case reset = 6
    }

    private let centralManager: CBCentralManager
    private let queue = DispatchQueue(label: "goDiceBLEControllerDelegateQueue")
    
    private static let serviceUUID = CBUUID(string: "6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    private static let writeUUID = CBUUID(string: "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    private static let notifyUUID = CBUUID(string: "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    
    private var sessions: [String : DiceSession] = [:]
    private var pendingDisconnectReasons: [String : ConnectionReason] = [:]
    
    public typealias DeviceFoundCallback = (String, String) -> Void
    public typealias DataCallback = (String, Data) -> Void
    public typealias DeviceConnectedCallback = (String) -> Void
    public typealias DeviceConnectionFailedCallback = (String) -> Void
    public typealias DeviceDisconnectedCallback = (String) -> Void
    public typealias ListenerStoppedCallback = () -> Void
    public typealias DeviceStateCallback = (String, ConnectionState, ConnectionReason, Int32, UInt64, String) -> Void
    
    public typealias Logger = (String) -> Void
    
    private var deviceFoundCallback: DeviceFoundCallback = {_, _ in }
    private var dataCallback: DataCallback = {_,_ in }
    private var deviceConnectedCallback: DeviceConnectedCallback = {_ in }
    private var deviceConnectionFailedCallback: DeviceConnectionFailedCallback = {_ in }
    private var deviceDisconnectedCallback: DeviceDisconnectedCallback = {_ in }
    private var listenerStoppedCallback: ListenerStoppedCallback = {}
    private var deviceStateCallback: DeviceStateCallback = {_, _, _, _, _, _ in }
    private var logger: Logger = {_ in}
    
    public func setDeviceFoundCallback(cb: @escaping DeviceFoundCallback) -> Void {
        deviceFoundCallback = cb
    }
    public func setDataCallback(cb: @escaping DataCallback) -> Void { dataCallback = cb }
    public func setDeviceConnectedCallback(cb: @escaping DeviceConnectedCallback) -> Void {
        deviceConnectedCallback = cb
    }    
    public func setDeviceConnectionFailedCallback(cb: @escaping DeviceConnectionFailedCallback) -> Void {
        deviceConnectionFailedCallback = cb
    }
    public func setDeviceDisconnectedCallback(cb: @escaping DeviceDisconnectedCallback) -> Void {
        deviceDisconnectedCallback = cb
    }
    public func setListenerStoppedCallback(cb: @escaping ListenerStoppedCallback) -> Void {
        listenerStoppedCallback = cb
    }
    public func setDeviceStateCallback(cb: @escaping DeviceStateCallback) -> Void {
        deviceStateCallback = cb
    }
    public func setLogger(cb: @escaping Logger) -> Void {
        logger = cb
    }
    
    public func connectDevice(identifier: String) -> Void {
        if let session = sessions[identifier] {
            emitState(identifier: identifier, state: .connecting, detail: "connection requested")
            centralManager.connect(session.peripheral)
        } else {
            logger("No session found for \(identifier)")
            emitState(identifier: identifier,
                      state: .disconnected,
                      reason: .connectionFailed,
                      detail: "no discovered session")
            deviceConnectionFailedCallback(identifier)
        }
    }
    
    public func disconnectDevice(identifier: String) -> Void {
        if let session = sessions[identifier] {
            pendingDisconnectReasons[identifier] = .requested
            emitState(identifier: identifier,
                      state: .disconnecting,
                      reason: .requested,
                      detail: "disconnect requested")
            centralManager.cancelPeripheralConnection(session.peripheral)
        }
    }
    
    public func sendData(identifier: String, data: Data) -> Void {
        guard let session = sessions[identifier] else {
            logger("No session found for \(identifier)")
            return
        }
        guard session.peripheral.state == .connected else {
            logger("Cannot send to disconnected device \(identifier)")
            return
        }
        guard let writeCharacteristic = session.writeCharacteristic else {
            logger("Cannot send before write characteristic is ready for \(identifier)")
            return
        }

        logger("Sending \(data.count) bytes to \(identifier)\n")
        session.peripheral.writeValue(data, for: writeCharacteristic, type: .withResponse)
    }
    
    public var listening: Bool = false {
        didSet {
            guard listening != oldValue else {
                return
            }
            if listening {
                maybeStartScan()
            } else {
                maybeStopScan()
            }
        }
    }
    
    func reset() {
        listening = false
        
        for (identifier, session) in sessions {
            pendingDisconnectReasons[identifier] = .reset
            emitState(identifier: identifier,
                      state: .disconnecting,
                      reason: .reset,
                      detail: "controller reset")
            centralManager.cancelPeripheralConnection(session.peripheral)
        }
        sessions.removeAll()
    }

    private func emitState(identifier: String,
                           state: ConnectionState,
                           reason: ConnectionReason = .none,
                           nativeStatus: Int32 = 0,
                           detail: String) {
        let milliseconds = DispatchTime.now().uptimeNanoseconds / 1_000_000
        deviceStateCallback(identifier, state, reason, nativeStatus, milliseconds, detail)
    }
    
    public override init() {
        centralManager = CBCentralManager(delegate: nil, queue: queue)
        super.init()
        centralManager.delegate = self
    }
    
    var shouldScan: Bool {
        return centralManager.state == .poweredOn && listening
    }
    
    func maybeStartScan() {
        if shouldScan && !centralManager.isScanning {
            logger("Starting scan")
            
            for (ident, session) in sessions {
                deviceFoundCallback(ident, session.peripheral.name ?? "")
            }
            
            centralManager.scanForPeripherals(withServices: [GoDiceBLEController.serviceUUID])
        }
    }
    
    func maybeStopScan() {
        if !shouldScan && centralManager.isScanning {
            logger("Stopping scan")
            centralManager.stopScan()
            
            listenerStoppedCallback()
        }
    }
    private class DiceSession: NSObject, CBPeripheralDelegate {
        let connectedCallback: (CBPeripheral) -> Void
        let connectionFailedCallback: (CBPeripheral) -> Void
        let dataCallback: (CBPeripheral, Data) -> Void
        let peripheral: CBPeripheral
        let logger: Logger
        var writeCharacteristic: CBCharacteristic?
        
        init(peripheral: CBPeripheral,
             connectedCallback: @escaping(CBPeripheral) -> Void,
             connectionFailedCallback: @escaping(CBPeripheral) -> Void,
             dataCallback: @escaping (CBPeripheral, Data) -> Void,
        logger: @escaping Logger) {
            self.peripheral = peripheral
            self.connectedCallback = connectedCallback
            self.connectionFailedCallback = connectionFailedCallback
            self.dataCallback = dataCallback
            self.logger = logger
            super.init()
            peripheral.delegate = self
        }
        
        func run() -> Void {
            peripheral.discoverServices([GoDiceBLEController.serviceUUID])
        }
        
        func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
            if let error = error {
                logger("Service discovery failed: \(error.localizedDescription)")
                connectionFailedCallback(peripheral)
                return
            }
            guard let services = peripheral.services, !services.isEmpty else {
                logger("No GoDice service found")
                connectionFailedCallback(peripheral)
                return
            }

            for service in services {
                peripheral.discoverCharacteristics([GoDiceBLEController.writeUUID, GoDiceBLEController.notifyUUID], for: service)
            }
        }
        
        func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
            if let error = error {
                logger("Characteristic discovery failed: \(error.localizedDescription)")
                connectionFailedCallback(peripheral)
                return
            }
            if let characteristics = service.characteristics {
                guard let notifyCH = characteristics.first(where: { $0.uuid == GoDiceBLEController.notifyUUID }) else {
                    logger("Unable to find notify characteristic")
                    connectionFailedCallback(peripheral)
                    return
                }
                guard let writeCH = characteristics.first(where: { $0.uuid == GoDiceBLEController.writeUUID }) else {
                    logger("Unable to find write characteristic")
                    connectionFailedCallback(peripheral)
                    return
                }
                writeCharacteristic = writeCH
                peripheral.setNotifyValue(true, for: notifyCH)
            } else {
                connectionFailedCallback(peripheral)
            }
        }

        func peripheral(_ peripheral: CBPeripheral,
                        didUpdateNotificationStateFor characteristic: CBCharacteristic,
                        error: Error?) {
            if let error = error {
                logger("Notification subscription failed: \(error.localizedDescription)")
                connectionFailedCallback(peripheral)
                return
            }
            guard characteristic.uuid == GoDiceBLEController.notifyUUID,
                  characteristic.isNotifying,
                  writeCharacteristic != nil else {
                logger("Notification characteristic is not ready")
                connectionFailedCallback(peripheral)
                return
            }

            connectedCallback(peripheral)
        }
        
        func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
            guard let value = characteristic.value else {
                logger("unable to fetch data")
                return
            }
            
            dataCallback(peripheral, value)
        }
    }
}

extension GoDiceBLEController: CBCentralManagerDelegate, CBPeripheralDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            maybeStartScan()
            
        case .unknown:
            logger("Unknown bluetooth status")
            
        case .resetting:
            maybeStopScan()
            
        case .unsupported:
            maybeStopScan()
            
        case .unauthorized:
            maybeStopScan()
            
        case .poweredOff:
            maybeStopScan()
            
        @unknown default:
            logger("Unknown bluetooth status")
            
        }
    }
    
    public func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        let identifier = peripheral.identifier.uuidString
        guard sessions[identifier] == nil else {
            return
        }

        sessions[identifier] = DiceSession(
            peripheral: peripheral, 
            connectedCallback: deviceConnected,
            connectionFailedCallback: deviceConnectionFailed,
            dataCallback: dataReceived,
        logger: logger)

        emitState(identifier: identifier, state: .discovered, detail: "device discovered")
        deviceFoundCallback(identifier, peripheral.name ?? "")
    }
    
    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard let name = peripheral.name else {
            logger("Peripheral has no name")
            return
        }
        guard let session = sessions[peripheral.identifier.uuidString] else {
            logger("No session for \(name)")
            return
        }

        emitState(identifier: peripheral.identifier.uuidString,
                  state: .subscribing,
                  detail: "discovering services and subscribing")
        session.run()
    }

    public func centralManager(_ central: CBCentralManager,
                               didFailToConnect peripheral: CBPeripheral,
                               error: Error?) {
        logger("Failed to connect \(peripheral.identifier.uuidString): \(error?.localizedDescription ?? "unknown error")")
        emitState(identifier: peripheral.identifier.uuidString,
                  state: .disconnected,
                  reason: .connectionFailed,
                  nativeStatus: Int32(clamping: (error as NSError?)?.code ?? 0),
                  detail: error?.localizedDescription ?? "connection failed")
        deviceConnectionFailedCallback(peripheral.identifier.uuidString)
    }
    
    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        logger("Received error \(error?.localizedDescription ?? "unknown")")

        let identifier = peripheral.identifier.uuidString
        let reason = pendingDisconnectReasons.removeValue(forKey: identifier) ?? .linkLoss
        emitState(identifier: identifier,
                  state: .disconnected,
                  reason: reason,
                  nativeStatus: Int32(clamping: (error as NSError?)?.code ?? 0),
                  detail: error?.localizedDescription ?? (reason == .linkLoss ? "connection lost" : "disconnected"))
        deviceDisconnectedCallback(identifier)
    }
    
    func deviceConnected(peripheral: CBPeripheral) {
        emitState(identifier: peripheral.identifier.uuidString,
                  state: .ready,
                  detail: "notification subscription ready")
        deviceConnectedCallback(peripheral.identifier.uuidString)
    }
    
    func deviceConnectionFailed(peripheral: CBPeripheral) {
        emitState(identifier: peripheral.identifier.uuidString,
                  state: .disconnected,
                  reason: .connectionFailed,
                  detail: "connection setup failed")
        deviceConnectionFailedCallback(peripheral.identifier.uuidString)
    }
    
    func dataReceived(peripheral: CBPeripheral, results: Data) {
        dataCallback(peripheral.identifier.uuidString, results)
    }
}
