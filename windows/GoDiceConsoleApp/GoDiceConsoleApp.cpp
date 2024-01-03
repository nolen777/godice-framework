
#include <future>
#include <iostream>
#include <ostream>
#include <unordered_set>
#include <string>

#include "../GoDiceDll/GoDiceDll.h"

using std::cerr;
using std::endl;
using std::string;

void DeviceFoundCallback(const char* identifier, const char* name);
void DataCallback(const char* identifier, uint32_t data_size, uint8_t* data);
void DeviceConnectedCallback(const char* identifier);
void DeviceConnectionFailedCallback(const char* identifier);
void DeviceDisconnectedCallback(const char* identifier);
void ListenerStoppedCallback(void);

void Log(const char* str);

void RequestColor(const char* identifier);

std::mutex connectedMutex;
std::unordered_set<string> connectedDevices;
std::unordered_set<string> connectingDevices;
std::unordered_map<string, string> deviceNames;

int main(int argc, char* argv[])
{
    cerr << "Starting!" << endl;
    godice_set_logger(Log);
    godice_set_callbacks(
        DeviceFoundCallback,
        DataCallback,
        DeviceConnectedCallback,
        DeviceConnectionFailedCallback,
        DeviceDisconnectedCallback,
        ListenerStoppedCallback);
    godice_start_listening();

    while(1);
    return 0;
}

void DeviceFoundCallback(const char* identifier, const char* name)
{
    {
        std::scoped_lock lk(connectedMutex);
        if (connectedDevices.contains(identifier)) return;
        if (connectingDevices.contains(identifier)) return;
        connectingDevices.insert(identifier);
        deviceNames[identifier] = name;
    }
    cerr << "Device found! " << identifier << " : " << name << endl;
    godice_connect(identifier);
}

void DataCallback(const char* identifier, uint32_t data_size, uint8_t* data)
{
    string& name = deviceNames[identifier];
    cerr << "  " << name <<  " Received data! " << data_size << "b" << " : ";
    for (uint32_t i=0; i<data_size; i++)
    {
        cerr << std::dec << (int)data[i] << " ";
    }
    cerr << " : 0x";
    for (uint32_t i=0; i<data_size; i++)
    {
        cerr << std::hex << (int)data[i];
    }
    cerr << std::dec << endl;
    
    if (data_size > 3 && data[0] == 'C' && data[1] == 'o' && data[2] == 'l')
    {
        cerr <<  "  " << name <<  " Received color " << static_cast<int>(data[3]) << endl;;
    }
}

void DeviceConnectionFailedCallback(const char* identifier)
{
    const string& name = deviceNames[identifier];
    cerr <<  "  " << name << " Device failed to connect! " << identifier << endl;
    {
        std::scoped_lock lk(connectedMutex);
        connectingDevices.erase(identifier);
    }
}


void DeviceConnectedCallback(const char* identifier)
{
    const string& name = deviceNames[identifier];
    cerr << "  " << name << " Device connected! " << identifier << endl;

    {
        std::scoped_lock lk(connectedMutex);
        connectedDevices.insert(identifier);
        connectingDevices.erase(identifier);
    }

    std::string id(identifier);
    std::thread([id]
    {
        RequestColor(id.c_str());
    }).detach();
}

void DeviceDisconnectedCallback(const char* identifier)
{
    const string& name = deviceNames[identifier];
    cerr << "  " << name << " Device disconnected! " << identifier << endl;

    {
        std::scoped_lock lk(connectedMutex);
        connectedDevices.erase(identifier);
        connectingDevices.erase(identifier);
    }

    godice_connect(identifier);
}

void ListenerStoppedCallback(void)
{
    cerr << "  Listener stopped!" << endl;
}

void RequestColor(const char* identifier)
{
    // uint8_t pulseMessage[] = { 
    //     (uint8_t) 16 /* set LED toggle */,
    //     0x05 /* pulse count */,
    //     0x04 /* onTime */,
    //     0x04 /* offTime */,
    //     0xFF /* red */,
    //     0x00 /* green */,
    //     0x00 /* blue */,
    //     0x01,
    //     0x00};
    // godice_send(identifier, sizeof(pulseMessage), pulseMessage);
    
    uint8_t data[] = { 23 };
    godice_send(identifier, 1, data);
}

void Log(const char* str)
{
    cerr << str;
}