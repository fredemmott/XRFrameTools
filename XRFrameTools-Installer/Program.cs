using System.CommandLine;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices.Marshalling;
using WixSharp;
using WixSharp.CommonTasks;
using File = WixSharp.File;

[assembly: InternalsVisibleTo(assemblyName: "XRFrameTools_Installer.aot")] // assembly name + '.aot suffix


async Task<int> CreateMSI(DirectoryInfo inputRoot)
{
    if (!System.IO.File.Exists($"{inputRoot}/bin/XRFrameTools.exe"))
    {
        Console.WriteLine($"Cannot find bin/XRFrameTools.exe in INPUT_ROOT ({inputRoot.FullName})");
        return 1;
    }

    var installerResources = new Feature("Installer Resources");
    installerResources.IsEnabled = false;

    var project =
        new ManagedProject("XRFrameTools",
            new Dir(@"%ProgramFiles%\XRFrameTools",
                new Dir("bin", new Files("bin/*.*")),
                new Dir("lib", new Files("lib/*.*")),
                new Dir("share", new Files("share/*.*")),
                    new Files(installerResources, @"installer/*.*")));

    project.GUID = Guid.Parse("e3334ff2-9b8f-4f3c-ba25-5f965f3b7dc9");
    project.Platform = Platform.x64;
    
    project.SourceBaseDir = inputRoot.FullName;

    project.ControlPanelInfo.Manufacturer = "Fred Emmott";
    project.LicenceFile = "installer/LICENSE.rtf";

    project.BuildMsi();

    return 0;
}

var inputArg = new Argument<DirectoryInfo>(
    name: "INPUT_ROOT",
    description: "Location of files to include in the installer");
var command = new RootCommand("Build the MSI");
command.Add(inputArg);
command.SetHandler(
    CreateMSI, inputArg);
return await command.InvokeAsync(args);