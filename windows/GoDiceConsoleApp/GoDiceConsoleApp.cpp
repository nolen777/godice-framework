
#include <future>
#include <iostream>
#include <ostream>
#include "../GoDiceDll/GoDiceDll.h"

using std::cerr;
using std::endl;

void DeviceFoundCallback(const char* identifier, const char* name);
void DataCallback(const char* identifier, uint32_t data_size, uint8_t* data);
void DeviceConnectedCallback(const char* identifier);
void DeviceDisconnectedCallback(const char* identifier);
void ListenerStoppedCallback(void);

void Log(const char* str);

void RequestColor(const char* identifier);

int main(int argc, char* argv[])
{
    cerr << "Hello, World!" << endl;
    godice_set_logger(Log);
    godice_set_callbacks(DeviceFoundCallback, DataCallback, DeviceConnectedCallback, DeviceDisconnectedCallback, ListenerStoppedCallback);
    godice_start_listening();

    while(1);
    return 0;
}

void DeviceFoundCallback(const char* identifier, const char* name)
{
    cerr << "Device found! " << identifier << " : " << name << endl;
    godice_connect(identifier);
}

void DataCallback(const char* identifier, uint32_t data_size, uint8_t* data)
{
    cerr << "Received data! " << identifier << " : " << data_size << "b" << " : ";
    for (auto i=0; i<data_size; i++)
    {
        cerr << std::dec << (int)data[i] << " ";
    }
    cerr << " : 0x";
    for (auto i=0; i<data_size; i++)
    {
        cerr << std::hex << (int)data[i];
    }
    cerr << std::dec << endl;
    
    if (data_size > 3 && data[0] == 'C' && data[1] == 'o' && data[2] == 'l')
    {
        cerr << "Received color " << (int)data[3] << endl;;
    }
}

void DeviceConnectedCallback(const char* identifier)
{
    cerr << "Device connected! " << identifier << endl;

    std::string id(identifier);
    std::async(std::launch::async, [id]
    {
        RequestColor(id.c_str());
    });
}

void DeviceDisconnectedCallback(const char* identifier)
{
    cerr << "Device disconnected! " << identifier << endl;
}

void ListenerStoppedCallback(void)
{
    cerr << "Listener stopped!" << endl;
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