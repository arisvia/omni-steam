#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {

bool Memory::Protect(void* address, size_t size, uint32_t newProtect, uint32_t* oldProtect) {
    if (!address || size == 0)
        return false;
    long pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t pageStart = reinterpret_cast<uintptr_t>(address) & ~(pageSize - 1);
    size_t totalLength = (reinterpret_cast<uintptr_t>(address) + size) - pageStart;
    return mprotect(reinterpret_cast<void*>(pageStart), totalLength, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
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
