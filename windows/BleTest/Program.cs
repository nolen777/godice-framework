

// See https://aka.ms/new-console-template for more information
using UnityGoDiceInterface;

Console.WriteLine("Hello, World!");

// xcopy /y /d  "$(SolutionDir)x64\Debug\BleWinrtDll.dll" "$(ProjectDir)$(OutDir)"
IDiceInterfaceImports diceInterfaceImports = new NativeDiceInterfaceImports();

diceInterfaceImports.SetCallbacks(
    (addr, name) => { 
        Console.Out.WriteLine($"{addr} {name}");
        diceInterfaceImports.Connect(addr);
    },
    (addr, bytes) => { Console.Out.WriteLine($"{addr} received {bytes.Count} bytes"); }
);

diceInterfaceImports.StartListening();

Thread.Sleep(30000);
