#include <vibranceUI/core/file.h>
#include <fstream>

std::vector<unsigned char> read_binary_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return {};
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        return {};
    }
    return bytes;
}

std::string read_text_file(const std::filesystem::path& path)
{
    const std::vector<unsigned char> bytes = read_binary_file(path);
    return std::string(bytes.begin(), bytes.end());
}
