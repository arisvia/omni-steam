#pragma once
#include <cstdint>
#include <functional>
#include <string>

// Runtime symbol-table enumeration for loaded modules.
//
// This is the zero-maintenance counterpart to pattern scanning: on platforms
// whose Steam libraries retain symbol information (steamclient.so /
// steamclient.dylib), PatternLoader resolves hook targets directly from the
// module's own symbol data at startup - fully adaptive across client updates.
namespace SymbolTable {

// Iterates defined function symbols of a loaded module (in-memory metadata;
// never modifies the target process).
//
//   name      : raw symbol name (mangled where applicable)
//   demangled : best-effort demangled form (equals name when unavailable)
//   rva       : symbol offset from the module base
//
// Return false from the callback to stop enumeration early.
using SymbolCallback = std::function<bool(const std::string& name, const std::string& demangled, uint64_t rva)>;

bool ForEachFunction(const std::string& moduleName, const SymbolCallback& callback);

// Shared helpers used by the per-platform implementations.
std::string ToLower(std::string value);
bool ModuleMatches(const std::string& modulePath, const std::string& moduleName);

} // namespace SymbolTable
