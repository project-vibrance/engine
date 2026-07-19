#include <vibranceUI/core/file.h>
#include <vibranceUI/core/logger.h>
#include <fstream>
#include <sstream>

std::vector<char> read_file(const char* fileName)
{
    // Read whole files into memory because asset decoders expect contiguous data
    Logger* logger = Logger::fetch_logger();

    std::ifstream file(fileName, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        std::stringstream lineBuilder;
        lineBuilder << "Failed to load \"" << fileName << "\"" << std::endl;
        std::string line = lineBuilder.str();
        logger->print(line);
        return std::vector<char>();
    }

    size_t fileSize{ static_cast<size_t>(file.tellg()) };

    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();
    return buffer;
}
