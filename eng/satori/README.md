# Publish with Satori GC

Add the entrypoint SDK after your existing SDK, replacing the example version
with the version from the feed:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <Sdk Name="PublishWithSatoriGC" Version="10.0.13-satori.123456789.1" />
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
  </PropertyGroup>
</Project>
```

Usage:

```sh
# CoreCLR self-contained publish
dotnet publish -c Release -r linux-x64 --self-contained true

# CoreCLR single-file publish
dotnet publish -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true

# NativeAOT
dotnet publish -c Release -r linux-x64 -p:PublishAot=true

# macOS NativeAOT
dotnet publish -c Release -r osx-arm64 -p:PublishAot=true
```

`UseSatoriGC` defaults to `true`. Set it to
`false` to use the stock runtime and compiler.
