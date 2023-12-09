//
//  GodiceClientAppApp.swift
//  GodiceClientApp
//
//  Created by Dan Crosby on 12/7/23.
//

import SwiftUI

@main
struct GodiceClientApp : App {
    var body: some Scene {
        WindowGroup {
            ContentView(listenForBluetooth: true)
        }
    }
}
