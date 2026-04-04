#include "ErrorFile.h"

#include <stdexcept>

namespace imagestore {

ErrorFile::ErrorFile(const std::filesystem::path& path) {
    // Read existing contents into the skip set
    {
        std::ifstream in(path);
        if (in) {
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty())
                    m_known.insert(line);
            }
        }
    }
    m_initialCount = m_known.size();

    // Open in append mode for writing new errors
    m_out.open(path, std::ios::app);
    if (!m_out)
        throw std::runtime_error("Cannot open error file for writing: " + path.string());
}

bool ErrorFile::shouldSkip(const std::string& absPath) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_known.count(absPath) > 0;
}

void ErrorFile::recordError(const std::string& absPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_known.count(absPath) > 0) return;
    m_known.insert(absPath);
    m_out << absPath << '\n';
    m_out.flush();
    ++m_errorCount;
}

size_t ErrorFile::errorCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_errorCount;
}

} // namespace imagestore
