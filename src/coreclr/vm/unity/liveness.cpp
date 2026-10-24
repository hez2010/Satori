// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

#include "common.h"
#include "threadsuspend.h"

namespace
{
using RegisterObjects = void (__cdecl *)(Object**, int, void*);
using Reallocate = void* (__cdecl *)(void*, size_t, void*);

enum class WalkMode { Inactive, PreviouslyPreemptive, PreviouslyCooperative };
thread_local WalkMode t_walkMode;

struct Liveness
{
    RegisterObjects report;
    Reallocate reallocate;
    void* context;
    Object** objects;
    Object** slots;
    size_t count;
    size_t capacity;

    Object** Allocate(Object** old, size_t length)
    {
        Object** result = static_cast<Object**>(reallocate(old, length * sizeof(Object*), context));
        if (result == nullptr)
            EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);
        return result;
    }

    Object** FindSlot(Object* object) const
    {
        size_t mask = 2 * capacity - 1;
        size_t hash = (reinterpret_cast<uintptr_t>(object) >> 3) * UINT64_C(11400714819323198485);
        size_t index = (hash ^ (hash >> 32)) & mask;
        while (slots[index] != nullptr && slots[index] != object)
            index = (index + 1) & mask;
        return slots + index;
    }

    void Grow()
    {
        if (capacity > SIZE_MAX / (4 * sizeof(Object*)))
            EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);
        capacity = capacity == 0 ? 256 : capacity * 2;
        objects = Allocate(objects, capacity);
        slots = Allocate(slots, 2 * capacity);
        memset(slots, 0, 2 * capacity * sizeof(Object*));
        for (size_t i = 0; i < count; ++i)
            *FindSlot(objects[i]) = objects[i];
    }

    void Add(Object* object)
    {
        if (object == nullptr)
            return;
        if (count == capacity)
            Grow();
        Object** slot = FindSlot(object);
        if (*slot == nullptr)
        {
            *slot = object;
            objects[count++] = object;
        }
    }

    void Report(size_t first)
    {
        // A callback may walk another root. Its discoveries belong to that call.
        const size_t end = count;
        Object* batch[64];
        int used = 0;
        for (size_t i = first; i < end; ++i)
        {
            Object* object = objects[i];
            if (object->GetGCSafeMethodTable()->IsUnityAssetGCFilterType())
            {
                batch[used++] = object;
                if (used == ARRAY_SIZE(batch))
                {
                    report(batch, used, context);
                    used = 0;
                }
            }
        }
        if (used != 0)
            report(batch, used, context);
    }
};

struct Walk : ScanContext
{
    Liveness* state;
    Object* root;

    static bool Visit(Object* object, void* data)
    {
        Walk* walk = static_cast<Walk*>(data);
        // An explicit root seeds a20 traversal. Only its descendants are reported.
        if (object != walk->root)
            walk->state->Add(object);
        return true;
    }

    static void VisitStatic(Object** slot, ScanContext* data, uint32_t)
    {
        static_cast<Walk*>(data)->state->Add(*slot);
    }

    void Run()
    {
        // The Editor dispatches traversal to native asset-GC workers while the
        // coordinating thread holds the runtime suspension.
        if (!ThreadSuspend::SysIsSuspended())
            EEPOLICY_HANDLE_FATAL_ERROR(COR_E_INVALIDOPERATION);
        size_t first = state->count;
        IGCHeap* heap = GCHeapUtilities::GetGCHeap();
        if (root != nullptr)
            heap->DiagWalkObject(root, Visit, this);
        else
            GetAppDomain()->EnumStaticGCRefs(VisitStatic, this);
        for (size_t i = first; i < state->count; ++i)
            heap->DiagWalkObject(state->objects[i], Visit, this);
        state->Report(first);
    }
};
}

extern "C" DLLEXPORT int __cdecl coreclr_unity_liveness_stop_world()
{
    if (t_walkMode != WalkMode::Inactive)
        EEPOLICY_HANDLE_FATAL_ERROR(COR_E_INVALIDOPERATION);
    Thread* thread = SetupThread();
    bool preemptive = !thread->PreemptiveGCDisabled();
    if (preemptive)
        thread->DisablePreemptiveGC();
    // Suspension excludes relocation. Deferred sweeping and trimming reclaim only
    // unreachable storage, so a walk following exact references can coexist with them.
    ThreadSuspend::SuspendEE(ThreadSuspend::SUSPEND_FOR_GC_PREP);
    t_walkMode = preemptive ? WalkMode::PreviouslyPreemptive : WalkMode::PreviouslyCooperative;
    return 0;
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_liveness_start_world()
{
    WalkMode mode = t_walkMode;
    if (mode == WalkMode::Inactive)
        EEPOLICY_HANDLE_FATAL_ERROR(COR_E_INVALIDOPERATION);
    t_walkMode = WalkMode::Inactive;
    ThreadSuspend::RestartEE(FALSE, TRUE);
    if (mode == WalkMode::PreviouslyPreemptive)
        GetThread()->EnablePreemptiveGC();
}

extern "C" DLLEXPORT Liveness* __cdecl coreclr_unity_liveness_allocate_struct(
    void*, int, RegisterObjects report, void* context, Reallocate reallocate)
{
    if (report == nullptr || reallocate == nullptr)
        return nullptr;
    void* storage = reallocate(nullptr, sizeof(Liveness), context);
    if (storage == nullptr)
        EEPOLICY_HANDLE_FATAL_ERROR(COR_E_OUTOFMEMORY);
    return new (storage) Liveness{report, reallocate, context, nullptr, nullptr, 0, 0};
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_liveness_calculation_from_root(Object* root, Liveness* state)
{
    if (root == nullptr || state == nullptr)
        return;
    Walk walk;
    walk.root = root;
    walk.state = state;
    walk.Run();
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_liveness_calculation_from_statics(Liveness* state)
{
    if (state == nullptr)
        return;
    Walk walk;
    walk.root = nullptr;
    walk.state = state;
    walk.promotion = true;
    walk.Run();
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_liveness_finalize(Liveness* state)
{
    if (state != nullptr && state->count != 0)
    {
        memset(state->slots, 0, 2 * state->capacity * sizeof(Object*));
        state->count = 0;
    }
}

extern "C" DLLEXPORT void __cdecl coreclr_unity_liveness_free_struct(Liveness* state)
{
    if (state == nullptr)
        return;
    if (state->capacity != 0)
    {
        state->reallocate(state->slots, 0, state->context);
        state->reallocate(state->objects, 0, state->context);
    }
    state->reallocate(state, 0, state->context);
}
