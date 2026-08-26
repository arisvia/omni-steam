#include "DepotKeyStore.h"
#include "omni_check.h"

#include <iostream>
#include <string>

void TestDepotKeyStore() {
    Manager::DepotKeyStore::Initialize();
    std::cout << "[INFO] Loaded Depot Keys count: " << Manager::DepotKeyStore::Count() << "\n";
    OMNI_CHECK(Manager::DepotKeyStore::Count() > 0);
    // Check known depot key from depotkeys.bin (depot 1)
    std::string key1 = Manager::DepotKeyStore::GetKeyForDepot(1);
    OMNI_CHECK(!key1.empty());
    OMNI_CHECK(key1 == "b465d45ab2a7c396f7d1c08a6644e68529ec86b14da77e18588abbbcd2412060");

    // Check binary search on a higher depot ID
    OMNI_CHECK(Manager::DepotKeyStore::HasKey(1));
    OMNI_CHECK(!Manager::DepotKeyStore::HasKey(999999999));

    // Check multi-depot resolution for App 2358720 (Black Myth: Wukong) with DLCs (2672610, 3288260)
    auto wukongKeys = Manager::DepotKeyStore::FindDepotKeysForApp(2358720, {2672610, 3288260});
    OMNI_CHECK(!wukongKeys.empty());
    OMNI_CHECK(wukongKeys.count(2358720) > 0 || wukongKeys.count(2358721) > 0);
    OMNI_CHECK(wukongKeys.count(2672611) > 0); // DLC 1 Depot
    OMNI_CHECK(wukongKeys.count(3288261) > 0); // DLC 2 Depot 1
    OMNI_CHECK(wukongKeys.count(3288262) > 0); // DLC 2 Depot 2
    std::cout << "[INFO] Verified Black Myth Wukong & DLC Depots matched (" << wukongKeys.size() << " keys)\n";
    std::cout << "[PASS] TestDepotKeyStore (Binary format loaded & Depot 1 verified)\n";
}

int main() {
    std::cout << "Running OmniSteam DepotKeyStore Tests...\n";
    TestDepotKeyStore();
    std::cout << "All DepotKeyStore Tests Passed!\n";
    return 0;
}
