// SPDX-License-Identifier: BSD-2-Clause-Patent
// d2xq: start the source-backed UFS adapter after the original storage family loads.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include <kern/thread.h>
#include <IOKit/IOLib.h>

extern kmod_info_t g_kernel_kmod_info;
extern "C" void source_storage_base_init(void) __asm__("__GLOBAL__sub_I_IOBlockStorageDevice.cpp");
extern "C" void source_storage_ufs_init(void) __asm__("__GLOBAL__sub_I_NativeUfs.cpp");
extern "C" void source_storage_ram_init(void) __asm__("__GLOBAL__sub_I_NativeRamBlock.cpp");

static const OSMetaClass *storageParent;
static bool attempted;

// The driver records its bring-up stage here so the watchdog can report a stall.
extern "C" volatile int d2xq_ufs_stage = -1;
extern "C" void d2xq_console_text(const char *);

static void
d2xq_rd_test(void)
{
	IORegistryEntry *map = IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane);
	uint64_t address = 0, size = 0;
	uint32_t first = 0;
	if (map != nullptr) {
		OSData *data = OSDynamicCast(OSData, map->getProperty("RAMDisk"));
		if (data != nullptr && data->getLength() >= 16) {
			const uint64_t *entry = static_cast<const uint64_t *>(data->getBytesNoCopy());
			address = entry[0];
			size = entry[1];
		}
		map->release();
	}
	panic("RD addr=%llx size=%llx first=%08x", address, size, first);
}


static void
d2xq_ufs_watchdog(void *, wait_result_t)
{
	IOSleep(20000);
	if (d2xq_ufs_stage >= 0) {
		panic("SourceStorage: UFS stalled at stage %d", d2xq_ufs_stage);
	}
}

// Used only by the deferred source IOBlockStorageDevice metaclass constructor.
// The C++ class name stays unchanged; only its registered name differs.
extern "C" const OSMetaClass *native_source_storage_parent(void)
{
	if (!storageParent) {
		panic("SourceStorage: metaclass initialized before original family");
	}
	return storageParent;
}

extern "C" void native_source_storage_start(void)
{
	unsigned enabled = 0;

	if (!PE_parse_boot_argn("native-source-storage", &enabled, sizeof(enabled)) || enabled != 1 || attempted) {
		return;
	}
	d2xq_console_text("V8 ");
	int rdtest = 0;
	PE_parse_boot_argn("d2xq-rdtest", &rdtest, sizeof(rdtest));
	if (rdtest) {
		d2xq_rd_test();
	}
	attempted = true;
	d2xq_console_text("V8 ");

	d2xq_ufs_stage = 1; d2xq_console_text("S1 ");
	thread_t watchdog = nullptr;
	if (kernel_thread_start(d2xq_ufs_watchdog, nullptr, &watchdog) == KERN_SUCCESS) {
		thread_deallocate(watchdog);
	}

	OSReturn status = OSKextLoadKextWithIdentifier("com.apple.iokit.IOStorageFamily");
	if (status != kOSReturnSuccess) {
		panic("SourceStorage: IOStorageFamily load failed %x", status);
	}
	d2xq_ufs_stage = 2; d2xq_console_text("S2 ");
	const OSSymbol *name = OSSymbol::withCString("IOBlockStorageDevice");
	storageParent = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
	if (name) {
		name->release();
	}
	if (!storageParent || storageParent->getClassSize() != sizeof(IOBlockStorageDevice)) {
		panic("SourceStorage: original base layout %u differs from source %lu",
		    storageParent ? storageParent->getClassSize() : 0, sizeof(IOBlockStorageDevice));
	}

	d2xq_ufs_stage = 3; d2xq_console_text("S3 ");
	void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
	if (!handle) {
		panic("SourceStorage: cannot begin class registration");
	}
	d2xq_ufs_stage = 4; d2xq_console_text("S4 ");
	source_storage_base_init();
	source_storage_ufs_init();
	source_storage_ram_init();
	status = OSMetaClass::postModLoad(handle);
	if (status != kOSReturnSuccess) {
		panic("SourceStorage: class registration failed %x", status);
	}

	d2xq_ufs_stage = 5; d2xq_console_text("S5 ");
	OSDictionary *matching = IOService::serviceMatching("IOPlatformExpertDevice");
	IOService *provider = matching ? IOService::waitForMatchingService(matching, 5000000000ULL) : nullptr;
	if (!provider) {
		panic("SourceStorage: platform root unavailable");
	}
	d2xq_ufs_stage = 6; d2xq_console_text("S6 ");
	// The pinned NativeUfs build has no source factory; allocate the registered
	// class directly (same object the factory would return).
	d2xq_ufs_stage = 7; d2xq_console_text("S7 ");
	IOService *driver = OSDynamicCast(IOService, OSMetaClass::allocClassWithName("NativeUfs"));
	if (!driver) {
		panic("SourceStorage: cannot allocate UFS service");
	}
	const char *step = !driver->init() ? "init" : !driver->attach(provider) ? "attach" :
	    !driver->start(provider) ? "start" : nullptr;
	if (step) {
		IORegistryEntry *ufs = IORegistryEntry::fromPath("/arm-io/ufshc", gIODTPlane);
		char compatible[96] = {};
		const char *text = "none";
		if (ufs) {
			OSObject *property = ufs->getProperty("compatible");
			OSData *data = OSDynamicCast(OSData, property);
			OSString *string = OSDynamicCast(OSString, property);
			if (data) {
				unsigned length = data->getLength() > 80 ? 80 : data->getLength();
				memcpy(compatible, data->getBytesNoCopy(), length);
				text = compatible;
			} else if (string) {
				strlcpy(compatible, string->getCStringNoCopy(), sizeof(compatible));
				text = compatible;
			}
		}
		panic("SourceStorage: UFS %s failed\nufshc=%p compatible=%.48s\nprovider=%s driver=%s",
		    step, ufs, text,
		    provider->getMetaClass() ? provider->getMetaClass()->getClassName() : "?",
		    driver->getMetaClass() ? driver->getMetaClass()->getClassName() : "?");
	}
	d2xq_ufs_stage = -1;
	printf("SourceStorage: UFS started through public IOStorageFamily interface\n");
}
