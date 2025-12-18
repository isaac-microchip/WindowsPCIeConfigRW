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

#include <iostream>
#include <Windows.h>
#include <vector>
#include <string>

using namespace std;

#define IOCTL_GET_CFG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

int main(int argc, char** argv)
{
    // Declare array for all symbolic links present in device
    WCHAR* symlnk = new WCHAR[65536];
    DWORD devices;
    vector<wstring> symlnkDev;

    
    devices = QueryDosDevice(NULL, symlnk, 65536); // Obtain all MS-DOS device names 

    if (devices) {
        WCHAR* pSymlnk = symlnk;
        int numSymlnk = 0;
        while (*pSymlnk) {

            if (wcsncmp(pSymlnk, L"kmdfCfgSpcRdDeviceLink", wcslen(L"kmdfCfgSpcRdDeviceLink")) == 0) { // both characters start with kmdfCfgSpcRdDeviceLink
                symlnkDev.emplace_back(pSymlnk);
            }
            pSymlnk += wcslen(pSymlnk) + 1;
        }
        delete[] symlnk;
    }
    else if (devices == 0) {
        printf("Failed: larger size of array is required for symlnk");
    }

    for (const auto& link : symlnkDev) {
        HANDLE device = INVALID_HANDLE_VALUE;
        BOOL status = FALSE;
        DWORD bytesReturned = 0;
        typedef struct _PCI_INFO {
            UCHAR pci_config[4096];
            ULONG bytes;
            int busID;
            int deviceID;
            int functionID;
        } PCI_INFO, * PPCI_INFO;
        CHAR outputBuffer[sizeof(PCI_INFO)] = { 0 };
        
        wstring dosLink = L"\\\\.\\" + link;
        
        // Open driver device
        device = CreateFile(dosLink.c_str(), GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

        if (device == INVALID_HANDLE_VALUE)
        {
            printf_s("> Could not open device: 0x%x\n", GetLastError());
            return FALSE;
        }

        status = DeviceIoControl(device, IOCTL_GET_CFG, NULL, 0, outputBuffer, sizeof(PCI_INFO), &bytesReturned, (LPOVERLAPPED)NULL);

        PPCI_INFO dump = (PPCI_INFO)outputBuffer;

        printf("%d:%d.%d\n", dump->busID, dump->deviceID, dump->functionID);

        for (ULONG i = 0; i < dump->bytes; i += 16) {

            printf("%04X: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X \n", i,
                dump->pci_config[i], dump->pci_config[i + 1], dump->pci_config[i + 2], dump->pci_config[i + 3],
                dump->pci_config[i + 4], dump->pci_config[i + 5], dump->pci_config[i + 6], dump->pci_config[i + 7],
                dump->pci_config[i + 8], dump->pci_config[i + 9], dump->pci_config[i + 10], dump->pci_config[i + 11],
                dump->pci_config[i + 12], dump->pci_config[i + 13], dump->pci_config[i + 14], dump->pci_config[i + 15]
            );
        }

        CloseHandle(device); 
    }
    
}