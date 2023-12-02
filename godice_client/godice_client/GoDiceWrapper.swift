//
//  File.swift
//  godice_client_lib
//
//  Created by Dan Crosby on 12/1/23.
//

let btc = GoDiceBLEController()

@_cdecl("start_listening")
public func start_listening() {
    btc.listening = true
}

@_cdecl("stop_listening")
public func stop_listening() {
    btc.listening = false
}

