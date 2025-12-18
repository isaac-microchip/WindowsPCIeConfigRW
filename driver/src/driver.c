/*
* Microchip Switchtec(tm) PCIe Extended Config Space Windows Driver
* Copyright (c) 2025, Microsemi Technology Incoroporated
*
* This program is free software; you can redistribute it and/or modify it
* under the terms and conditions of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
*/

#include <ntddk.h>
#include <wdf.h>
#include <wdmsec.h>
#include <wdm.h>
#include <initguid.h>
#include <wdmguid.h>
#include <stdio.h>

#define PCI_EXTENDED_CONFIG_SIZE 4096
#define TAG 'dBDF'
#define IOCTL_GET_CFG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _DEVICE_CONTEXT {
    UCHAR pci_config[PCI_EXTENDED_CONFIG_SIZE];
    ULONG bytes;
    int busID;
    int deviceID;
    int functionID;
} DEVICE_CONTEXT, * PDEVICE_CONTEXT;
// Declare the struct as a WDF device context type 
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

typedef struct _PCI_INFO {
    UCHAR pci_config[PCI_EXTENDED_CONFIG_SIZE];
    ULONG bytes;
    int busID;
    int deviceID;
    int functionID;
} PCI_INFO, * PPCI_INFO;

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD kmdfPcieDriverEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtIoDeviceControl;

// Initialize driver
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, kmdfPcieDriverEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "DriverEntry: Could not successfully create driver kmdfPcieDriver\n");
    }

    return status;
}

NTSTATUS kmdfPcieDriverEvtDeviceAdd(
    _In_    WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER(Driver);

    WDFDEVICE device;
    NTSTATUS status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES attributes;
    PDEVICE_CONTEXT deviceContext;

    // Initialize object attribute structure and allocate memory for DEVICE_CONTEXT
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT); 

    // Declare this driver as an upper-level filter
    WdfFdoInitSetFilter(DeviceInit);

    status = WdfDeviceCreate(
        &DeviceInit,
        &attributes,
        &device
    );

    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "EvtDeviceAdd: Failed to create device\n");
        return status;
    }

    deviceContext = GetDeviceContext(device);


    // Query Bus Interface and obtain PCI cfg Space
    BUS_INTERFACE_STANDARD busInterface;
    RtlZeroMemory(&busInterface, sizeof(busInterface)); // Intialize the struct with zero

    status = WdfFdoQueryForInterface(
        device,
        &GUID_BUS_INTERFACE_STANDARD,
        (PINTERFACE)&busInterface,
        sizeof(BUS_INTERFACE_STANDARD),
        1,
        NULL
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint(("Query failed!\n"));
        return status;
    }

    if (busInterface.GetBusData == NULL) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "BUS_INTERFACE_STANDARD returned but GetBusData is NULL\n");
        if (busInterface.InterfaceDereference) {
            busInterface.InterfaceDereference(busInterface.Context);
        }
        return STATUS_NOT_SUPPORTED;
    }

    if (busInterface.InterfaceReference) {
        busInterface.InterfaceReference(busInterface.Context);
    }

    deviceContext->bytes = busInterface.GetBusData(
        busInterface.Context,
        PCI_WHICHSPACE_CONFIG,
        deviceContext->pci_config,
        0,
        PCI_EXTENDED_CONFIG_SIZE
    );

    if (deviceContext->bytes == 0) {
        DbgPrint("PCI GetBusData returned 0 bytes!\n");
    }

    // Obtain Device Information

    PDEVICE_OBJECT pdo = WdfDeviceWdmGetPhysicalDevice(device);

    ULONG locationLength = 0;

    WCHAR devInfo[256] = { 0 }; // Pointer to the device information
    PWCHAR pdevInfo = devInfo;

    // Get BDF from Device Information
    status = IoGetDeviceProperty(
        pdo,
        DevicePropertyLocationInformation,
        sizeof(devInfo),
        devInfo,
        &locationLength
    );

    if (NT_SUCCESS(status)) {
        if (swscanf_s(devInfo, L"PCI bus %d, device %d, function %d", &deviceContext->busID, &deviceContext->deviceID, &deviceContext->functionID) == 3) {
            DbgPrint("%d:%d.%d\n", deviceContext->busID, deviceContext->deviceID, deviceContext->functionID);
        }
        else {
            DbgPrint("Failed to parse BDF string: %ws\n", devInfo);
        }
    }

    if ((locationLength > sizeof(devInfo)) && (status == STATUS_BUFFER_TOO_SMALL)) {
        pdevInfo = ExAllocatePoolZero(NonPagedPoolNx, locationLength, TAG);

        if (devInfo == NULL) {
            DbgPrint("Location Information could not be retrieved (alloc failed).");
            return STATUS_NOT_SUPPORTED;
        }

        status = IoGetDeviceProperty(
            pdo,
            DevicePropertyLocationInformation,
            locationLength,
            pdevInfo,
            &locationLength
        );

        if (!NT_SUCCESS(status)) {
            DbgPrint("Failed to get device location information.");
        }

        if (NT_SUCCESS(status)) {
            if (swscanf_s(devInfo, L"PCI bus %d, device %d, function %d", &deviceContext->busID, &deviceContext->deviceID, &deviceContext->functionID) == 3) {
                DbgPrint("%d:%d.%d\n", deviceContext->busID, deviceContext->deviceID, deviceContext->functionID);
            }
            else {
                DbgPrint("Failed to parse BDF string: %ws\n", devInfo);
            }
        }

    }

    // Create symbolic Link Dynamically for multiple device creation
    WCHAR symbolicLinkName[128];
    swprintf_s(symbolicLinkName, 128, L"\\??\\kmdfCfgSpcRdDeviceLink_%d_%d_%d", deviceContext->busID, deviceContext->deviceID, deviceContext->functionID);

    UNICODE_STRING uSymbolicLink;
    RtlInitUnicodeString(&uSymbolicLink, symbolicLinkName);
    status = WdfDeviceCreateSymbolicLink(device, &uSymbolicLink);

    if (!NT_SUCCESS(status))
    {
        DbgPrint("Error creating symbolic link %wZ", uSymbolicLink);
        return status;
    }

    // Create I/O Queue
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDFQUEUE hQueue;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &ioQueueConfig,
        WdfIoQueueDispatchParallel
    );

    ioQueueConfig.EvtIoDeviceControl = EvtIoDeviceControl;

    status = WdfIoQueueCreate(
        device,
        &ioQueueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &hQueue
    );
    if (!NT_SUCCESS(status)) {
        DbgPrint("EvtDeviceAdd: Failed to create I/O Queue\n");
        return status;
    }

    // Print config space
    for (ULONG i = 0; i < deviceContext->bytes; i += 16) {
        DbgPrint("%04X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X \n", i,
            deviceContext->pci_config[i], deviceContext->pci_config[i + 1], deviceContext->pci_config[i + 2], deviceContext->pci_config[i + 3],
            deviceContext->pci_config[i + 4], deviceContext->pci_config[i + 5], deviceContext->pci_config[i + 6], deviceContext->pci_config[i + 7],
            deviceContext->pci_config[i + 8], deviceContext->pci_config[i + 9], deviceContext->pci_config[i + 10], deviceContext->pci_config[i + 11],
            deviceContext->pci_config[i + 12], deviceContext->pci_config[i + 13], deviceContext->pci_config[i + 14], deviceContext->pci_config[i + 15]
        );
    }

    if (busInterface.InterfaceDereference) {
        busInterface.InterfaceDereference(busInterface.Context);
    }

    return STATUS_SUCCESS;
}


VOID EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    UNREFERENCED_PARAMETER(InputBufferLength);

    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT deviceContext = GetDeviceContext(device);
    size_t bytesReturned = 0;

    if (IoControlCode == IOCTL_GET_CFG) {
        PVOID outputBuffer;

        // Make sure the output buffer is big enough
        if (OutputBufferLength < sizeof(PCI_INFO)) {
            status = STATUS_BUFFER_TOO_SMALL;
            DbgPrint("Buffer requested was too small.\nRequest a bigger output buffer size.");
            return;
        }

        // Get the output buffer
        status = WdfRequestRetrieveOutputBuffer(
            Request,
            sizeof(PCI_INFO),
            &outputBuffer,
            NULL
        );

        if (NT_SUCCESS(status)) {
            // Copy our saved PCI config data to the output buffer 

            PPCI_INFO dump = (PPCI_INFO)outputBuffer;

            RtlZeroMemory(dump, sizeof(PCI_INFO));
            RtlCopyMemory(dump->pci_config, deviceContext->pci_config, deviceContext->bytes);
            dump->busID = deviceContext->busID;
            dump->deviceID = deviceContext->deviceID;
            dump->functionID = deviceContext->functionID;
            dump->bytes = deviceContext->bytes;

            bytesReturned = sizeof(PCI_INFO);
            DbgPrint("IOCTL: Returned %d bytes of PCI config data\n", bytesReturned);
        }
    }

    // Complete the request and tell user-mode how many bytes we returned
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
    return;
}