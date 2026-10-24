// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include <platformdefines.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <thread>

namespace
{
using Report = void (*)(void**, int, void*);
using Resize = void* (*)(void*, size_t, void*);
struct Api
{
    int (*stop)();
    void (*start)();
    void* (*allocate)(void*, int, Report, void*, Resize);
    void (*fromRoot)(void*, void*);
    void (*fromStatics)(void*);
    void (*finish)(void*);
    void (*release)(void*);
    void* (*target)(uintptr_t);
};

constexpr int AssetCount = 96;
struct Observation
{
    void* assets[AssetCount];
    int seen[AssetCount];
    void* root;
    int rootCount;
    bool invalidBatch;
};

void Record(void** objects, int count, void* context)
{
    Observation& result = *static_cast<Observation*>(context);
    result.invalidBatch |= count <= 0 || count > 64;
    for (int i = 0; i < count; ++i)
    {
        if (objects[i] == result.root)
            ++result.rootCount;
        for (int j = 0; j < AssetCount; ++j)
            if (objects[i] == result.assets[j])
                ++result.seen[j];
    }
}

void* Reallocate(void* old, size_t size, void*)
{
    if (size != 0)
        return realloc(old, size);
    free(old);
    return nullptr;
}
}

extern "C" DLL_EXPORT int CheckUnityWalk(Api* api, uintptr_t root, const uintptr_t* handles, int count, int statics)
{
    if (count != AssetCount)
        return 1;
    Observation result{};
    void* state = api->allocate(nullptr, count, Record, &result, Reallocate);
    if (state == nullptr)
        return 2;
    api->stop();
    result.root = api->target(root);
    for (int i = 0; i < count; ++i)
        result.assets[i] = api->target(handles[i]);
    int error = 0;
    for (int pass = 0; pass < 2; ++pass)
    {
        memset(result.seen, 0, sizeof(result.seen));
        result.rootCount = 0;
        auto walk = [&]()
        {
            if (statics)
                api->fromStatics(state);
            else
                api->fromRoot(result.root, state);
        };
        std::thread worker(walk);
        worker.join();
        walk();
        if (result.invalidBatch || result.rootCount != (statics ? 1 : 0))
            error = 3;
        for (int i = 0; i < count; ++i)
            if (result.seen[i] != 1)
                error = 10 + i;
        api->finish(state);
    }
    api->start();
    api->release(state);
    return error;
}
