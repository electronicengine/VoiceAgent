#include "tools/RememberTool.h"

#include "common/StringUtils.h"

#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace voice_agent {

namespace {

constexpr std::size_t kMaxNoteLength = 240;

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

bool WriteAtomic(const std::filesystem::path& path, const std::deque<std::string>& lines) {
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

}  // namespace

RememberTool::RememberTool(std::string experiencesFilePath, int maxLines)
    : experiencesFilePath_(std::move(experiencesFilePath)),
      maxLines_(maxLines > 0 ? maxLines : 100) {
    definition_ = ToolDefinition{
        "RememberTool",
        "Bir gorevde ise yaramis SOMUT, yeniden kullanilabilir bir ders ogrendiyseniz "
        "kalici not kaydedin. Tek satir, eyleme donuk, en fazla 240 karakter. Genel "
        "veya bariz bilgileri ASLA kaydetmeyin (orn. 'Linux iyi bir OS'). Iyi ornek: "
        "'mpv yt-dlp icin --script-opts=ytdl_hook-ytdl_path=... gerekiyor; sistem "
        "yt-dlp'si eski'.",
        {
            {"type", "object"},
            {"properties", {
                {"note", {{"type", "string"}}},
                {"tags", {{"type", "array"}, {"items", {{"type", "string"}}}}}
            }},
            {"required", nlohmann::json::array({"note"})}
        },
        {},
        ToolRiskLevel::Safe
    };
}

const ToolDefinition& RememberTool::Definition() const {
    return definition_;
}

ToolResult RememberTool::Execute(const ToolCall& call, const CancellationToken*) const {
    ToolResult result;
    if (experiencesFilePath_.empty()) {
        result.summary = "Experiences dosya yolu yapilandirilmamis.";
        result.output = {{"reason", "no_path"}};
        return result;
    }

    std::string note;
    if (const auto it = call.arguments.find("note"); it != call.arguments.end() && it->is_string()) {
        note = it->get<std::string>();
    }
    note = CollapseWhitespace(Trim(note));
    if (note.empty()) {
        result.summary = "Note bos olamaz.";
        result.output = {{"reason", "empty_note"}};
        return result;
    }
    if (note.size() > kMaxNoteLength) {
        note.resize(kMaxNoteLength);
    }

    std::string tagsRendered;
    if (const auto it = call.arguments.find("tags"); it != call.arguments.end()) {
        tagsRendered = RenderTags(*it);
    }

    std::ostringstream lineBuilder;
    lineBuilder << "[" << TodayStamp() << "]";
    if (!tagsRendered.empty()) {
        lineBuilder << " " << tagsRendered;
    }
    lineBuilder << " " << note;
    const std::string newLine = lineBuilder.str();

    const std::filesystem::path path(experiencesFilePath_);
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    auto lines = ReadExistingLines(path);
    // Drop trailing empty lines so the file stays compact.
    while (!lines.empty() && Trim(lines.back()).empty()) {
        lines.pop_back();
    }
    lines.push_back(newLine);
    while (static_cast<int>(lines.size()) > maxLines_) {
        lines.pop_front();
    }

    if (!WriteAtomic(path, lines)) {
        result.summary = "experiences.md yazilamadi.";
        result.output = {{"reason", "write_failed"}, {"path", experiencesFilePath_}};
        return result;
    }

    result.succeeded = true;
    result.summary = "Not kaydedildi.";
    result.output = {
        {"path", experiencesFilePath_},
        {"line", newLine},
        {"totalLines", static_cast<int>(lines.size())}
    };
    return result;
}

}  // namespace voice_agent
