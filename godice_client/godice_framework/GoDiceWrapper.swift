//
//  File.swift
//  godice_client_lib
//
//  Created by Dan Crosby on 12/1/23.
//

private let btc = GoDiceBLEController()

@_cdecl("get_controller")
internal func GetController() -> GoDiceBLEController {
    return btc;
}

@_cdecl("set_dice_vector_callback")
func SetDiceVectorCallback(cb: @escaping (UnsafePointer<CChar>, UInt8, UInt8, UInt8) -> Void) -> Void {
    btc.setDiceVectorCallback(cb: { (name: String, x: UInt8, y: UInt8, z: UInt8) in
        cb(name.cString(using: .utf8)!, x, y, z)
    })
}

@_cdecl("start_listening")
func StartListening() -> Void {
    btc.listening = true
}

@_cdecl("stop_listening")
func StopListening() -> Void {
    btc.listening = false
}
