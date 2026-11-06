# Publish with Satori GC

Use Satori GC in self-contained .NET 11 applications with CoreCLR or NativeAOT.
Satori GC is experimental.

## Add to your project

Add the `Sdk` element to your existing project, keeping its current SDK and
other settings:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <Sdk Name="PublishWithSatoriGC" Version="__PACKAGE_VERSION__" />
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net11.0</TargetFramework>
  </PropertyGroup>
</Project>
```

Choose the package version that matches your application's .NET major version.
The SDK restores the supporting packages automatically. Central Package
Management works without additional entries in `Directory.Packages.props`.

## Publish

Examples:

```sh
# CoreCLR self-contained publish
dotnet publish -c Release -r linux-x64 --self-contained true

# CoreCLR single-file publish
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true

# CoreCLR ReadyToRun publish
dotnet publish -c Release -r linux-x64 --self-contained true -p:PublishReadyToRun=true

# NativeAOT
dotnet publish -c Release -r linux-x64 -p:PublishAot=true

# macOS NativeAOT
dotnet publish -c Release -r osx-arm64 -p:PublishAot=true

# Android: add the SDK to the existing Android/MAUI project
dotnet publish -c Release -r android-arm64 -f net11.0-android -p:PublishAot=true -p:AndroidPackageFormats=apk

# Apple mobile: add the SDK to the existing iOS/tvOS/Mac Catalyst project
dotnet publish -c Release -r ios-arm64 -f net11.0-ios -p:PublishAot=true
```

Run the application from its publish output.

`UseSatoriGC` defaults to `true`. Add `-p:UseSatoriGC=false` to publish with the stock runtime instead. Mono is not supported by Satori GC.

### Framework-dependent applications

Set `AppHostRelativeDotNet` to the path of a compatible Satori .NET installation relative to the application executable. The .NET apphost selects the runtime from that installation; the SDK does not bundle a runtime or install one at that path. Keep the standard .NET installation layout, including `host/fxr` and `shared/Microsoft.NETCore.App`, and use a Satori runtime compatible with the project's .NET version and target architecture.

```sh
dotnet publish -c Release -r win-x64 --self-contained false -p:AppHostRelativeDotNet=../satori-dotnet
```

Launch the published executable so its apphost search settings take effect. Running `dotnet app.dll` uses the installation of the invoked `dotnet` executable instead. ReadyToRun publishing still uses the Satori host tools. Without `AppHostRelativeDotNet`, CoreCLR publishing with this SDK requires a self-contained application.

## Supported targets

32-bit targets use the standard .NET runtime and tools and emit warning `SATORI001` when Satori is enabled. A selected `RuntimeIdentifier` takes precedence over the project's `RuntimeIdentifiers` list.

| Platform | Runtime identifiers |
| --- | --- |
| Windows | `win-x64`, `win-arm64` |
| Linux (glibc) | `linux-x64`, `linux-arm64` |
| Linux (musl) | `linux-musl-x64`, `linux-musl-arm64` |
| macOS | `osx-x64`, `osx-arm64` |
| Android | `android-x64`, `android-arm64` |
| iOS | `ios-arm64`, `iossimulator-x64`, `iossimulator-arm64` |
| tvOS | `tvos-arm64`, `tvossimulator-x64`, `tvossimulator-arm64` |
| Mac Catalyst | `maccatalyst-x64`, `maccatalyst-arm64` |
