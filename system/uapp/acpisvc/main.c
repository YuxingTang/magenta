// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <acpica/acpi.h>
#include <magenta/syscalls.h>
#include <runtime/thread.h>

#define ACPI_MAX_INIT_TABLES 32

static ACPI_STATUS acpi_set_apic_irq_mode(void);
static uint32_t power_button_event_handler(void* ctx);
static void notify_event_handler(ACPI_HANDLE Device, UINT32 Value, void* Context);
static int power_button_thread(void* arg);

int main(int argc, char **argv) {

    // This sequence is described in section 10.1.2.1 (ACPICA Initialization With
    // Early ACPI Table Access) of the ACPICA developer's reference.
    ACPI_STATUS status = AcpiInitializeSubsystem();
    if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI\n");
        return 1;
    }

    status = AcpiInitializeTables(NULL, ACPI_MAX_INIT_TABLES, FALSE);
    if (status == AE_NOT_FOUND) {
        printf("WARNING: could not find ACPI tables\n");
        return 1;
    } else if (status == AE_NO_MEMORY) {
        printf("WARNING: could not initialize ACPI tables\n");
        return 1;
    } else if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI tables for unknown reason\n");
        return 1;
    }

    status = AcpiLoadTables();
    if (status != AE_OK) {
        printf("WARNING: could not load ACPI tables: %d\n", status);
        return 1;
    }

    status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    if (status != AE_OK) {
        printf("WARNING: could not enable ACPI\n");
        return 1;
    }

    status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    if (status != AE_OK) {
        printf("WARNING: could not initialize ACPI objects\n");
        return 1;
    }

    status = acpi_set_apic_irq_mode();
    if (status == AE_NOT_FOUND) {
        printf("WARNING: Could not find ACPI IRQ mode switch\n");
    } else if (status != AE_OK) {
        printf("Failed to set APIC IRQ mode\n");
        return 1;
    }
    printf("Initialized ACPI\n");

    mx_handle_t power_button_event = mx_event_create(0);

    status = AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON,
                                          power_button_event_handler,
                                          (void*)(uintptr_t)power_button_event);
    if (status != AE_OK) {
        printf("Failed to install POWER_BUTTON handler\n");
    }

#if 0
    acpi_ec_init();
#endif

    /* HACKs to make the power button power off the machine */
    AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT, ACPI_SYSTEM_NOTIFY | ACPI_DEVICE_NOTIFY, notify_event_handler, (void*)(uintptr_t)power_button_event);

    mxr_thread_t* thread;
    mx_status_t mx_status = mxr_thread_create(power_button_thread, (void*)(uintptr_t)power_button_event, "acpi-powerbtn", &thread);
    if (mx_status != NO_ERROR) {
        printf("Failed to create power button thread\n");
    }
    mxr_thread_detach(thread);

    while (1) {
        mx_nanosleep(1ULL<<40);
    }
}

/**
 * @brief  Handle the Power Button Fixed Event
 *
 * We simply write to a well known port. A user-mode driver should pick
 * this event and take action.
 */
static uint32_t power_button_event_handler(void* ctx)
{
    mx_handle_t event = (mx_handle_t)(uintptr_t)ctx;
    mx_event_signal(event);
    // Note that the spec indicates to return 0. The code in the
    // Intel implementation (AcpiEvFixedEventDetect) reads differently.
    return ACPI_INTERRUPT_HANDLED;
}

static void notify_event_handler(ACPI_HANDLE Device, UINT32 Value, void* Context)
{
    ACPI_DEVICE_INFO *info = NULL;
    ACPI_STATUS status = AcpiGetObjectInfo(Device, &info);
    if (status != AE_OK) {
        if (info) {
            ACPI_FREE(info);
        }
        return;
    }

    mx_handle_t event = (mx_handle_t)(uintptr_t)Context;

    /* Handle powerbutton events via the notify interface */
    bool power_btn = false;
    if (info->Valid & ACPI_VALID_HID) {
        if (Value == 128 &&
            !strncmp(info->HardwareId.String, "PNP0C0C", info->HardwareId.Length)) {

            power_btn = true;
        } else if (Value == 199 &&
                   (!strncmp(info->HardwareId.String, "MSHW0028", info->HardwareId.Length) ||
                    !strncmp(info->HardwareId.String, "MSHW0040", info->HardwareId.Length))) {
            power_btn = true;
        }
    }

    if (power_btn) {
        mx_event_signal(event);
    }

    ACPI_FREE(info);
}

static void acpi_poweroff(void)
{
    ACPI_STATUS status = AcpiEnterSleepStatePrep(5);
    if (status == AE_OK) {
        AcpiEnterSleepState(5);
    }
}

static int power_button_thread(void* arg) {
    mx_handle_t event = (mx_handle_t)(uintptr_t)arg;

    for(;;) {
        mx_signals_state_t state;
        mx_status_t status = mx_handle_wait_one(event,
                                                MX_SIGNAL_SIGNALED,
                                                MX_TIME_INFINITE,
                                                &state);
        if (status != NO_ERROR) {
            continue;
        }
        if (state.satisfied != MX_SIGNAL_SIGNALED) {
            continue;
        }
        acpi_poweroff();
    }

    printf("acpi power button thread terminated\n");
    return 0;
}

/* @brief Switch interrupts to APIC model (controls IRQ routing) */
static ACPI_STATUS acpi_set_apic_irq_mode(void)
{
    ACPI_OBJECT selector = {
        .Integer.Type = ACPI_TYPE_INTEGER,
        .Integer.Value = 1, // 1 means APIC mode according to ACPI v5 5.8.1
    };
    ACPI_OBJECT_LIST params = {
        .Count =  1,
        .Pointer = &selector,
    };
    return AcpiEvaluateObject(NULL, (char *)"\\_PIC", &params, NULL);
}
