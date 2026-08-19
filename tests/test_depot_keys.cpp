#include "DepotKeyStore.h"

#include <cassert>
#include <iostream>

void TestDepotKeyStore() {
    Manager::DepotKeyStore::Initialize();
    std::cout << "[INFO] Loaded Depot Keys count: " << Manager::DepotKeyStore::Count() << "\n";
    assert(Manager::DepotKeyStore::Count() > 0);

    // Check known depot key from depotkeys.json (depot 1)
    std::string key1 = Manager::DepotKeyStore::GetKeyForDepot(1);
    assert(!key1.empty());
    assert(key1 == "b465d45ab2a7c396f7d1c08a6644e68529ec86b14da77e18588abbbcd2412060");

    std::cout << "[PASS] TestDepotKeyStore (Depot 1 key matched)\n";
}

int main() {
    std::cout << "Running OmniSteam DepotKeyStore Tests...\n";
    TestDepotKeyStore();
    std::cout << "All DepotKeyStore Tests Passed!\n";
    return 0;
}
