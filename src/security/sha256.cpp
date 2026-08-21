#include "kf2/security/sha256.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <limits>
#include <vector>

namespace kf2::security {
namespace {

std::string encode_digest(const std::vector<UCHAR>& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(digest.size() * 2);
    for (const UCHAR byte : digest) {
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & 0x0FU]);
    }
    return encoded;
}

bool same_file_state(const BY_HANDLE_FILE_INFORMATION& left,
                     const BY_HANDLE_FILE_INFORMATION& right) noexcept {
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
        left.nFileIndexHigh == right.nFileIndexHigh &&
        left.nFileIndexLow == right.nFileIndexLow &&
        left.nFileSizeHigh == right.nFileSizeHigh &&
        left.nFileSizeLow == right.nFileSizeLow &&
        left.ftLastWriteTime.dwHighDateTime ==
            right.ftLastWriteTime.dwHighDateTime &&
        left.ftLastWriteTime.dwLowDateTime ==
            right.ftLastWriteTime.dwLowDateTime;
}

}  // namespace

Result<std::string> sha256_hex(std::string_view bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD returned = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                   reinterpret_cast<PUCHAR>(&object_size),
                                   sizeof(object_size), &returned, 0);
    }
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&hash_size),
                                   sizeof(hash_size), &returned, 0);
    }
    std::vector<UCHAR> object(object_size);
    std::vector<UCHAR> digest(hash_size);
    if (status >= 0) {
        status = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                                  nullptr, 0, 0);
    }
    if (status >= 0 && !bytes.empty()) {
        status = BCryptHashData(
            hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
            static_cast<ULONG>(bytes.size()), 0);
    }
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), hash_size, 0);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0 || digest.size() != 32) {
        return Result<std::string>::failure(
            {ErrorCode::platform_failure, L"SHA-256 operation failed",
             static_cast<std::uint32_t>(status)});
    }
    return Result<std::string>::success(encode_digest(digest));
}

Result<std::string> sha256_file_hex(const std::filesystem::path& path,
                                    std::uint64_t maximum_bytes) {
    if (path.empty() || maximum_bytes == 0) {
        return Result<std::string>::failure(
            {ErrorCode::invalid_argument, L"SHA-256 file request is invalid", 0});
    }
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"File cannot be opened for SHA-256",
             GetLastError()});
    }
    struct CloseFile {
        HANDLE value;
        ~CloseFile() { CloseHandle(value); }
    } close_file{file};

    BY_HANDLE_FILE_INFORMATION before{};
    if (!GetFileInformationByHandle(file, &before) ||
        (before.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        before.nNumberOfLinks != 1) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"SHA-256 input is not a safe single-link regular file",
             GetLastError()});
    }
    const std::uint64_t size =
        (static_cast<std::uint64_t>(before.nFileSizeHigh) << 32U) |
        before.nFileSizeLow;
    if (size > maximum_bytes) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"SHA-256 input exceeds its size limit", 0});
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD hash_size = 0;
    DWORD returned = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                   reinterpret_cast<PUCHAR>(&object_size),
                                   sizeof(object_size), &returned, 0);
    }
    if (status >= 0) {
        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                                   reinterpret_cast<PUCHAR>(&hash_size),
                                   sizeof(hash_size), &returned, 0);
    }
    std::vector<UCHAR> object(object_size);
    std::vector<UCHAR> digest(hash_size);
    if (status >= 0) {
        status = BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                                  nullptr, 0, 0);
    }
    std::array<UCHAR, 64 * 1024> buffer{};
    std::uint64_t read_total = 0;
    while (status >= 0) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &read, nullptr)) {
            status = static_cast<NTSTATUS>(0xC0000001L);
            break;
        }
        if (read == 0) break;
        read_total += read;
        if (read_total > size ||
            BCryptHashData(hash, buffer.data(), read, 0) < 0) {
            status = static_cast<NTSTATUS>(0xC0000001L);
            break;
        }
    }
    if (status >= 0 && read_total == size) {
        status = BCryptFinishHash(hash, digest.data(), hash_size, 0);
    } else if (status >= 0) {
        status = static_cast<NTSTATUS>(0xC0000001L);
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);

    BY_HANDLE_FILE_INFORMATION after{};
    if (status < 0 || digest.size() != 32 ||
        !GetFileInformationByHandle(file, &after)) {
        return Result<std::string>::failure(
            {ErrorCode::platform_failure, L"SHA-256 file operation failed",
             static_cast<std::uint32_t>(status)});
    }
    if (!same_file_state(before, after)) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data,
             L"SHA-256 input changed while it was being verified", 0});
    }
    return Result<std::string>::success(encode_digest(digest));
}

}  // namespace kf2::security
