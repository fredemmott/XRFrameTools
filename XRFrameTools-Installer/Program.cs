using System.CommandLine;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices.Marshalling;
using WixSharp;
using WixSharp.CommonTasks;
using WixSharp.Controls;
using File = WixSharp.File;

[assembly: InternalsVisibleTo(assemblyName: "XRFrameTools_Installer.aot")] // assembly name + '.aot suffix

async Task<int> CreateMsi(DirectoryInfo inputRoot, string? signingKeyId, string? timestampServer)
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

    const string apiLayersKey = @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit";
    foreach (var file in inputRoot.GetDirectories("lib").First().GetFiles("XR_APILAYER_*64.json"))
    {
        project.AddRegValue(new RegValue(RegistryHive.LocalMachine, apiLayersKey,
            $"[INSTALLDIR]lib\\{file.Name}", 0));
    }
    
    project.GUID = Guid.Parse("e3334ff2-9b8f-4f3c-ba25-5f965f3b7dc9");
    project.Platform = Platform.x64;

    project.SourceBaseDir = inputRoot.FullName;

    project.ControlPanelInfo.Manufacturer = "Fred Emmott";
    project.LicenceFile = "installer/LICENSE.rtf";

    if (signingKeyId != null)
    {
        project.DigitalSignature = new DigitalSignature
        {
            CertificateId = signingKeyId,
            CertificateStore = StoreType.sha1Hash,
            HashAlgorithm = HashAlgorithmType.sha256,
            Description = project.OutFileName,
            TimeUrl = (timestampServer == null) ? null : new Uri(timestampServer),
        };
        project.SignAllFiles = true;
    }
    else
    {
        project.OutFileName += "-UNSIGNED";
    }

    project.BuildMsi();

    return 0;
}

var inputRootArg = new Argument<DirectoryInfo>(
    name: "INPUT_ROOT",
    description: "Location of files to include in the installer");
var signingKeyArg = new Option<string>(
    name: "--signing-key",
    description: "Code signing key ID");
var timestampServerArg = new Option<string>(
    name: "--timestamp-server",
    description: "Code signing timestamp server");
    
var command = new RootCommand("Build the MSI");
command.Add(inputRootArg);
command.Add(signingKeyArg);
command.Add(timestampServerArg);
command.SetHandler(
    CreateMsi, inputRootArg, signingKeyArg, timestampServerArg);
return await command.InvokeAsync(args);