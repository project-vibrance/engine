#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

namespace vibrance::win32
{
class PublicReadSecurity final
{
public:
    PublicReadSecurity()
    {
        // The creator retains full control; another account in this session
        // (DWM or the app) may only read/synchronise the shared object.
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GR;;;WD)(A;;GA;;;OW)",
                SDDL_REVISION_1, &descriptor_, nullptr))
        {
            attributes_.nLength = sizeof(attributes_);
            attributes_.lpSecurityDescriptor = descriptor_;
            attributes_.bInheritHandle = FALSE;
        }
    }

    ~PublicReadSecurity()
    {
        if (descriptor_)
        {
            LocalFree(descriptor_);
        }
    }

    PublicReadSecurity(const PublicReadSecurity&) = delete;
    PublicReadSecurity& operator=(const PublicReadSecurity&) = delete;

    SECURITY_ATTRIBUTES* get() noexcept
    {
        return descriptor_ ? &attributes_ : nullptr;
    }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_ {};
};

class PublicSynchroniseSecurity final
{
public:
    PublicSynchroniseSecurity()
    {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;0x00100000;;;WD)(A;;GA;;;OW)",
                SDDL_REVISION_1, &descriptor_, nullptr))
        {
            attributes_.nLength = sizeof(attributes_);
            attributes_.lpSecurityDescriptor = descriptor_;
        }
    }

    ~PublicSynchroniseSecurity()
    {
        if (descriptor_)
        {
            LocalFree(descriptor_);
        }
    }

    PublicSynchroniseSecurity(const PublicSynchroniseSecurity&) = delete;
    PublicSynchroniseSecurity& operator=(const PublicSynchroniseSecurity&) = delete;

    SECURITY_ATTRIBUTES* get() noexcept
    {
        return descriptor_ ? &attributes_ : nullptr;
    }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_ {};
};
}
