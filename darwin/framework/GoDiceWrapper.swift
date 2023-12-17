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

@_cdecl("set_device_found_callback")
func SetDeviceFoundCallback(cb: @escaping (UnsafePointer<CChar>, UnsafePointer<CChar>) -> Void) -> Void {
    btc.setDeviceFoundCallback(cb: { (identifier: String, name: String) in
        cb(identifier.cString(using: .utf8)!, name.cString(using: .utf8)!)
    })
}

@_cdecl("set_data_callback")
func SetDataCallback(cb: @escaping (UnsafePointer<CChar>, UInt32, UnsafePointer<UInt8>?) -> Void) -> Void {
    btc.setDataCallback(cb: { (identifier: String, data: Data) in
        cb(identifier.cString(using: .utf8)!, UInt32(data.count), [UInt8](data))
    })
}

@_cdecl("set_device_connected_callback")
func SetDeviceConnectedCallback(cb: @escaping (UnsafePointer<CChar>) -> Void) -> Void {
    btc.setDeviceConnectedCallback(cb: { (identifier: String) in
        cb(identifier.cString(using: .utf8)!)
    })
}

@_cdecl("set_device_disconnected_callback")
func SetDeviceDisconnectedCallback(cb: @escaping (UnsafePointer<CChar>) -> Void) -> Void {
    btc.setDeviceDisconnectedCallback(cb: { (identifier: String) in
        cb(identifier.cString(using: .utf8)!)
    })
}

@_cdecl("set_listener_stopped_callback")
func SetListenerStoppedCallback(cb: @escaping () -> Void) -> Void {
    btc.setListenerStoppedCallback {
        cb()
    }
}

@_cdecl("connect_device")
func ConnectDevice(identifier: UnsafePointer<CChar>) -> Void {
    btc.connectDevice(identifier: String(cString: identifier))
}

@_cdecl("disconnect_device")
func DisconnectDevice(identifier: UnsafePointer<CChar>) -> Void {
    btc.disconnectDevice(identifier: String(cString: identifier))
}

@_cdecl("send_data")
func SendData(identifier: UnsafePointer<CChar>, byteCount: UInt32, rawBytes: UnsafePointer<UInt8>) {
    btc.sendData(identifier: String(cString: identifier), data: Data(bytes: rawBytes, count: Int(byteCount)))
}

@_cdecl("start_listening")
func StartListening() -> Void {
    btc.listening = true
}

@_cdecl("stop_listening")
func StopListening() -> Void {
    btc.listening = false
}
