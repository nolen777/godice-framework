//
//  ContentView.swift
//  godice_client
//
//  Created by Dan Crosby on 11/30/23.
//

import SwiftUI
import GodiceClient

final class GoDiceClientModel: ObservableObject {
    private let controller = GoDiceBLEController()
    private let parser = GoDiceDataParser()

    init() {
        controller.setDeviceFoundCallback { [weak self] identifier, name in
            print("Found \(name): \(identifier)")
            self?.controller.connectDevice(identifier: identifier)
        }

        controller.setDeviceConnectedCallback { identifier in
            print("Connected: \(identifier)")
        }

        controller.setDeviceConnectionFailedCallback { identifier in
            print("Connection failed: \(identifier)")
        }

        controller.setDeviceDisconnectedCallback { identifier in
            print("Disconnected: \(identifier)")
        }

        controller.setDeviceStateCallback { identifier, state, reason, nativeStatus, milliseconds, detail in
            print("[\(milliseconds) ms] \(identifier) state=\(state) reason=\(reason) status=\(nativeStatus): \(detail)")
        }

        controller.setDataCallback { [weak self] name, data in
            guard let self = self else {
                return
            }
            if let result = self.parser.possibleDieRollData(rawData: data) {
                print("\(name) received \(result)")
            } else {
                print("\(name) received \(data.count) bytes but failed to parse")
            }
        }
    }

    func setListening(_ listening: Bool) {
        controller.listening = listening
    }
}

struct ContentView: View {
    @StateObject private var client = GoDiceClientModel()
    @State var listenForBluetooth: Bool
    
    var body: some View {
        VStack {
            Toggle("Listen for Treadmill",
                   isOn: $listenForBluetooth)
            .onChange(of: listenForBluetooth) { newValue in
                client.setListening(newValue)
            }
        }
        .padding().onAppear {
            client.setListening(listenForBluetooth)
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView(listenForBluetooth: true)
    }
}
