// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Reflection;

namespace System.Runtime.CompilerServices;

internal static class UnityInvocation
{
    internal static unsafe object? Invoke(nint method, object? target, nint arguments)
    {
        IRuntimeMethodInfo info = RuntimeMethodHandle.FromIntPtr(method).GetMethodInfo();
        RuntimeType owner = RuntimeMethodHandle.GetDeclaringType(info.Value);
        Signature signature = new Signature(info, owner);
        object? result = RuntimeMethodHandle.InvokeMethod(target, (void**)arguments, signature, isConstructor: false);
        GC.KeepAlive(info);
        return result;
    }
}
