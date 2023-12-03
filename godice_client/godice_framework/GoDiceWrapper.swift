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
