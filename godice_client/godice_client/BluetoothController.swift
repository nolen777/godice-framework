//
//  BluetoothController.swift
//  LifespanTester
//
//  Created by Dan Crosby on 11/30/22.
//

import Foundation
import CoreBluetooth

class BluetoothController: NSObject {
    private var centralManager: CBCentralManager!
    
    static let serviceUUID = CBUUID(string: "6e400001-b5a3-f393-e0a9-e50e24dcca9e")
    static let writeUUID = CBUUID(string: "6e400002-b5a3-f393-e0a9-e50e24dcca9e")
    static let notifyUUID = CBUUID(string: "6e400003-b5a3-f393-e0a9-e50e24dcca9e")
    
    var colors: [String : DiceColor] = [:]
    var sessions: [String : DiceSession] = [:]
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
    
    var shouldScan: Bool {
        return centralManager.state == .poweredOn && listening
    }
    
    func maybeStartScan() {
        if shouldScan && !centralManager.isScanning {
            print("Starting scan")
            
            centralManager.scanForPeripherals(withServices: [BluetoothController.serviceUUID])
        }
    }
    
    func maybeStopScan() {
        if !shouldScan && centralManager.isScanning {
            print("Stopping scan")
            centralManager.stopScan()
        }
    }
    
    func setUp() {
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }
    
    enum MessageIdentifier: UInt8 {
        case BatteryLevel = 3
        case DiceColor = 23
        case SetLed = 8
        case SetLedToggle = 16
    }
    
    enum DiceColor: UInt8 {
        case Unknown = 99
        case Black = 0
        case Red = 1
        case Green = 2
        case Blue = 3
        case Yellow = 4
        case Orange = 5
    }
    
    enum DiceResults {
        case Connected
        case RollStarted
        case ColorFetched(value: UInt8)
        case BatteryLevel(value: UInt8)
        case Stable(values: [UInt8])
        case FakeStable(values: [UInt8])
        case TiltStable(values: [UInt8])
        case MoveStable(values: [UInt8])
    }
    
    class DiceSession: NSObject, CBPeripheralDelegate {
        let updateCallback: (CBPeripheral, DiceResults) -> Void
        let peripheral: CBPeripheral
        var writeCharacteristic: CBCharacteristic!
        
        init(peripheral: CBPeripheral, updateCallback: @escaping (CBPeripheral, DiceResults) -> Void) {
            self.peripheral = peripheral
            self.updateCallback = updateCallback
            super.init()
            peripheral.delegate = self
        }
        
        func run() -> Void {
            peripheral.discoverServices([BluetoothController.serviceUUID])
        }
        
        func fetchColor() -> Void {
            peripheral.writeValue(Data([MessageIdentifier.DiceColor.rawValue]), for: writeCharacteristic, type: .withResponse)
        }
        
        func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
            if let services = peripheral.services {
                for service in services {
                    peripheral.discoverCharacteristics([BluetoothController.writeUUID, BluetoothController.notifyUUID], for: service)
                }
            }
        }
        
        func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
            if let characteristics = service.characteristics {
                guard let notifyCH = characteristics.first(where: { $0.uuid == BluetoothController.notifyUUID }) else {
                    print("Unable to find notify characteristic")
                    return
                }
                guard let writeCH = characteristics.first(where: { $0.uuid == BluetoothController.writeUUID }) else {
                    print("Unable to find write characteristic")
                    return
                }
                writeCharacteristic = writeCH
                peripheral.setNotifyValue(true, for: notifyCH)
            }
            
            updateCallback(peripheral, .Connected)
        }
        
        private func rollVector(_ rawData: Data) -> [UInt8]? {
            guard let firstByte = rawData.first, firstByte == 83 else {
                print("Bad raw data \(rawData)")
                return nil
            }
            
            return [UInt8](rawData.advanced(by: 1))
        }
        
        func possibleDieRollData(rawData: Data) -> DiceResults? {
            guard (!rawData.isEmpty) else {
                return nil
            }
            
            let firstByte = rawData[0]
            switch (firstByte) {
            case 82:
                return DiceResults.RollStarted
                
            case 66:
                if rawData[1] == 97 && rawData[2] == 116 {
                    print("battery level received: \(rawData[3])")
                    return .BatteryLevel(value: rawData[3])
                } else {
                    print("Doesn't match expected battery data")
                    return nil
                }
                
            case 67:
                if rawData[1] == 111 && rawData[2] == 108 {
                    print("dice color received: \(rawData[3])")
                    return .ColorFetched(value: rawData[3])
                } else {
                    print("Doesn't match expected color data")
                    return nil
                }
                
            case 83:
                return rollVector(rawData).map { .Stable(values: $0) }
                
            case 70:
                return rollVector(rawData.advanced(by: 1)).map { .FakeStable(values: $0) }
                
            case 84:
                return rollVector(rawData.advanced(by: 1)).map { .TiltStable(values: $0) }
                
            case 77:
                return rollVector(rawData.advanced(by: 1)).map { .MoveStable(values: $0) }
                
            default:
                print("Unknown first byte \(firstByte)")
                return nil
            }
        }
        
        func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
            guard let value = characteristic.value else {
                print("unable to fetch data")
                return
            }
            
            guard let rollResults = possibleDieRollData(rawData: value) else {
                print("No roll values received")
                return
            }
            
            updateCallback(peripheral, rollResults)
        }
    }
}

extension BluetoothController: CBCentralManagerDelegate, CBPeripheralDelegate {
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
    
    func sessionUpdated(peripheral: CBPeripheral, results: DiceResults) {
        guard let name = peripheral.name else {
            print("Peripheral has no name!")
            return
        }
        guard let session = sessions[name] else {
            print("No session exists for \(name)")
            return
        }
        
        switch (results) {
        case .Connected:
            if colors[name] == nil {
                sessions[name]?.fetchColor()
            }
            break
            
            
        case .ColorFetched(value: let value):
            colors[name] = DiceColor(rawValue: value)
            break
            
        case .BatteryLevel(value: let value):
            print("Received battery level \(value)")
            
        case .RollStarted:
            print("roll started")
            break
            
        case let .Stable(values),
            let .FakeStable(values),
            let .TiltStable(values),
            let .MoveStable(values):
            print("received values \(values) for color \(colors[name] ?? .Unknown)")
            break
        }
    }
}
