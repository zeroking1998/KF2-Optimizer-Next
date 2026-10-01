#pragma once

#include <Windows.h>
#include <aclapi.h>

namespace kf2::test {

// Use only with this test's own process or an already-owned inert child.
// Existing handles remain valid; restore before closing the borrowed handle.
class ProcessInspectionDenial final {
public:
    ProcessInspectionDenial() = default;
    ProcessInspectionDenial(const ProcessInspectionDenial&) = delete;
    ProcessInspectionDenial& operator=(const ProcessInspectionDenial&) = delete;
    ~ProcessInspectionDenial() {
        static_cast<void>(restore());
        if (descriptor_) LocalFree(descriptor_);
        if (token_) CloseHandle(token_);
    }

    bool deny(HANDLE owned_process) {
        if (process_ || !owned_process) return false;
        process_ = owned_process;
        // An enabled debug privilege bypasses DACLs. Disable only this test
        // process's privilege and save its original state for restoration.
        if (!OpenProcessToken(GetCurrentProcess(),
                TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_)) return false;
        TOKEN_PRIVILEGES disabled{};
        disabled.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME,
                &disabled.Privileges[0].Luid)) return false;
        DWORD length = 0;
        if (!AdjustTokenPrivileges(token_, FALSE, &disabled,
                sizeof(previous_), &previous_, &length)) return false;
        // ERROR_NOT_ALL_ASSIGNED is expected when the token never had this
        // privilege; no privilege was changed in that case.
        const auto privilege_error = GetLastError();
        if (privilege_error != ERROR_SUCCESS &&
            privilege_error != ERROR_NOT_ALL_ASSIGNED) return false;
        if (GetSecurityInfo(process_, SE_KERNEL_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, &original_dacl_,
                nullptr, &descriptor_) != ERROR_SUCCESS) return false;
        ACL empty{};
        if (!InitializeAcl(&empty, sizeof(empty), ACL_REVISION)) return false;
        active_ = SetSecurityInfo(process_, SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, &empty, nullptr) ==
                ERROR_SUCCESS;
        return active_;
    }

    bool restore() {
        bool restored = !active_ || SetSecurityInfo(process_, SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, original_dacl_,
            nullptr) == ERROR_SUCCESS;
        if (restored) active_ = false;
        if (token_ && previous_.PrivilegeCount != 0) {
            if (AdjustTokenPrivileges(token_, FALSE, &previous_, 0,
                    nullptr, nullptr)) previous_ = {};
            else restored = false;
        }
        return restored;
    }

private:
    HANDLE process_{};
    HANDLE token_{};
    TOKEN_PRIVILEGES previous_{};
    PACL original_dacl_{};
    PSECURITY_DESCRIPTOR descriptor_{};
    bool active_{};
};

}  // namespace kf2::test
