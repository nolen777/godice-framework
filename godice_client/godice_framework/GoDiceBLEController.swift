//
//  BluetoothController.swift
//  LifespanTester
//
//  Created by Dan Crosby on 11/30/22.
//

import Foundation
import CoreBluetooth

class GoDiceBLEController: NSObject {
    private let centralManager: CBCentralManager
    private let queue = DispatchQueue(label: "goDiceBLEControllerDelegateQueue")
    
    private static let serviceUUID = CBUUID(string: "6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    private static let writeUUID = CBUUID(string: "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    private static let notifyUUID = CBUUID(string: "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    
    private var sessions: [String : DiceSession] = [:]
    
    typealias DataCallback = (String, Data?) -> Void
    
    private var dataCallback: DataCallback = {_,_ in }
    
    func setDataCallback(cb: @escaping DataCallback) -> Void {
        dataCallback = cb
    }
    
    var listening: Bool = false {
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

    override init() {
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
            
            centralManager.scanForPeripherals(withServices: [GoDiceBLEController.serviceUUID])
        }
    }
    
    func maybeStopScan() {
        if !shouldScan && centralManager.isScanning {
            print("Stopping scan")
            centralManager.stopScan()
            
            sessions.forEach { (name, session) in
                centralManager.cancelPeripheralConnection(session.peripheral)
            }
            sessions = [:]
        }
    }
    private class DiceSession: NSObject, CBPeripheralDelegate {
        let updateCallback: (CBPeripheral, Data?) -> Void
        let peripheral: CBPeripheral
        var writeCharacteristic: CBCharacteristic!
        
        init(peripheral: CBPeripheral, updateCallback: @escaping (CBPeripheral, Data?) -> Void) {
            self.peripheral = peripheral
            self.updateCallback = updateCallback
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
            }
            
            updateCallback(peripheral, nil)
        }
        
        func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
            guard let value = characteristic.value else {
                print("unable to fetch data")
                return
            }
            
            updateCallback(peripheral, value)
        }
    }
}

extension GoDiceBLEController: CBCentralManagerDelegate, CBPeripheralDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
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
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String : Any], rssi RSSI: NSNumber) {
        guard let name = peripheral.name else {
            print("Peripheral has no name")
            return
        }
        
        sessions[name] = DiceSession(peripheral: peripheral, updateCallback: sessionUpdated)
        centralManager.connect(peripheral)
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard let name = peripheral.name else {
            print("Peripheral has no name")
            return
        }
        guard let session = sessions[name] else {
            print("No session for \(name)")
            return
        }
        
        session.run()
    }
    
    func sessionUpdated(peripheral: CBPeripheral, results: Data?) {
        guard let name = peripheral.name else {
            print("Peripheral has no name!")
            return
        }
        
        dataCallback(name, results)
    }
}
