/*
 * fiwix/include/fiwix/efi.h
 *
 * Minimal UEFI definitions for the Fiwix64 EFI stub (M1).
 *
 * Only the types and Boot Services entries needed to collect the memory
 * map and exit boot services are declared. Field order and offsets follow
 * the UEFI 2.x specification (x64); do not reorder.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FIWIX_EFI_H
#define _FIWIX_EFI_H

typedef unsigned char		UINT8;
typedef unsigned short		UINT16;
typedef unsigned int		UINT32;
typedef unsigned long long	UINT64;
typedef signed long long	INT64;
typedef unsigned long		UINTN;		/* pointer-sized on x64 */
typedef long			INTN;
typedef UINTN			EFI_STATUS;
typedef void			*EFI_HANDLE;
typedef UINT64			EFI_PHYSICAL_ADDRESS;
typedef UINT64			EFI_VIRTUAL_ADDRESS;
typedef UINT16			CHAR16;

#define EFIAPI			__attribute__((ms_abi))

#ifndef NULL
#define NULL			((void *)0)
#endif

#define EFI_SUCCESS		0
#define EFI_ERROR_MASK		0x8000000000000000ULL
#define EFI_BUFFER_TOO_SMALL	(EFI_ERROR_MASK | 5)

#define EFI_SYSTEM_TABLE_SIGNATURE	0x5453595320494249ULL	/* 'IBI SYST' */

typedef struct {
	UINT32 data1;
	UINT16 data2;
	UINT16 data3;
	UINT8  data4[8];
} EFI_GUID;

typedef struct {
	UINT64 Signature;
	UINT32 Revision;
	UINT32 HeaderSize;
	UINT32 CRC32;
	UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef enum {
	EfiReservedMemoryType	= 0,
	EfiLoaderCode		= 1,
	EfiLoaderData		= 2,
	EfiBootServicesCode	= 3,
	EfiBootServicesData	= 4,
	EfiRuntimeServicesCode	= 5,
	EfiRuntimeServicesData	= 6,
	EfiConventionalMemory	= 7,
	EfiUnusableMemory	= 8,
	EfiACPIReclaimMemory	= 9,
	EfiACPIMemoryNVS	= 10,
	EfiMemoryMappedIO	= 11,
	EfiMemoryMappedIOPortSpace = 12,
	EfiPalCode		= 13,
	EfiPersistentMemory	= 14,
	EfiMaxMemoryType	= 15
} EFI_MEMORY_TYPE;

typedef struct {
	UINT32		Type;
	UINT32		Padding;
	EFI_PHYSICAL_ADDRESS PhysicalStart;
	EFI_VIRTUAL_ADDRESS	VirtualStart;
	UINT64		NumberOfPages;
	UINT64		Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_RAISE_TPL)(UINTN);
typedef EFI_STATUS (EFIAPI *EFI_RESTORE_TPL)(UINTN);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(INTN, EFI_MEMORY_TYPE, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, UINT32 *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE, UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

struct _EFI_BOOT_SERVICES {
	EFI_TABLE_HEADER Header;		/* 0x00 */
	EFI_RAISE_TPL RaiseTPL;			/* 0x18 */
	EFI_RESTORE_TPL RestoreTPL;		/* 0x20 */
	EFI_ALLOCATE_PAGES AllocatePages;	/* 0x28 */
	EFI_FREE_PAGES FreePages;		/* 0x30 */
	EFI_GET_MEMORY_MAP GetMemoryMap;	/* 0x38 */
	EFI_ALLOCATE_POOL AllocatePool;		/* 0x40 */
	EFI_FREE_POOL FreePool;			/* 0x48 */
	void *CreateEvent;			/* 0x50 */
	void *SetTimer;				/* 0x58 */
	void *WaitForEvent;			/* 0x60 */
	void *SignalEvent;			/* 0x68 */
	void *CloseEvent;			/* 0x70 */
	void *CheckEvent;			/* 0x78 */
	void *InstallProtocolInterface;		/* 0x80 */
	void *ReinstallProtocolInterface;	/* 0x88 */
	void *UninstallProtocolInterface;	/* 0x90 */
	void *HandleProtocol;			/* 0x98 */
	void *Reserved;				/* 0xA0 */
	void *RegisterProtocolNotify;		/* 0xA8 */
	void *LocateHandle;			/* 0xB0 */
	void *LocateDevicePath;			/* 0xB8 */
	void *InstallConfigurationTable;	/* 0xC0 */
	void *LoadImage;			/* 0xC8 */
	void *StartImage;			/* 0xD0 */
	void *Exit;				/* 0xD8 */
	void *UnloadImage;			/* 0xE0 */
	EFI_EXIT_BOOT_SERVICES ExitBootServices;	/* 0xE8 */
	void *GetNextMonotonicCount;		/* 0xF0 */
	void *Stall;				/* 0xF8 */
	void *SetWatchdogTimer;			/* 0x100 */
	void *ConnectController;		/* 0x108 */
	void *DisconnectController;		/* 0x110 */
	void *OpenProtocol;			/* 0x118 */
	void *CloseProtocol;			/* 0x120 */
	void *OpenProtocolInformation;		/* 0x128 */
	void *ProtocolsPerHandle;		/* 0x130 */
	void *LocateHandleBuffer;		/* 0x138 */
	void *LocateProtocol;			/* 0x140 */
	void *InstallMultipleProtocolInterfaces;	/* 0x148 */
	void *UninstallMultipleProtocolInterfaces;	/* 0x150 */
	void *CalculateCrc32;			/* 0x158 */
	void *CopyMem;				/* 0x160 */
	void *SetMem;				/* 0x168 */
	void *CreateEventEx;			/* 0x170 */
};

typedef struct {
	EFI_GUID Signature;			/* 0x00 */
	UINT32 Revision;			/* 0x10 */
	UINT32 FirmwareBuildNumber;		/* 0x14 */
	CHAR16 *FirmwareVendor;			/* 0x18 */
	UINT32 FirmwareRevision;		/* 0x20 */
	EFI_HANDLE ConsoleInHandle;		/* 0x28 */
	void *ConIn;				/* 0x30 */
	EFI_HANDLE ConsoleOutHandle;		/* 0x38 */
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;	/* 0x40 */
	EFI_HANDLE StandardErrorHandle;		/* 0x48 */
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;	/* 0x50 */
	struct _EFI_RUNTIME_SERVICES *RuntimeServices;	/* 0x58 */
	struct _EFI_BOOT_SERVICES *BootServices;	/* 0x60 */
	UINTN NumberOfTableEntries;		/* 0x68 */
	void *ConfigurationTable;		/* 0x70 */
} EFI_SYSTEM_TABLE;

typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;

#endif /* _FIWIX_EFI_H */
