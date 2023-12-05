//
//  File.swift
//  godice_client_lib
//
//  Created by Dan Crosby on 12/1/23.
//

import Foundation

private let btc = GoDiceBLEController()

@_cdecl("get_controller")
internal func GetController() -> GoDiceBLEController {
    return btc;
}

@_cdecl("set_data_callback")
func SetDataCallback(cb: @escaping (UnsafePointer<CChar>, UInt32, UnsafePointer<UInt8>?) -> Void) -> Void {
    btc.setDataCallback(cb: { (name: String, data: Data?) in
        if let data = data {
            cb(name.cString(using: .utf8)!, UInt32(data.count), [UInt8](data))
        } else {
            cb(name.cString(using: .utf8)!, 0, nil)
        }
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
