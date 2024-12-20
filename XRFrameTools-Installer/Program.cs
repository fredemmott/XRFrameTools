using System.CommandLine;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices.Marshalling;
using WixSharp;
using WixSharp.CommonTasks;
using WixSharp.Controls;
using File = WixSharp.File;
using System.Text.Json;
using System.Text.Json.Serialization;

[assembly: InternalsVisibleTo(assemblyName: "XRFrameTools-Installer.aot")] // assembly name + '.aot suffix

var inputRootArg = new Argument<DirectoryInfo>(
    name: "INPUT_ROOT",
    description: "Location of files to include in the installer");
var signingKeyArg = new Option<string>(
    name: "--signing-key",
    description: "Code signing key ID");
var timestampServerArg = new Option<string>(
    name: "--timestamp-server",
    description: "Code signing timestamp server");
var stampFileArg = new Option<FileInfo>(
    name: "--stamp-file",
    description: "The full path to the produced executable will be written here on success");

var command = new RootCommand("Build the MSI");
command.Add(inputRootArg);
command.Add(signingKeyArg);
command.Add(timestampServerArg);
command.Add(stampFileArg);
command.SetHandler(
    CreateInstaller, inputRootArg, signingKeyArg, timestampServerArg, stampFileArg);
return await command.InvokeAsync(args);

async Task SetProjectVersionFromJson(ManagedProject project, DirectoryInfo inputRoot)
{
    var stream = System.IO.File.OpenRead(inputRoot.GetFiles("installer/version.json").First().FullName);
    var options = new JsonSerializerOptions
    {
        PropertyNameCaseInsensitive = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
    };

    var version = await JsonSerializer.DeserializeAsync<JsonVersionInfo>(stream, options);
    Debug.Assert(version != null, nameof(version) + " != null");
    var c = version.Components;
    project.Version = new Version($"{c.A}.{c.B}.{c.C}.{c.D}");
    project.OutFileName += $"-v{c.A}.{c.B}.{c.C}";
    if (!version.Tagged)
    {
        project.OutFileName += $"+{version.TweakLabel}.{c.D}";
    }
}

async Task<int> CreateInstaller(DirectoryInfo inputRoot, string? signingKeyId, string? timestampServer, FileInfo? stampFile)
{
    if (!System.IO.File.Exists($"{inputRoot}/bin/XRFrameTools.exe"))
    {
        Console.WriteLine($"Cannot find bin/XRFrameTools.exe in INPUT_ROOT ({inputRoot.FullName})");
        return 1;
    }

    var project = CreateProject(inputRoot);
    await SetProjectVersionFromJson(project, inputRoot);
    
    project.ResolveWildCards();
    CreateShortcuts(inputRoot, project);
    AddCommandLineAliases(project);

    RegisterAPILayers(inputRoot, project);

    SignProject(project, signingKeyId, timestampServer);
    BuildMsi(project, stampFile);

    return 0;
}

void SignProject(ManagedProject managedProject, string? s, string? timestampServer1)
{
    if (s != null)
    {
        managedProject.DigitalSignature = new DigitalSignature
        {
            CertificateId = s,
            CertificateStore = StoreType.sha1Hash,
            HashAlgorithm = HashAlgorithmType.sha256,
            Description = managedProject.OutFileName,
            TimeUrl = (timestampServer1 == null) ? null : new Uri(timestampServer1),
        };
        managedProject.SignAllFiles = true;
    }
    else
    {
        managedProject.OutFileName += "-UNSIGNED";
    }
}

void BuildMsi(ManagedProject managedProject, FileInfo? fileInfo)
{
    var outFile = managedProject.BuildMsi();
    if (fileInfo != null)
    {
        System.IO.File.WriteAllText(fileInfo.FullName, $"{outFile}\n");
        Console.WriteLine($"Wrote output path '{outFile}' to `{fileInfo.FullName}`");
    }
}


void InstallUpdater(SetupEventArgs e)
{
    var debugOut = new DefaultTraceListener();
    if (!e.IsElevated)
    {
        debugOut.WriteLine("Not installing updater, no longer elevated");
        return;
    }
    var path = e.InstallDir + "/bin/fredemmott_XRFrameTools_Updater.exe";
    if (!System.IO.File.Exists(path))
    {
        debugOut.WriteLine($"Not installing updater, `{path}` does not exist");
        return;
    }

    Process.Start(path, "--install --no-scheduled-task --no-autostart --silent");
}

ManagedProject CreateProject(DirectoryInfo inputRoot)
{
    var installerResources = new Feature("Installer Resources");
    installerResources.IsEnabled = false;

    var project =
        new ManagedProject("XRFrameTools",
            new Dir(@"%ProgramFiles%\XRFrameTools",
                new Dir("bin", new Files("bin/*.*")),
                new Dir("lib", new Files("lib/*.*")),
                new Dir("share", new Files("share/*.*")),
                new Files(installerResources, @"installer/*.*")));
    project.AfterInstall += InstallUpdater;

    project.GUID = Guid.Parse("e3334ff2-9b8f-4f3c-ba25-5f965f3b7dc9");
    project.Platform = Platform.x64;

    project.SourceBaseDir = inputRoot.FullName;

    project.ControlPanelInfo.Manufacturer = "Fred Emmott";
    project.ControlPanelInfo.InstallLocation = "[INSTALLDIR]";
    project.LicenceFile = "installer/LICENSE.rtf";
    return project;
}

void RegisterAPILayers(DirectoryInfo directoryInfo, ManagedProject managedProject)
{
    const string apiLayersKey = @"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit";
    foreach (var file in directoryInfo.GetDirectories("lib").First().GetFiles("XR_APILAYER_*64.json"))
    {
        managedProject.AddRegValue(new RegValue(RegistryHive.LocalMachine, apiLayersKey,
            $"[INSTALLDIR]lib\\{file.Name}", 0));
    }
    foreach (var file in directoryInfo.GetDirectories("lib").First().GetFiles("XR_APILAYER_*32.json"))
    {
        var value = new RegValue(RegistryHive.LocalMachine, apiLayersKey,
            $"[INSTALLDIR]lib\\{file.Name}", 0);
        value.Win64 = false;
        managedProject.AddRegValue(value);
    }
}

void AddCommandLineAliases(ManagedProject managedProject)
{
    const string appPathsRoot = @"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths";
    managedProject.AddRegValues(
        new RegValue(
            RegistryHive.LocalMachine,
            appPathsRoot + @"\XRFrameTools.exe",
            string.Empty,
            @"[INSTALLDIR]bin\XRFrameTools.exe"),
        new RegValue(
            RegistryHive.LocalMachine,
            appPathsRoot + @"\XRFrameTools-binlog-to-csv.exe",
            string.Empty,
            @"[INSTALLDIR]bin\binlog-to-csv.exe")
    );
}

void CreateShortcuts(DirectoryInfo directoryInfo, ManagedProject managedProject)
{
    var target = $"{directoryInfo}\\bin\\XRFrameTools.exe".PathGetFullPath();
    var file = managedProject.AllFiles.Single(f => f.Name == target);
    file.AddShortcuts(
        new FileShortcut("XRFrameTools", "INSTALLDIR"),
        new FileShortcut("XRFrameTools", "%Desktop%"),
        new FileShortcut("XRFrameTools", "%ProgramMenuFolder%"));
}

class JsonVersionComponents
{
    public int A { get; set; }
    public int B { get; set; }
    public int C { get; set; }
    public int D { get; set; }
}

class JsonVersionInfo
{
    public JsonVersionComponents Components { get; set; } = new JsonVersionComponents();
    public string TweakLabel { get; set; } = string.Empty;
    public bool Tagged { get; set; }
}