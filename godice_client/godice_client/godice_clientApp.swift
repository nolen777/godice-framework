//
//  godice_clientApp.swift
//  godice_client
//
//  Created by Dan Crosby on 11/30/23.
//

import SwiftUI

@main
struct godice_clientApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView(listenForBluetooth: true)
        }
    }
}
