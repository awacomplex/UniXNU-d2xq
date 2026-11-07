// SPDX-License-Identifier: MIT
// Original 21H461 ApplePMGR ABI and nub lifecycle for the CI194 read-only gate.
#include "CI194PMGR.h"
#include "OriginalPMGRABI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/OSKextLib.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSSymbol.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>

extern kmod_info_t g_kernel_kmod_info;
extern "C" {
void sheng_pmgr_static_init(void) __asm__("__GLOBAL__sub_I_ShengPlatformPower.cpp");
IOService *d2xq_original_pmgr_nub_create(IORegistryEntry *, ApplePMGR *);
uint32_t d2xq_original_pmgr_nub_read_aon(IOService *);
void d2xq_original_pmgr_nub_write_aon(IOService *, uint64_t);
extern const OSMetaClass d2xq_original_pmgr_nub_metaclass;
void d2xq_console_text(const char *);
}

__attribute__((noreturn)) static void unavailable(unsigned slot)
{
    panic("CI194 PMGR REFUSE unsupported operation slot=%u", slot);
}

// The original constructor initializes the full original private allocation.
// CPU, cluster, idle and unknown methods fail explicitly. Only original AON
// methods are inherited: in this exact target they return -1 / do no work.
class ShengPlatformPower : public ApplePMGR {
    OSDeclareDefaultStructors(ShengPlatformPower)
public:
    void enableCPUCore(unsigned, uint64_t) override { unavailable(168); }
    void enableCPUCore(unsigned) override { unavailable(169); }
    void disableCPUCore(unsigned) override { unavailable(170); }
    void enableCPUCluster(unsigned) override { unavailable(171); }
    void disableCPUCluster(unsigned) override { unavailable(172); }
    void initCPUIdle(ml_processor_info_t *) override { unavailable(173); }
    void enterCPUIdle(UInt64 *) override { unavailable(174); }
    void exitCPUIdle(UInt64 *) override { unavailable(175); }
    void updateCPUIdle(UInt64 *) override { unavailable(176); }
#include "GuardedPMGRSlots.inc"
};

OSDefineMetaClassWithInit(ShengPlatformPower, ApplePMGR, )
OSMetaClassConstructorInit(ShengPlatformPower, ApplePMGR, )
OSDefineBasicStructors(ShengPlatformPower, ApplePMGR)

// The original provider is larger than kalloc_type's 16 KiB limit.
void *ShengPlatformPower::operator new(size_t size)
{
    return OSObject::operator new(size);
}

void ShengPlatformPower::operator delete(void *memory, size_t size)
{
    OSObject::operator delete(memory, size);
}

static_assert(sizeof(IOPMGR) == 0x88, "Original 21H461 IOPMGR base layout");
static_assert(sizeof(ShengPlatformPower) == 0x4b8a8, "Original PMGR provider layout");

// Keep the owning allocations for this boot. Never release a published service
// while an original consumer may still hold a reference to it.
static ShengPlatformPower *provider;
static IOService *nub;
static IOService *platform;
static const OSMetaClass *parentMeta;
static const OSMetaClass *nubMeta;
static unsigned lifecycle; // 0 unattempted, 1 preparing, 2 fully published
static bool aonChecked;

static const OSMetaClass *lookup(const char *text)
{
    const OSSymbol *name = OSSymbol::withCString(text);
    const OSMetaClass *meta = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
    OSSafeReleaseNULL(name);
    return meta;
}

static bool providerSlotsMatch(void)
{
    uintptr_t table = 0, read = 0, write = 0;
    bcopy(provider, &table, sizeof(table));
    if (!table) return false;
    bcopy(reinterpret_cast<const void *>(table + 320 * sizeof(uintptr_t)), &read, sizeof(read));
    bcopy(reinterpret_cast<const void *>(table + 321 * sizeof(uintptr_t)), &write, sizeof(write));
    return read == kCI194OriginalReadAon && write == kCI194OriginalWriteAon;
}

extern "C" int d2xq_ci194_pmgr_ready(void)
{
    if (lifecycle != 2 || !aonChecked || !provider || !nub || !platform ||
        parentMeta != &ApplePMGR::gMetaClass ||
        nubMeta != &d2xq_original_pmgr_nub_metaclass ||
        lookup("ApplePMGR") != parentMeta || lookup("ApplePMGRNub") != nubMeta ||
        parentMeta->getClassSize() != sizeof(ApplePMGR) ||
        parentMeta->getSuperClass() != &IOPMGR::gMetaClass ||
        nubMeta->getClassSize() != 0x740 ||
        nubMeta->getSuperClass() != &IOService::gMetaClass ||
        lookup("ShengPlatformPower") != &ShengPlatformPower::gMetaClass ||
        ShengPlatformPower::gMetaClass.getSuperClass() != parentMeta ||
        provider->getMetaClass() != &ShengPlatformPower::gMetaClass ||
        provider->getMetaClass()->getClassSize() != sizeof(ShengPlatformPower) ||
        provider->metaCast("ApplePMGR") != provider ||
        nub->getMetaClass() != nubMeta || provider->getProvider() != platform ||
        nub->getProvider() != provider || provider->isInactive() || nub->isInactive() ||
        !provider->inPlane(gIOServicePlane) || !nub->inPlane(gIOServicePlane) ||
        !providerSlotsMatch()) {
        return 0;
    }

    // Original nub methods dispatch through this separately stored field,
    // rather than IOService::getProvider(). The builder audits its instruction.
    ApplePMGR *dispatchProvider = nullptr;
    bcopy(reinterpret_cast<const uint8_t *>(nub) + 0x88,
          &dispatchProvider, sizeof(dispatchProvider));
    if (dispatchProvider != provider) return 0;

    // Use the normal published-service matching API used by original consumers.
    // A second nub or subclass is ambiguous and must never qualify as ready.
    OSDictionary *matching = IOService::serviceMatching("ApplePMGRNub");
    OSIterator *services = matching ? IOService::getMatchingServices(matching) : nullptr;
    OSSafeReleaseNULL(matching);
    if (!services) return 0;
    unsigned count = 0;
    bool exact = true;
    while (OSObject *object = services->getNextObject()) {
        if (++count != 1 || object != nub) {
            exact = false;
            break;
        }
    }
    OSSafeReleaseNULL(services);
    return exact && count == 1 ? 1 : 0;
}

extern "C" void sheng_pmgr_run(void)
{
    unsigned mode = 0, ci194 = 0;
    if (!PE_parse_boot_argn("sheng-pmgr", &mode, sizeof(mode)) || mode != 2 ||
        !PE_parse_boot_argn("native-ufs-ci194", &ci194, sizeof(ci194)) || ci194 != 1) {
        panic("CI194 PMGR REFUSE required diagnostic boot arguments");
    }
    if (lifecycle != 0) panic("CI194 PMGR REFUSE repeated entry");
    lifecycle = 1;
    d2xq_console_text("CI194 PM0 ");

    OSReturn status = OSKextLoadKextWithIdentifier("com.apple.driver.ApplePMGR");
    if (status != kOSReturnSuccess) {
        panic("CI194 PMGR FAIL original module load=%x", status);
    }
    parentMeta = lookup("ApplePMGR");
    nubMeta = lookup("ApplePMGRNub");
    if (!parentMeta || parentMeta != &ApplePMGR::gMetaClass ||
        parentMeta->getClassSize() != sizeof(ApplePMGR) ||
        parentMeta->getSuperClass() != &IOPMGR::gMetaClass ||
        !nubMeta || nubMeta != &d2xq_original_pmgr_nub_metaclass ||
        nubMeta->getClassSize() != 0x740 ||
        nubMeta->getSuperClass() != &IOService::gMetaClass) {
        panic("CI194 PMGR FAIL original class identity or layout");
    }
    d2xq_console_text("PM1 ");

    if (lookup("ShengPlatformPower")) panic("CI194 PMGR REFUSE prior local class");
    void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
    if (!handle) panic("CI194 PMGR FAIL begin local class registration");
    sheng_pmgr_static_init();
    status = OSMetaClass::postModLoad(handle);
    if (status != kOSReturnSuccess ||
        lookup("ShengPlatformPower") != &ShengPlatformPower::gMetaClass ||
        ShengPlatformPower::gMetaClass.getSuperClass() != parentMeta ||
        ShengPlatformPower::gMetaClass.getClassSize() != sizeof(ShengPlatformPower)) {
        panic("CI194 PMGR FAIL local class registration=%x", status);
    }

    platform = IOService::getPlatform();
    provider = new ShengPlatformPower;
    IORegistryEntry *description = new IORegistryEntry;
    if (!platform || !provider || !description || !provider->init() || !description->init()) {
        panic("CI194 PMGR FAIL provider allocation or init");
    }
    // No Apple register ranges, interrupts, power policy or device nodes.
    description->setName("d2xq-ci194-platform-power");
    nub = d2xq_original_pmgr_nub_create(description, provider);
    if (!nub || nub->getMetaClass() != nubMeta ||
        provider->metaCast("ApplePMGR") != provider || !providerSlotsMatch()) {
        panic("CI194 PMGR FAIL original nub factory");
    }
    if (!provider->attach(platform) || !provider->start(platform) ||
        !nub->attach(provider) || !nub->start(provider)) {
        panic("CI194 PMGR FAIL service attachment or start");
    }
    d2xq_console_text("PM2 ");
    provider->registerService(kIOServiceSynchronous);
    nub->registerService(kIOServiceSynchronous);
    OSSafeReleaseNULL(description);

    // Exercise the unmodified original consumer dispatch. This is the target's
    // constant-read/no-op-write contract, not an SM8150 PMIC or latch readback.
    if (d2xq_original_pmgr_nub_read_aon(nub) != UINT32_MAX) {
        panic("CI194 PMGR FAIL original AON dispatch");
    }
    d2xq_original_pmgr_nub_write_aon(nub, UINT32_MAX);
    aonChecked = true;
    lifecycle = 2;
    if (d2xq_ci194_pmgr_ready() != 1) panic("CI194 PMGR FAIL published service readiness");
    d2xq_console_text("CI194 PMGR READY ");
    printf("CI194 PMGR READY original=1 nub=1 ancestry=1 unique=1 aon=ffffffff\n");
}
