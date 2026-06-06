#include "tools/ProjectFilesTool.h"

#include "common/StringUtils.h"

#include <chrono>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace voice_agent {

namespace {

constexpr std::size_t kMaxNoteLength = 240;

std::filesystem::path ResolveExecutableDir() {
    std::error_code ec;
    const std::filesystem::path executablePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !executablePath.empty()) {
        return executablePath.parent_path();
    }
    return std::filesystem::current_path();
}

bool IsBareFilename(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_parent_path()) {
        return false;
    }

    const std::filesystem::path normalized = path.lexically_normal();
    return !normalized.empty() &&
           !normalized.has_parent_path() &&
           normalized != "." &&
           normalized != "..";
}

std::string TodayStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d");
    return out.str();
}

std::string CollapseWhitespace(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    bool prevSpace = false;
    for (const char c : input) {
        const char ch = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        if (ch == ' ') {
            if (!prevSpace && !out.empty()) {
                out.push_back(' ');
            }
            prevSpace = true;
        } else {
            out.push_back(ch);
            prevSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string RenderTags(const nlohmann::json& tagsJson) {
    if (!tagsJson.is_array() || tagsJson.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const auto& tag : tagsJson) {
        if (!tag.is_string()) {
            continue;
        }
        const std::string trimmed = Trim(tag.get<std::string>());
        if (trimmed.empty()) {
            continue;
        }
        if (!first) {
            out << ",";
        }
        out << trimmed;
        first = false;
    }
    out << "]";
    const std::string result = out.str();
    return result == "[]" ? std::string{} : result;
}

std::deque<std::string> ReadExistingLines(const std::filesystem::path& path) {
    std::deque<std::string> lines;
    std::ifstream file(path);
    if (!file) {
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::filesystem::path CanonicalBestEffort(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    return std::filesystem::absolute(path).lexically_normal();
}

bool IsPathWithin(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const std::filesystem::path normalizedChild = CanonicalBestEffort(child);
    const std::filesystem::path normalizedParent = CanonicalBestEffort(parent);
    auto childIt = normalizedChild.begin();
    auto parentIt = normalizedParent.begin();
    for (; parentIt != normalizedParent.end(); ++parentIt, ++childIt) {
        if (childIt == normalizedChild.end() || *childIt != *parentIt) {
            return false;
        }
    }
    return true;
}

bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& content) {
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << content;
        out.flush();
        if (!out) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (!ec) {
        return true;
    }

    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool WriteLinesAtomic(const std::filesystem::path& path, const std::deque<std::string>& lines) {
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        for (const auto& line : lines) {
            out << line << "\n";
        }
        out.flush();
        if (!out) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool ReadTextFile(const std::filesystem::path& path, std::string* content) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::ostringstream out;
    out << file.rdbuf();
    if (!file.good() && !file.eof()) {
        return false;
    }

    *content = out.str();
    return true;
}

std::size_t ReplaceAllInPlace(
    std::string* content,
    const std::string& needle,
    const std::string& replacement
) {
    if (content == nullptr || needle.empty()) {
        return 0;
    }

    std::size_t replacements = 0;
    std::size_t pos = 0;
    while ((pos = content->find(needle, pos)) != std::string::npos) {
        content->replace(pos, needle.size(), replacement);
        pos += replacement.size();
        ++replacements;
    }
    return replacements;
}

std::deque<std::string> SplitLines(const std::string& content) {
    std::deque<std::string> lines;
    if (content.empty()) {
        return lines;
    }

    std::stringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string JoinLines(const std::deque<std::string>& lines, std::size_t startIndex, std::size_t endIndexExclusive) {
    std::ostringstream out;
    for (std::size_t index = startIndex; index < endIndexExclusive; ++index) {
        out << lines[index] << "\n";
    }
    return out.str();
}

}  // namespace

ProjectFilesTool::ProjectFilesTool(std::filesystem::path scriptsRoot)
    : scriptsRoot_(ResolveExecutableDir() / "scripts") {
    (void)scriptsRoot;
    definition_.name = "ProjectFilesTool";
    definition_.description =
        "Kalici script dosyasi islemlerini sadece binary dizini altindaki scripts/ konumuna yapar. Model hedef dizin secemez; path alanina yalnizca tek bir dosya adi verilir. operation=write yeni icerik yazar, operation=read tum dosyayi veya istenen satir araligini okur, operation=replace ayni sabit dosyada metin ya da satir araligi degistirir, operation=insert ise istenen satirdan once yeni icerik ekler.";
    definition_.parameters = {
        {"type", "object"},
        {"properties", {
            {"operation", {{"type", "string"}, {"enum", nlohmann::json::array({"write", "read", "replace", "insert", "edit"})}, {"description", "Varsayilan write. read tum dosyayi veya satir araligini dondurur; replace/edit mevcut dosyada bul-degistir veya satir araligi degisikligi yapar; insert belirtilen satirdan once icerik ekler."}}},
            {"path", {{"type", "string"}, {"description", "Sadece scripts/ icindeki tek dosya adi. Klasor, /, \\, ../ kullanma."}}},
            {"content", {{"type", "string"}, {"description", "operation=write/replace/insert icin kullanilacak icerik."}}},
            {"findText", {{"type", "string"}, {"description", "operation=replace icin dosyada bulunup degistirilecek metin."}}},
            {"replaceWith", {{"type", "string"}, {"description", "operation=replace icin findText yerine yazilacak metin."}}},
            {"startLine", {{"type", "integer"}, {"description", "operation=read veya replace/edit icin 1-based baslangic satiri."}}},
            {"endLine", {{"type", "integer"}, {"description", "operation=read veya replace/edit icin 1-based bitis satiri. Verilmezse startLine kullanilir."}}},
            {"insertLine", {{"type", "integer"}, {"description", "operation=insert icin 1-based satir numarasi. Verilen satirdan once ekler; totalLines+1 ise sona ekler."}}},
            {"overwrite", {{"type", "boolean"}, {"description", "false ise dosya varsa hata ver."}}}
        }},
        {"required", nlohmann::json::array({"path"})}
    };
    definition_.aliases = {
        "project.file.write",
        "project.file.read",
        "project.file.edit",
        "scripts.write",
        "scripts.read"
    };
    definition_.riskLevel = ToolRiskLevel::Dangerous;
}

const ToolDefinition& ProjectFilesTool::Definition() const {
    return definition_;
}

ToolResult ProjectFilesTool::Execute(const ToolCall& call, const CancellationToken*) const {
    std::string operation = ToLower(Trim(call.arguments.value("operation", "write")));
    std::string relativePath = Trim(call.arguments.value("path", ""));
    const bool overwrite = call.arguments.value("overwrite", true);

    if (operation == "edit") {
        operation = "replace";
    }
    if (operation != "write" && operation != "read" && operation != "replace" && operation != "insert") {
        return ToolResult{false, false, "operation sadece write, read, replace, edit veya insert olabilir.", {{"reason", "invalid_operation"}}};
    }
    if (relativePath.empty()) {
        return ToolResult{false, false, "path bos olamaz.", {{"reason", "missing_path"}}};
    }

    auto buildReadResult = [&](const std::filesystem::path& path) -> ToolResult {
        const bool hasStartLine = call.arguments.contains("startLine") && call.arguments.at("startLine").is_number_integer();
        const bool hasEndLine = call.arguments.contains("endLine") && call.arguments.at("endLine").is_number_integer();
        if (!hasStartLine && !hasEndLine) {
            std::string content;
            if (!ReadTextFile(path, &content)) {
                return ToolResult{false, false, "Dosya okunamadi.", {{"reason", "read_failed"}, {"path", path.string()}}};
            }
            return ToolResult{
                true,
                false,
                "Dosya okundu.",
                {{"target", "script"}, {"path", path.string()}, {"content", content}, {"bytes", static_cast<int>(content.size())}}
            };
        }

        const std::deque<std::string> lines = ReadExistingLines(path);
        const int totalLines = static_cast<int>(lines.size());
        if (totalLines == 0) {
            return ToolResult{false, false, "Dosya bos veya okunamadi.", {{"reason", "empty_file"}, {"path", path.string()}}};
        }

        const int startLine = hasStartLine ? call.arguments.at("startLine").get<int>() : 1;
        const int endLine = hasEndLine ? call.arguments.at("endLine").get<int>() : startLine;
        if (startLine < 1 || endLine < startLine || endLine > totalLines) {
            return ToolResult{false, false, "Gecersiz satir araligi.", {{"reason", "invalid_line_range"}, {"path", path.string()}, {"totalLines", totalLines}}};
        }

        const std::string content = JoinLines(lines, static_cast<std::size_t>(startLine - 1), static_cast<std::size_t>(endLine));
        return ToolResult{
            true,
            false,
            "Dosya bolumu okundu.",
            {{"target", "script"}, {"path", path.string()}, {"content", content}, {"bytes", static_cast<int>(content.size())}, {"startLine", startLine}, {"endLine", endLine}, {"totalLines", totalLines}}
        };
    };

    auto buildReplaceResult = [&](const std::filesystem::path& path) -> ToolResult {
        const bool hasStartLine = call.arguments.contains("startLine") && call.arguments.at("startLine").is_number_integer();
        const bool hasEndLine = call.arguments.contains("endLine") && call.arguments.at("endLine").is_number_integer();
        if (hasStartLine || hasEndLine) {
            const std::deque<std::string> fileLines = ReadExistingLines(path);
            const int totalLines = static_cast<int>(fileLines.size());
            const int startLine = hasStartLine ? call.arguments.at("startLine").get<int>() : 1;
            const int endLine = hasEndLine ? call.arguments.at("endLine").get<int>() : startLine;
            if (startLine < 1 || endLine < startLine || endLine > totalLines) {
                return ToolResult{false, false, "Gecersiz satir araligi.", {{"reason", "invalid_line_range"}, {"path", path.string()}, {"totalLines", totalLines}}};
            }

            const std::string replacementContent = call.arguments.value("content", std::string{});
            std::deque<std::string> updatedLines = fileLines;
            const std::size_t startIndex = static_cast<std::size_t>(startLine - 1);
            const std::size_t eraseCount = static_cast<std::size_t>(endLine - startLine + 1);
            auto eraseBegin = updatedLines.begin() + static_cast<std::ptrdiff_t>(startIndex);
            auto eraseEnd = eraseBegin + static_cast<std::ptrdiff_t>(eraseCount);
            eraseBegin = updatedLines.erase(eraseBegin, eraseEnd);

            const std::deque<std::string> newLines = SplitLines(replacementContent);
            updatedLines.insert(eraseBegin, newLines.begin(), newLines.end());

            if (!WriteLinesAtomic(path, updatedLines)) {
                return ToolResult{false, false, "Dosya guncellenemedi.", {{"reason", "write_failed"}, {"path", path.string()}}};
            }

            nlohmann::json output = {
                {"target", "script"},
                {"path", path.string()},
                {"replacedStartLine", startLine},
                {"replacedEndLine", endLine},
                {"insertedLines", static_cast<int>(newLines.size())},
                {"totalLines", static_cast<int>(updatedLines.size())}
            };

            return ToolResult{true, false, "Dosya bolumu guncellendi.", output};
        }

        const std::string findText = call.arguments.value("findText", std::string{});
        if (findText.empty()) {
            return ToolResult{false, false, "replace icin findText veya startLine gerekli.", {{"reason", "missing_find_text"}}};
        }
        const std::string replaceWith = call.arguments.value("replaceWith", std::string{});

        std::string currentContent;
        if (!ReadTextFile(path, &currentContent)) {
            return ToolResult{false, false, "Duzenlenecek dosya okunamadi.", {{"reason", "read_failed"}, {"path", path.string()}}};
        }

        const std::size_t replacements = ReplaceAllInPlace(&currentContent, findText, replaceWith);
        if (replacements == 0) {
            return ToolResult{false, false, "findText dosyada bulunamadi.", {{"reason", "find_text_not_found"}, {"path", path.string()}}};
        }

        if (!WriteTextFileAtomic(path, currentContent)) {
            return ToolResult{false, false, "Dosya guncellenemedi.", {{"reason", "write_failed"}, {"path", path.string()}}};
        }

        nlohmann::json output = {
            {"target", "script"},
            {"path", path.string()},
            {"bytes", static_cast<int>(currentContent.size())},
            {"replacements", static_cast<int>(replacements)}
        };

        return ToolResult{true, false, "Dosya guncellendi.", output};
    };

    auto buildInsertResult = [&](const std::filesystem::path& path) -> ToolResult {
        if (!call.arguments.contains("content") || !call.arguments.at("content").is_string()) {
            return ToolResult{false, false, "insert icin content gerekli.", {{"reason", "missing_content"}}};
        }

        const std::string content = call.arguments.at("content").get<std::string>();
        const std::deque<std::string> newLines = SplitLines(content);
        if (newLines.empty()) {
            return ToolResult{false, false, "insert icin content bos olamaz.", {{"reason", "empty_content"}}};
        }

        std::deque<std::string> existingLines;
        if (std::filesystem::exists(path)) {
            existingLines = ReadExistingLines(path);
        }

        const int totalLines = static_cast<int>(existingLines.size());
        const int insertLine = call.arguments.value("insertLine", totalLines + 1);
        if (insertLine < 1 || insertLine > totalLines + 1) {
            return ToolResult{false, false, "Gecersiz insertLine degeri.", {{"reason", "invalid_insert_line"}, {"path", path.string()}, {"totalLines", totalLines}}};
        }

        auto insertIt = existingLines.begin() + static_cast<std::ptrdiff_t>(insertLine - 1);
        existingLines.insert(insertIt, newLines.begin(), newLines.end());

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return ToolResult{false, false, "Hedef klasor olusturulamadi.", {{"reason", "create_parent_failed"}, {"details", ec.message()}, {"path", path.parent_path().string()}}};
        }

        if (!WriteLinesAtomic(path, existingLines)) {
            return ToolResult{false, false, "Dosya guncellenemedi.", {{"reason", "write_failed"}, {"path", path.string()}}};
        }

        nlohmann::json output = {
            {"target", "script"},
            {"path", path.string()},
            {"insertLine", insertLine},
            {"insertedLines", static_cast<int>(newLines.size())},
            {"totalLines", static_cast<int>(existingLines.size())}
        };

        return ToolResult{true, false, "Dosyaya yeni satirlar eklendi.", output};
    };

    const std::filesystem::path root = scriptsRoot_;
    if (root.empty()) {
        return ToolResult{false, false, "Hedef kok klasor yapilandirilmamis.", {{"reason", "missing_root"}}};
    }

    std::filesystem::path relative(relativePath);
    if (relative.is_absolute()) {
        return ToolResult{false, true, "path relative olmalidir.", {{"reason", "absolute_path_not_allowed"}, {"path", relativePath}}};
    }
    if (!IsBareFilename(relative)) {
        return ToolResult{
            false,
            true,
            "path sadece tek bir dosya adi olabilir; klasor veya ust dizin secilemez.",
            {{"reason", "directory_selection_not_allowed"}, {"path", relativePath}}
        };
    }

    const std::filesystem::path resolved = CanonicalBestEffort(root / relative);
    if (!IsPathWithin(resolved, root)) {
        return ToolResult{
            false,
            true,
            "Dosya allowlist klasoru disinda oldugu icin islem yapilmadi.",
            {{"reason", "path_outside_allowlist"}, {"path", resolved.string()}, {"allowedRoot", root.string()}}
        };
    }

    if (operation == "read") {
        return buildReadResult(resolved);
    }
    if (operation == "replace") {
        return buildReplaceResult(resolved);
    }
    if (operation == "insert") {
        return buildInsertResult(resolved);
    }

    std::string content;
    if (call.arguments.contains("content") && call.arguments.at("content").is_string()) {
        content = call.arguments.at("content").get<std::string>();
    }

    if (content.empty()) {
        return ToolResult{false, false, "Yazilacak icerik bos olamaz.", {{"reason", "empty_content"}}};
    }

    if (!overwrite && std::filesystem::exists(resolved)) {
        return ToolResult{false, false, "Dosya zaten var ve overwrite=false.", {{"reason", "file_exists"}, {"path", resolved.string()}}};
    }

    std::error_code ec;
    std::filesystem::create_directories(resolved.parent_path(), ec);
    if (ec) {
        return ToolResult{false, false, "Hedef klasor olusturulamadi.", {{"reason", "create_parent_failed"}, {"details", ec.message()}, {"path", resolved.parent_path().string()}}};
    }

    if (!WriteTextFileAtomic(resolved, content)) {
        return ToolResult{false, false, "Dosya yazilamadi.", {{"reason", "write_failed"}, {"path", resolved.string()}}};
    }

    nlohmann::json output = {
        {"target", "script"},
        {"path", resolved.string()},
        {"bytes", static_cast<int>(content.size())}
    };

    return ToolResult{true, false, "Dosya kaydedildi.", output};
}

}  // namespace voice_agent