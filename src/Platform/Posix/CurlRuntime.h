#pragma once

namespace OmniPlatform::CurlRuntime {

// One-time, thread-safe libcurl global bootstrap shared by every curl-backed
// translation unit inside a single binary (the Posix Http backend and the
// manager's WebDAV client). Returns false when curl_global_init() failed.
//
// curl_easy_init() would also self-initialise lazily, but that path is not
// thread-safe, and curl_global_init() itself must never run twice in a process -
// hence one shared std::once_flag rather than a per-file copy.
//
// Deliberately never paired with curl_global_cleanup(): a hook injected into
// Steam, or the manager's background upload threads, can still be inside
// curl_easy_perform() while the process tears down.
bool EnsureInitialized();

} // namespace OmniPlatform::CurlRuntime
