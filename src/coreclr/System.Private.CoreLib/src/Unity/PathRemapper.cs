// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text;

namespace Unity;

internal static class PathRemapper
{
    public delegate string? RemapOp(ReadOnlySpan<char> path);

    // Unity installs the managed callback through reflection.
    private static RemapOp? s_managedRemapper;
    private static readonly RemapOp s_nativeRemapper = RemapViaNativeCallback;
    internal static RemapOp RemapOperation => s_managedRemapper ?? s_nativeRemapper;

    [return: NotNullIfNotNull(nameof(path))]
    public static string? Remap(string? path) =>
        string.IsNullOrEmpty(path) ? path : RemapOperation(path) ?? path;

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern unsafe int InvokeNativeRemapper(byte* pathUtf8, byte* buffer, int bufferLen);

    private static unsafe string? RemapViaNativeCallback(ReadOnlySpan<char> path)
    {
        const int MaxBytes = 32768; // Includes the UTF-8 terminator.
        if (path.IsEmpty)
            return null;
        int count = Encoding.UTF8.GetByteCount(path);
        if (count >= MaxBytes)
            throw new PathTooLongException();
        Span<byte> input = count < 256 ? stackalloc byte[count + 1] : new byte[count + 1];
        Encoding.UTF8.GetBytes(path, input);
        input[count] = 0;
        fixed (byte* source = input)
        {
            int size = InvokeNativeRemapper(source, null, 0);
            if (size == 0)
                return null;
            if ((uint)size > MaxBytes)
                throw new PathTooLongException();
            Span<byte> output = size <= 256 ? stackalloc byte[size] : new byte[size];
            output.Clear();
            fixed (byte* destination = output)
                InvokeNativeRemapper(source, destination, size);
            return Encoding.UTF8.GetString(output[..(size - 1)]);
        }
    }
}
