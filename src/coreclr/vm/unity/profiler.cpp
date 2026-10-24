// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"
#include "profilepriv.h"

namespace
{
struct UnityLoaderAllocatorInfo
{
    uint32_t size;
    uint32_t flags;
    uint64_t id;
    ObjectHandleID assemblyLoadContextHandle;
    ObjectHandleID loaderAllocatorHandle;
};

enum AllocatorFlags : uint32_t { Collectible = 1, ManagedObjectLive = 2, Unloaded = 4, GlobalAllocator = 8 };

ObjectHandleID ContextHandle(AssemblyBinder* binder)
{
    return binder == nullptr || binder->IsDefault() ? nullptr : reinterpret_cast<ObjectHandleID>(binder->GetAssemblyLoadContext());
}

ObjectHandleID AssemblyContext(Assembly* assembly)
{
    return assembly == nullptr ? 0 : ContextHandle(assembly->GetPEAssembly()->GetAssemblyBinder());
}

bool IsAssemblyAllocator(LoaderAllocator* allocator)
{
    return allocator->Id()->GetType() == LAT_Assembly;
}

ObjectHandleID AllocatorContext(LoaderAllocator* allocator)
{
    if (allocator == nullptr || allocator->IsUnloaded() || !IsAssemblyAllocator(allocator))
        return 0;
    return ContextHandle(static_cast<AssemblyLoaderAllocator*>(allocator)->GetBinder());
}

LONG s_requestedConcurrentMode = -1;
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_profiler_register(const CLSID* id, const WCHAR* path)
{
    NewHolder<StoredProfilerNode> entry(new StoredProfilerNode());
    entry->guid = *id;
    entry->path.Set(path);
    g_profControlBlock.storedProfilers.InsertHead(entry);
    entry.SuppressRelease();
}

extern "C" DLLEXPORT ObjectHandleID __cdecl coreclr_unity_profiler_get_managed_assembly_load_context(AssemblyID id)
{
    return AssemblyContext(reinterpret_cast<Assembly*>(id));
}

extern "C" DLLEXPORT ObjectHandleID __cdecl coreclr_unity_profiler_class_get_assembly_load_context_handle(ClassID id)
{
    if (id == 0 || id == PROFILER_GLOBAL_CLASS)
        return 0;
    Module* module = TypeHandle::FromPtr(reinterpret_cast<void*>(id)).GetModule();
    return module == nullptr ? 0 : AssemblyContext(module->GetAssembly());
}

extern "C" DLLEXPORT LoaderAllocator* __cdecl coreclr_unity_profiler_assembly_get_loader_allocator(AssemblyID id)
{
    Assembly* assembly = reinterpret_cast<Assembly*>(id);
    return assembly == nullptr ? nullptr : assembly->GetLoaderAllocator();
}

extern "C" DLLEXPORT LoaderAllocator* __cdecl coreclr_unity_profiler_class_get_loader_allocator(ClassID id)
{
    if (id == 0 || id == PROFILER_GLOBAL_CLASS)
        return nullptr;
    TypeHandle type = TypeHandle::FromPtr(reinterpret_cast<void*>(id));
    return type.IsFullyLoaded() ? type.GetLoaderAllocator() : nullptr;
}

extern "C" DLLEXPORT ObjectHandleID __cdecl coreclr_unity_profiler_assembly_load_context_get_loader_allocator_handle(ObjectID id)
{
    if (id == 0)
        return 0;
    auto* context = reinterpret_cast<AssemblyLoadContextBaseObject*>(id);
    auto* binder = reinterpret_cast<AssemblyBinder*>(context->GetNativeAssemblyBinder());
    LoaderAllocator* allocator = binder == nullptr ? nullptr : binder->GetLoaderAllocator();
    return allocator == nullptr ? 0 : reinterpret_cast<ObjectHandleID>(allocator->GetLoaderAllocatorObjectHandle());
}

extern "C" DLLEXPORT ObjectHandleID __cdecl coreclr_unity_profiler_loader_allocator_get_assembly_load_context_handle(LoaderAllocator* allocator)
{
    return AllocatorContext(allocator);
}

extern "C" DLLEXPORT int __cdecl coreclr_unity_profiler_loader_allocator_get_info(LoaderAllocator* allocator, UnityLoaderAllocatorInfo* info)
{
    if (allocator == nullptr || info == nullptr || info->size < sizeof(*info))
        return 0;
    bool unloaded = allocator->IsUnloaded();
    bool live = !unloaded && allocator->IsExposedObjectLive();
    info->flags = (allocator->IsCollectible() ? Collectible : 0) | (live ? ManagedObjectLive : 0) |
                  (unloaded ? Unloaded : 0) | (allocator->Id()->GetType() == LAT_Global ? GlobalAllocator : 0);
    info->id = allocator->GetCreationNumber();
    info->assemblyLoadContextHandle = live ? AllocatorContext(allocator) : 0;
    info->loaderAllocatorHandle = live ? reinterpret_cast<ObjectHandleID>(allocator->GetLoaderAllocatorObjectHandle()) : 0;
    return 1;
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_gc_get_committed_bytes(uint64_t* total, uint64_t* bookkeeping)
{
    if (total != nullptr)
    {
        uint64_t fields[9], generations[5 * 4], pauses[2];
        uint32_t generation, pausePercentage;
        bool compacting, concurrent;
        GCHeapUtilities::GetGCHeap()->GetMemoryInfo(&fields[0], &fields[1], &fields[2], &fields[3], &fields[4], total,
            &fields[5], &fields[6], &fields[7], &fields[8], &generation, &pausePercentage,
            &compacting, &concurrent, generations, pauses, gc_kind_any);
    }
    if (bookkeeping != nullptr)
        *bookkeeping = 0; // The GC memory interface exposes only the total.
}

extern "C" DLLEXPORT int __cdecl coreclr_unity_gc_concurrent_mode(int enabled)
{
    // Unity saves and restores its requested mode. Satori schedules collections independently.
    LONG previous = InterlockedExchange(&s_requestedConcurrentMode, enabled != 0);
    return previous == -1 ? g_pConfig->GetGCconcurrent() != 0 : previous;
}
