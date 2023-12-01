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
        case Black = 0
        case Red = 1
        case Green = 2
        case Blue = 3
        case Yellow = 4
        case Orange = 5
    }
    
    enum DiceResults {
        case RollStarted(color: DiceColor)
        case BatteryLevel(color: Dice)
        case Stable(color: DiceColor, values: [UInt8])
        case FakeStable(color: DiceColor, values: [UInt8])
        case TiltStable(color: DiceColor, values: [UInt8])
        case MoveStable(color: DiceColor, values: [UInt8])
    }
    
    class DiceSession: NSObject, CBPeripheralDelegate {
        var color: DiceColor!
        let updateCallback: (CBPeripheral, DiceResults) -> Void
        let peripheral: CBPeripheral
        
        init(peripheral: CBPeripheral, updateCallback: @escaping (CBPeripheral, DiceResults) -> Void) {
            self.peripheral = peripheral
            self.updateCallback = updateCallback
            super.init()
            peripheral.delegate = self
        }
        
        func run() -> Void {
            peripheral.discoverServices([BluetoothController.serviceUUID])
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
                
                peripheral.setNotifyValue(true, for: notifyCH)
                
                peripheral.writeValue(Data([MessageIdentifier.DiceColor.rawValue]), for: writeCH, type: .withResponse)
            }
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
                return DiceResults.RollStarted(color: color)
                
                case 66:
                print("battery level received")
                return nil
                
                case 67:
                print("dice color received: \(rawData[3])")
                color = DiceColor(rawValue: rawData[3])
                return nil
                
                case 83:
                return rollVector(rawData).map { .Stable(color: color, values: $0) }
                
                case 70:
                return rollVector(rawData.advanced(by: 1)).map { .FakeStable(color: color, values: $0) }
                
                case 84:
                return rollVector(rawData.advanced(by: 1)).map { .TiltStable(color: color, values: $0) }
                
                case 77:
                return rollVector(rawData.advanced(by: 1)).map { .MoveStable(color: color, values: $0) }
                
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
        switch (results) {
            case .RollStarted:
            print("roll started")
            break
            
        case let .Stable(color, values), 
            let .FakeStable(color, values),
            let .TiltStable(color, values),
            let .MoveStable(color, values):
            print("received values \(values) for color \(color)")
            break
        }
    }
}
