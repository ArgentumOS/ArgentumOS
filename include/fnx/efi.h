/*
 * fnx/include/fnx/efi.h
 *
 * Minimal UEFI definitions for the FNX EFI stub (M1).
 *
 * Only the types and Boot Services entries needed to collect the memory
 * map and exit boot services are declared. Field order and offsets follow
 * the UEFI 2.x specification (x64); do not reorder.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#ifndef _FNX_EFI_H
#define _FNX_EFI_H

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

/* --- EFI Graphics Output Protocol (GOP), UEFI 2.x -------------------- */

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
	{ 0x9042a9de, 0x23dc, 0x4a38, \
	  { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } }

typedef enum {
	PixelRedGreenBlueReserved8BitPerColor = 0,
	PixelBlueGreenRedReserved8BitPerColor = 1,
	PixelBitMask = 2,
	PixelBltOnly = 3,
	PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
	UINT32 RedMask;
	UINT32 GreenMask;
	UINT32 BlueMask;
	UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
	UINT32 Version;
	UINT32 HorizontalResolution;
	UINT32 VerticalResolution;
	EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
	EFI_PIXEL_BITMASK PixelInformation;
	UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
	UINT32 MaxMode;
	UINT32 Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	UINTN SizeOfInfo;
	EFI_PHYSICAL_ADDRESS FrameBufferBase;
	UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
	EFI_STATUS (EFIAPI *QueryMode)(struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32, UINTN *, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **);
	EFI_STATUS (EFIAPI *SetMode)(struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *, UINT32);
	EFI_STATUS (EFIAPI *Blt)(struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *, void *, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN, UINTN);
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);

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
/* EFI_ALLOCATE_TYPE (UEFI 2.x): the selector AllocatePages() takes.
 * AllocateMaxAddress asks for a run at or below the address passed in, which
 * is how the boot-structures window below is kept low. */
typedef enum {
	AllocateAnyPages,		/* 0 */
	AllocateMaxAddress,		/* 1 */
	AllocateAddress,		/* 2 */
	MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(INTN, EFI_MEMORY_TYPE, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, UINT32 *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE, UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, UINT32);
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL	0x02

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

/* --- EFI Loaded Image Protocol (UEFI 2.x) ------------------------------ */

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
	{ 0x5b1b31a1, 0x9562, 0x11d2, \
	  { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct _EFI_LOADED_IMAGE_PROTOCOL {
	UINT32			Revision;		/* 0x00 */
	EFI_HANDLE		ParentHandle;		/* 0x08 */
	EFI_SYSTEM_TABLE	*SystemTable;		/* 0x10 */
	EFI_HANDLE		DeviceHandle;		/* 0x18: the boot volume */
	struct _EFI_DEVICE_PATH	*FilePath;		/* 0x20: how this image was loaded */
	void			*Reserved;		/* 0x28 */
	UINT32			LoadOptionsSize;	/* 0x30 */
	void			*LoadOptions;		/* 0x38 */
	void			*ImageBase;		/* 0x40 */
	UINT64			ImageSize;		/* 0x48 */
	EFI_MEMORY_TYPE		ImageCodeType;		/* 0x50 */
	EFI_MEMORY_TYPE		ImageDataType;		/* 0x58 */
	EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE);	/* 0x60 */
} EFI_LOADED_IMAGE_PROTOCOL;

/* --- EFI Simple File System + File protocols (UEFI 2.x) --------------- */

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
	{ 0x0964e5b22, 0x6459, 0x11d2, \
	  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_PROTOCOL_GUID \
	{ 0x095e75b2d, 0x6d3f, 0x11d2, \
	  { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ	0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE	0x0000000000000002ULL

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL *, UINTN *, void *);

struct _EFI_FILE_PROTOCOL {
	UINT64			Revision;		/* 0x00 */
	EFI_FILE_OPEN		Open;			/* 0x08 */
	EFI_FILE_CLOSE		Close;			/* 0x10 */
	void			*Delete;		/* 0x18 */
	EFI_FILE_READ		Read;			/* 0x20 */
	EFI_FILE_WRITE		Write;			/* 0x28 */
	void			*GetPosition;		/* 0x30 */
	void			*SetPosition;		/* 0x38 */
	void			*GetInfo;		/* 0x40 */
	void			*SetInfo;		/* 0x48 */
	void			*Flush;			/* 0x50 */
	void			*OpenEx;		/* 0x58 */
	void			*ReadEx;		/* 0x60 */
	void			*WriteEx;		/* 0x68 */
	void			*FlushEx;		/* 0x70 */
};

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
	UINT64			Revision;		/* 0x00 */
	EFI_STATUS (EFIAPI *OpenVolume)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* --- Device path (enough to read LoadedImage->FilePath) --------------- */

typedef struct _EFI_DEVICE_PATH {
	UINT8	Type;
	UINT8	SubType;
	UINT16	Length;
} EFI_DEVICE_PATH;

#define DEVICE_PATH_TYPE_MEDIA		0x04
#define MEDIA_FILEPATH_DP		0x04
#define DEVICE_PATH_TYPE_END		0x7f
#define END_ENTIRE_DEVICE_PATH_SUBTYPE	0xff


typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;

#endif /* _FNX_EFI_H */
