#pragma once

#include <stdbool.h>

#ifdef LK
#include <kernel/semaphore.h>
#include <kernel/spinlock.h>
#else
#include <runtime/mutex.h>
#include <semaphore.h>
#endif

/*
 * Settings described in section 7 of
 * https://acpica.org/sites/acpica/files/acpica-reference_17.pdf
 */


#if __x86_64__
#define ACPI_MACHINE_WIDTH 64
#elif __x86__
#define ACPI_MACHINE_WIDTH 32
#define ACPI_USE_NATIVE_DIVIDE
#else
#error Unexpected architecture
#endif

#define ACPI_FLUSH_CPU_CACHE() __asm__ volatile ("wbinvd")
#if 0
#define ACPI_ACQUIRE_GLOBAL_LOCK(FacsPtr, Acquired)
#define ACPI_RELEASE_GLOBAL_LOCK(FacsPtr, Pending)
#endif

// Use the standard library headers
#define ACPI_USE_STANDARD_HEADERS
#define ACPI_USE_SYSTEM_CLIBRARY

// Use the builtin cache implementation
#define ACPI_USE_LOCAL_CACHE

// Specify the types Magenta uses for various common objects
#ifdef LK
#define ACPI_CPU_FLAGS spin_lock_saved_state_t
#define ACPI_SPINLOCK spin_lock_t*
#define ACPI_SEMAPHORE semaphore_t*
#else
#define ACPI_CPU_FLAGS int
#define ACPI_SPINLOCK mxr_mutex_t*
#define ACPI_SEMAPHORE sem_t*
#endif

// Borrowed from aclinuxex.h

// Include the gcc header since we're compiling on gcc
#include "acgcc.h"

extern bool _acpica_acquire_global_lock(void *FacsPtr);
extern bool _acpica_release_global_lock(void *FacsPtr);
#define ACPI_ACQUIRE_GLOBAL_LOCK(FacsPtr, Acq) Acq = _acpica_acquire_global_lock(FacsPtr)
#define ACPI_RELEASE_GLOBAL_LOCK(FacsPtr, Pnd) Pnd = _acpica_release_global_lock(FacsPtr)
