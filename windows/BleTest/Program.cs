

// See https://aka.ms/new-console-template for more information
using UnityGoDiceInterface;

Console.WriteLine("Hello, World!");

// xcopy /y /d  "$(SolutionDir)x64\Debug\BleWinrtDll.dll" "$(ProjectDir)$(OutDir)"
IDiceInterfaceImports diceInterfaceImports = new NativeDiceInterfaceImports();

diceInterfaceImports.SetCallback((name, bytes) => {
   Console.Out.WriteLine(name);
});

diceInterfaceImports.StartListening();

Thread.Sleep(30000);
