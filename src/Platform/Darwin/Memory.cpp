#include <cstdint>
#include <cstring>
#include <mach/mach.h>
#include <mach/vm_map.h>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

bool Memory::Protect(void* address, size_t size, uint32_t newProtect, uint32_t* oldProtect) {
    vm_prot_t prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
    return vm_protect(mach_task_self(), reinterpret_cast<vm_address_t>(address), size, FALSE, prot) == KERN_SUCCESS;
}

bool Memory::Read(void* address, void* buffer, size_t size) {
    if (!address || !buffer || size == 0)
        return false;
    std::memcpy(buffer, address, size);
    return true;
}

bool Memory::Write(void* address, const void* buffer, size_t size) {
    if (!address || !buffer || size == 0)
        return false;
    if (!Protect(address, size, 0))
        return false;
    std::memcpy(address, buffer, size);
    return true;
}

} // namespace OmniPlatform
