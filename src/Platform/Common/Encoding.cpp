#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(OMNI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

#include "OmniPlatform/OmniPlatform.h"

namespace OmniPlatform {
namespace Encoding {

namespace {
inline uint8_t HexCharToNibble(char c) noexcept {
    if (c >= '0' && c <= '9')
        return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return static_cast<uint8_t>(c - 'A' + 10);
    return 0;
}
} // namespace

std::vector<uint8_t> HexToBytes(std::string_view hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        uint8_t high = HexCharToNibble(hex[i]);
        uint8_t low = HexCharToNibble(hex[i + 1]);
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::string BytesToHex(const uint8_t* data, size_t length) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(kHexDigits[(data[i] >> 4) & 0x0F]);
        out.push_back(kHexDigits[data[i] & 0x0F]);
    }
    return out;
}

std::string UrlEncode(std::string_view value) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(value.length() * 3);
    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped.push_back(c);
        } else {
            escaped.push_back('%');
            escaped.push_back(kHexDigits[(static_cast<unsigned char>(c) >> 4) & 0x0F]);
            escaped.push_back(kHexDigits[static_cast<unsigned char>(c) & 0x0F]);
        }
    }
    return escaped;
}

std::string UrlDecode(std::string_view in) {
    std::string out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        if (in[i] == '%' && i + 2 < in.length()) {
            uint8_t high = HexCharToNibble(in[i + 1]);
            uint8_t low = HexCharToNibble(in[i + 2]);
            out.push_back(static_cast<char>((high << 4) | low));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string EscapeJson(std::string_view in) {
    std::string out;
    out.reserve(in.length() + 16);
    for (char c : in) {
        switch (c) {
            case '"':
                out.append("\\\"");
                break;
            case '\\':
                out.append("\\\\");
                break;
            case '\b':
                out.append("\\b");
                break;
            case '\f':
                out.append("\\f");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out.append(buf);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty())
        return "";
#if defined(OMNI_PLATFORM_WINDOWS)
    int size =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.length()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return "";
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.length()), result.data(), size, nullptr,
                        nullptr);
    return result;
#else
    std::string result;
    result.reserve(wide.size());
    for (wchar_t wc : wide) {
        result.push_back(static_cast<char>(wc & 0xFF));
    }
    return result;
#endif
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty())
        return L"";
#if defined(OMNI_PLATFORM_WINDOWS)
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.length()), nullptr, 0);
    if (size <= 0)
        return L"";
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.length()), result.data(), size);
    return result;
#else
    std::wstring result;
    result.reserve(utf8.size());
    for (char c : utf8) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return result;
#endif
}

} // namespace Encoding
} // namespace OmniPlatform
