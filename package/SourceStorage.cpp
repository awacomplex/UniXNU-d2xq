// SPDX-License-Identifier: BSD-2-Clause-Patent
// d2xq: start the source-backed UFS adapter after the original storage family loads.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

extern kmod_info_t g_kernel_kmod_info;
extern "C" void source_storage_base_init(void) __asm__("__GLOBAL__sub_I_IOBlockStorageDevice.cpp");
extern "C" void source_storage_ufs_init(void) __asm__("__GLOBAL__sub_I_NativeUfs.cpp");
extern "C" void source_storage_ram_init(void) __asm__("__GLOBAL__sub_I_NativeRamBlock.cpp");

static const OSMetaClass *storageParent;
static bool attempted;

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
	attempted = true;

	OSReturn status = OSKextLoadKextWithIdentifier("com.apple.iokit.IOStorageFamily");
	if (status != kOSReturnSuccess) {
		panic("SourceStorage: IOStorageFamily load failed %x", status);
	}
	const OSSymbol *name = OSSymbol::withCString("IOBlockStorageDevice");
	storageParent = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
	if (name) {
		name->release();
	}
	if (!storageParent || storageParent->getClassSize() != sizeof(IOBlockStorageDevice)) {
		panic("SourceStorage: original base layout %u differs from source %lu",
		    storageParent ? storageParent->getClassSize() : 0, sizeof(IOBlockStorageDevice));
	}

	void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
	if (!handle) {
		panic("SourceStorage: cannot begin class registration");
	}
	source_storage_base_init();
	source_storage_ufs_init();
	source_storage_ram_init();
	status = OSMetaClass::postModLoad(handle);
	if (status != kOSReturnSuccess) {
		panic("SourceStorage: class registration failed %x", status);
	}

	OSDictionary *matching = IOService::serviceMatching("IOPlatformExpertDevice");
	IOService *provider = matching ? IOService::waitForMatchingService(matching, 5000000000ULL) : nullptr;
	if (!provider) {
		panic("SourceStorage: platform root unavailable");
	}
	// The pinned NativeUfs build has no source factory; allocate the registered
	// class directly (same object the factory would return).
	IOService *driver = OSDynamicCast(IOService, OSMetaClass::allocClassWithName("NativeUfs"));
	if (!driver) {
		panic("SourceStorage: cannot allocate UFS service");
	}
	const char *step = !driver->init() ? "init" : !driver->attach(provider) ? "attach" :
	    !driver->start(provider) ? "start" : nullptr;
	if (step) {
		panic("SourceStorage: UFS %s failed", step);
	}
	printf("SourceStorage: UFS started through public IOStorageFamily interface\n");
}
