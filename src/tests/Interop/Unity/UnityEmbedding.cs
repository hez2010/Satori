// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading;
using Xunit;

namespace UnityEngine
{
    [AttributeUsage(AttributeTargets.Class)]
    public sealed class AssetGCFilterTypeAttribute : Attribute { }
}

public static unsafe class UnityEmbedding
{
    [UnityEngine.AssetGCFilterType]
    private class Asset { public object Edge; }
    private sealed class Derived : Asset { }
    private sealed class Wrapper<T> { }
    private struct AllocatorInfo
    {
        public uint Size, Flags;
        public ulong Id;
        public nint Context, Loader;
    }

    private static Asset s_root;
    private static int s_value;
    public static bool HasUnityEmbedding => typeof(object).Assembly.GetType("Unity.PathRemapper") != null;

    [DllImport("UnityLivenessNative", CallingConvention = CallingConvention.Cdecl)]
    private static extern int CheckUnityWalk(nint[] api, nint root, nint[] objects, int count, int statics);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern void coreclr_unity_gc_get_committed_bytes(ulong* total, ulong* bookkeeping);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern int coreclr_unity_gc_concurrent_mode(int enable);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern int coreclr_unity_class_is_blittable(nint type);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern int mono_class_get_userdata_offset();
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern void mono_class_set_userdata(nint type, nint value);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern nint coreclr_unity_profiler_class_get_loader_allocator(nint type);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern int coreclr_unity_profiler_loader_allocator_get_info(nint allocator, AllocatorInfo* info);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern nint mono_runtime_invoke(nint method, nint target, nint* args, out nint exception);
    [DllImport("coreclr", CallingConvention = CallingConvention.Cdecl)]
    private static extern nint mono_method_get_last_managed();

    private static void Check(bool value, string message)
    {
        if (!value)
            throw new Exception(message);
    }

    private static void Add(int value, ref int other)
    {
        Check(mono_method_get_last_managed() != 0, "invocation depth");
        other += value;
        s_value = other;
    }

    private static void Fail() => throw new InvalidOperationException();

    private static string RemapPath(ReadOnlySpan<char> path) => path.SequenceEqual("unity-input") ? "unity-output" : null;

    private static void SetAliases(ref object first, ref object second)
    {
        first = new object();
        GC.Collect(2, GCCollectionMode.Forced, true, true);
        Check(ReferenceEquals(first, second), "aliased reference arguments");
        s_value = 73;
    }

    [ConditionalFact(nameof(HasUnityEmbedding))]
    public static void TestEntryPoint()
    {
        byte[] live = new byte[4 * 1024 * 1024];
        ulong total = 0, metadata = ulong.MaxValue;
        coreclr_unity_gc_get_committed_bytes(&total, &metadata);
        Check(total >= (ulong)live.Length && metadata == 0, "committed total and unavailable bookkeeping");
        metadata = ulong.MaxValue;
        coreclr_unity_gc_get_committed_bytes(null, &metadata);
        Check(metadata == 0, "bookkeeping-only query");
        total = 0;
        coreclr_unity_gc_get_committed_bytes(&total, null);
        Check(total >= (ulong)live.Length, "total-only query");
        GC.KeepAlive(live);
        Check(coreclr_unity_class_is_blittable(typeof(int).TypeHandle.Value) == 1, "blittable primitive");
        Check(coreclr_unity_class_is_blittable(typeof(Asset).TypeHandle.Value) == 0, "reference type");
        Check(coreclr_unity_class_is_blittable(typeof(int*).TypeHandle.Value) == 1, "pointer");
        nint type = typeof(Wrapper<string>).TypeHandle.Value;
        nint otherType = typeof(Wrapper<object>).TypeHandle.Value;
        int offset = mono_class_get_userdata_offset();
        nint original = Marshal.ReadIntPtr(type, offset), other = Marshal.ReadIntPtr(otherType, offset);
        try
        {
            mono_class_set_userdata(type, 42);
            Check(Marshal.ReadIntPtr(type, offset) == 42 && Marshal.ReadIntPtr(otherType, offset) == other, "per-type user data");
        }
        finally { mono_class_set_userdata(type, original); }

        const BindingFlags PrivateStatic = BindingFlags.Static | BindingFlags.NonPublic;
        Type remapper = typeof(object).Assembly.GetType("Unity.PathRemapper", throwOnError: true);
        FieldInfo callback = remapper.GetField("s_managedRemapper", PrivateStatic);
        object originalCallback = callback.GetValue(null);
        try
        {
            callback.SetValue(null, typeof(UnityEmbedding).GetMethod(nameof(RemapPath), PrivateStatic).CreateDelegate(callback.FieldType));
            Check((string)remapper.GetMethod("Remap").Invoke(null, new object[] { "unity-input" }) == "unity-output", "path remapping");
        }
        finally { callback.SetValue(null, originalCallback); }

        nint globalAllocator = coreclr_unity_profiler_class_get_loader_allocator(typeof(int).TypeHandle.Value);
        AllocatorInfo global = new AllocatorInfo { Size = (uint)sizeof(AllocatorInfo) };
        Check(coreclr_unity_profiler_loader_allocator_get_info(globalAllocator, &global) == 1 &&
              global.Flags == 8 && global.Loader == 0, "global loader allocator");

        var context = new AssemblyLoadContext("Unity test", isCollectible: true);
        using (context.EnterContextualReflection())
        {
            var assembly = Assembly.Load(System.IO.File.ReadAllBytes(typeof(UnityEmbedding).Assembly.Location));
            Check(AssemblyLoadContext.GetLoadContext(assembly) == context, "contextual assembly load");
            nint allocator = coreclr_unity_profiler_class_get_loader_allocator(assembly.GetType(typeof(Asset).FullName).TypeHandle.Value);
            AllocatorInfo info = default;
            Check(coreclr_unity_profiler_loader_allocator_get_info(allocator, &info) == 0, "allocator info size");
            info.Size = (uint)sizeof(AllocatorInfo);
            Check(coreclr_unity_profiler_loader_allocator_get_info(allocator, &info) == 1 &&
                  info.Flags == 3 && info.Context != 0 && info.Loader != 0, "collectible loader handles");
        }
        context.Unload();

        int a = 17, b = 25;
        nint* args = stackalloc nint[] { (nint)(&a), (nint)(&b) };
        mono_runtime_invoke(typeof(UnityEmbedding).GetMethod(nameof(Add), PrivateStatic).MethodHandle.Value, 0, args, out nint exception);
        Check(exception == 0 && b == 42 && s_value == 42 && mono_method_get_last_managed() == 0, "method invocation");
        nint reference = 0;
        args[0] = args[1] = (nint)(&reference);
        mono_runtime_invoke(typeof(UnityEmbedding).GetMethod(nameof(SetAliases), PrivateStatic).MethodHandle.Value, 0, args, out exception);
        Check(exception == 0 && reference != 0 && s_value == 73, "reference copy-back");
        mono_runtime_invoke(typeof(UnityEmbedding).GetMethod(nameof(Fail), PrivateStatic).MethodHandle.Value, 0, null, out exception);
        Check(exception != 0 && mono_method_get_last_managed() == 0, "managed exception");

        string[] names = { "coreclr_unity_liveness_stop_world", "coreclr_unity_liveness_start_world",
            "coreclr_unity_liveness_allocate_struct", "coreclr_unity_liveness_calculation_from_root",
            "coreclr_unity_liveness_calculation_from_statics", "coreclr_unity_liveness_finalize",
            "coreclr_unity_liveness_free_struct", "mono_gchandle_get_target_v2" };
        nint library = NativeLibrary.Load("coreclr", typeof(UnityEmbedding).Assembly, null);
        nint[] api = Array.ConvertAll(names, name => NativeLibrary.GetExport(library, name));
        var assets = new Asset[96];
        s_root = new Asset { Edge = assets };
        nint root = GCHandle.ToIntPtr(GCHandle.Alloc(s_root));
        var handles = new nint[assets.Length];
        for (int i = 0; i < assets.Length; ++i)
        {
            assets[i] = new Derived { Edge = s_root };
            handles[i] = GCHandle.ToIntPtr(GCHandle.Alloc(assets[i]));
        }

        bool done = false;
        var allocatorThread = new Thread(() => {
            while (!Volatile.Read(ref done))
            {
                var data = new byte[8192];
                GC.KeepAlive(data);
            }
        });
        allocatorThread.Start();
        try
        {
            for (int i = 0; i < 30; ++i)
            {
                GC.Collect(2, GCCollectionMode.Forced, blocking: false);
                int error = CheckUnityWalk(api, root, handles, handles.Length, i % 2);
                Check(error == 0, $"liveness ({i}, {error})");
            }
            int previous = coreclr_unity_gc_concurrent_mode(0);
            try
            {
                Check(coreclr_unity_gc_concurrent_mode(0) == 0, "repeated disable request");
                Check(coreclr_unity_gc_concurrent_mode(1) == 0, "previous mode was disabled");
                Check(coreclr_unity_gc_concurrent_mode(0) == 1, "previous mode was enabled");
            }
            finally { coreclr_unity_gc_concurrent_mode(previous); }
        }
        finally
        {
            Volatile.Write(ref done, true);
            allocatorThread.Join();
            foreach (nint handle in handles)
                GCHandle.FromIntPtr(handle).Free();
            GCHandle.FromIntPtr(root).Free();
            s_root = null;
            NativeLibrary.Free(library);
        }
    }
}
