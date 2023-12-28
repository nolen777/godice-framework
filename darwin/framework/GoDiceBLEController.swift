//
//  BluetoothController.swift
//  LifespanTester
//
//  Created by Dan Crosby on 11/30/22.
//

import Foundation
import CoreBluetooth

public class GoDiceBLEController: NSObject {
    private let centralManager: CBCentralManager
    private let queue = DispatchQueue(label: "goDiceBLEControllerDelegateQueue")
    
    private static let serviceUUID = CBUUID(string: "6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    private static let writeUUID = CBUUID(string: "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    private static let notifyUUID = CBUUID(string: "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    
    private var sessions: [String : DiceSession] = [:]
    
    public typealias DeviceFoundCallback = (String, String) -> Void
    public typealias DataCallback = (String, Data) -> Void
    public typealias DeviceConnectedCallback = (String) -> Void
    public typealias DeviceConnectionFailedCallback = (String) -> Void
    public typealias DeviceDisconnectedCallback = (String) -> Void
    public typealias ListenerStoppedCallback = () -> Void
    
    public typealias Logger = (String) -> Void
    
    private var deviceFoundCallback: DeviceFoundCallback = {_, _ in }
    private var dataCallback: DataCallback = {_,_ in }
    private var deviceConnectedCallback: DeviceConnectedCallback = {_ in }
    private var deviceConnectionFailedCallback: DeviceConnectionFailedCallback = {_ in }
    private var deviceDisconnectedCallback: DeviceDisconnectedCallback = {_ in }
    private var listenerStoppedCallback: ListenerStoppedCallback = {}
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
    public func setLogger(cb: @escaping Logger) -> Void {
        logger = cb
    }
    
    public func connectDevice(identifier: String) -> Void {
        if let session = sessions[identifier] {
            centralManager.connect(session.peripheral)
        }
    }
    
    public func disconnectDevice(identifier: String) -> Void {
        if let session = sessions[identifier] {
            centralManager.cancelPeripheralConnection(session.peripheral)
        }
    }
    
    public func sendData(identifier: String, data: Data) -> Void {
        if let session = sessions[identifier] {
            logger("Sending data!\n")
            session.peripheral.writeValue(data, for: session.writeCharacteristic, type: .withResponse)
        }
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
            print("Starting scan")
            
            for (ident, session) in sessions {
                deviceFoundCallback(ident, session.peripheral.name ?? "")
            }
            
            centralManager.scanForPeripherals(withServices: [GoDiceBLEController.serviceUUID])
        }
    }
    
    func maybeStopScan() {
        if !shouldScan && centralManager.isScanning {
            print("Stopping scan")
            centralManager.stopScan()
            
            listenerStoppedCallback()
        }
    }
    private class DiceSession: NSObject, CBPeripheralDelegate {
        let connectedCallback: (CBPeripheral) -> Void
        let connectionFailedCallback: (CBPeripheral) -> Void
        let dataCallback: (CBPeripheral, Data) -> Void
        let peripheral: CBPeripheral
        var writeCharacteristic: CBCharacteristic!
        
        init(peripheral: CBPeripheral,
             connectedCallback: @escaping(CBPeripheral) -> Void,
             connectionFailedCallback: @escaping(CBPeripheral) -> Void,
             dataCallback: @escaping (CBPeripheral, Data) -> Void) {
            self.peripheral = peripheral
            self.connectedCallback = connectedCallback
            self.connectionFailedCallback = connectionFailedCallback
            self.dataCallback = dataCallback
            super.init()
            peripheral.delegate = self
        }
        
        func run() -> Void {
            peripheral.discoverServices([GoDiceBLEController.serviceUUID])
        }
        
        func send(message: Data) -> Void {
            peripheral.writeValue(message, for: writeCharacteristic, type: .withResponse)
        }
        
        func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
            if let services = peripheral.services {
                for service in services {
                    peripheral.discoverCharacteristics([GoDiceBLEController.writeUUID, GoDiceBLEController.notifyUUID], for: service)
                }
            } else {
                connectionFailedCallback(peripheral)
            }
        }
        
        func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
            if let characteristics = service.characteristics {
                guard let notifyCH = characteristics.first(where: { $0.uuid == GoDiceBLEController.notifyUUID }) else {
                    print("Unable to find notify characteristic")
                    return
                }
                guard let writeCH = characteristics.first(where: { $0.uuid == GoDiceBLEController.writeUUID }) else {
                    print("Unable to find write characteristic")
                    return
                }
                writeCharacteristic = writeCH
                peripheral.setNotifyValue(true, for: notifyCH)
                
                connectedCallback(peripheral)
            } else {
                connectionFailedCallback(peripheral)
            }
        }
        
        func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
            guard let value = characteristic.value else {
                print("unable to fetch data")
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
            print("Unknown bluetooth status")
            
        case .resetting:
            maybeStopScan()
            
        case .unsupported:
            maybeStopScan()
            
        case .unauthorized:
            maybeStopScan()
            
        case .poweredOff:
            maybeStopScan()
            
        @unknown default:
            print("Unknown bluetooth status")
            
        }
    }
    
    public func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        sessions[peripheral.identifier.uuidString] = DiceSession(
            peripheral: peripheral, 
            connectedCallback: deviceConnected,
            connectionFailedCallback: deviceConnectionFailed,
            dataCallback: dataReceived)
        
        deviceFoundCallback(peripheral.identifier.uuidString, peripheral.name ?? "")
    }
    
    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard let name = peripheral.name else {
            print("Peripheral has no name")
            return
        }
        guard let session = sessions[peripheral.identifier.uuidString] else {
            print("No session for \(name)")
            return
        }
        
        session.run()
    }
    
    public func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("Received error \(error?.localizedDescription ?? "unknown")")
        
        sessions.removeValue(forKey: peripheral.identifier.uuidString)
        deviceDisconnectedCallback(peripheral.identifier.uuidString)
    }
    
    func deviceConnected(peripheral: CBPeripheral) {
        deviceConnectedCallback(peripheral.identifier.uuidString)
    }
    
    func deviceConnectionFailed(peripheral: CBPeripheral) {
        deviceConnectionFailedCallback(peripheral.identifier.uuidString)
    }
    
    func dataReceived(peripheral: CBPeripheral, results: Data) {
        dataCallback(peripheral.identifier.uuidString, results)
    }
}
