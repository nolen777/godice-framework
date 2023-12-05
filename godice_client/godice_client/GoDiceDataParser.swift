//
//  GoDiceDataParser.swift
//  godice_client
//
//  Created by Dan Crosby on 12/3/23.
//

import Foundation

class GoDiceDataParser {
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
    
    struct DieVector {
        let x: UInt8
        let y: UInt8
        let z: UInt8
        
        init(_ vector: [UInt8]) {
            x = vector[0]
            y = vector[1]
            z = vector[2]
        }
    }
    
    enum DiceResults {
        case Connected
        case RollStarted
        case ColorFetched(value: UInt8)
        case BatteryLevel(value: UInt8)
        case Stable(vector: DieVector)
        case FakeStable(vector: DieVector)
        case TiltStable(vector: DieVector)
        case MoveStable(vector: DieVector)
    }
    
    
    private func rollVector(_ rawData: Data) -> DieVector? {
        guard let firstByte = rawData.first, firstByte == 83 else {
            print("Bad raw data \(rawData)")
            return nil
        }
        
        return DieVector([UInt8](rawData.advanced(by: 1)))
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
            return rollVector(rawData).map { .Stable(vector: $0) }
            
        case 70:
            return rollVector(rawData.advanced(by: 1)).map { .FakeStable(vector: $0) }
            
        case 84:
            return rollVector(rawData.advanced(by: 1)).map { .TiltStable(vector: $0) }
            
        case 77:
            return rollVector(rawData.advanced(by: 1)).map { .MoveStable(vector: $0) }
            
        default:
            print("Unknown first byte \(firstByte)")
            return nil
        }
    }
    
    /*
     switch (results) {
     case .Connected:
         if colors[name] == nil {
             session.fetchColor()
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
      //   print("received values \(values) for color \(colors[name] ?? .Unknown)")
         diceVectorCallback(name, values.x, values.y, values.z)
         break
     }
     */
}
