//
//  ContentView.swift
//  godice_client
//
//  Created by Dan Crosby on 11/30/23.
//

import SwiftUI

struct ContentView: View {
    let btc = GoDiceBLEController()
    let parser = GoDiceDataParser()
    @State var listenForBluetooth: Bool
    
    var body: some View {
        VStack {
            Toggle("Listen for Treadmill",
                   isOn: $listenForBluetooth)
            .onChange(of: listenForBluetooth) { newValue in
                btc.listening = newValue
            }
        }
        .padding().onAppear {
            btc.listening = listenForBluetooth
            
            btc.setDataCallback { (name, data) in
                if let data = data {
                    if let result = parser.possibleDieRollData(rawData: data) {
                        print("\(name) received \(result)")
                    } else {
                        print("\(name) received \(data.count) but failed to parse")
                    }
                } else {
                    print("Connected: \(name)")
                }
            }
        }
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView(listenForBluetooth: true)
    }
}
