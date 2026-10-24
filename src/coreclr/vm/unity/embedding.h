// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#pragma once

#include "fcall.h"

class UnityEmbedding
{
public:
    static FCDECL3(INT32, InvokeNativeRemapper, BYTE* path, BYTE* buffer, INT32 size);
    static void NotifyFatalError(PEXCEPTION_POINTERS exception);
};
