#include "tools/RegistryTool.h"
#include "common/StringUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

namespace voice_agent {

RegistryTool::RegistryTool(RegistryController& registryController, std::filesystem::path binaryDir)
    : registryController_(registryController), binaryDir_(std::move(binaryDir)) {
    
    skillsDir_ = binaryDir_ / "skills";
    notesDir_ = binaryDir_ / "notes";
    experiencesDir_ = binaryDir_ / "experiences";

    // Ensure directories exist
    std::error_code ec;
    std::filesystem::create_directories(skillsDir_, ec);
    std::filesystem::create_directories(notesDir_, ec);
    std::filesystem::create_directories(experiencesDir_, ec);

    definition_.name = "RegistryTool";
    definition_.description = "Experience, Note ve Skill kaydetmek veya sorgulamak icin kullanilir.";
    definition_.parameters = {
        {"type", "object"},
        {"properties", {
            {"operation", {
                {"type", "string"}, 
                {"enum", {"record_experience", "record_note", "record_skill", "query"}},
                {"description", "Yapilacak islem tipi."}
            }},
            {"action_text", {{"type", "string"}, {"description", "Deneyim eylem metni."}}},
            {"text", {{"type", "string"}, {"description", "Not metni."}}},
            {"result", {{"type", "string"}, {"description", "Deneyim sonucu (opsiyonel)."}}},
            {"name", {{"type", "string"}, {"description", "Skill adi."}}},
            {"description", {{"type", "string"}, {"description", "Skill aciklamasi."}}},
            {"body", {{"type", "string"}, {"description", "Skill icerigi (script/komutlar)."}}},
            {"query", {{"type", "string"}, {"description", "Sorgulanacak konu."}}}
        }},
        {"required", {"operation"}}
    };
}

const ToolDefinition& RegistryTool::Definition() const {
    return definition_;
}

ToolResult RegistryTool::Execute(const ToolCall& call, const CancellationToken* /*token*/) const {
    std::string op = call.arguments.value("operation", "");

    if (op == "record_experience") {
        return RecordExperience(call.arguments);
    } else if (op == "record_note") {
        return RecordNote(call.arguments);
    } else if (op == "record_skill") {
        return RecordSkill(call.arguments);
    } else if (op == "query") {
        return Query(call.arguments);
    }

    return ToolResult{false, false, "Gecersiz operasyon: " + op};
}

ToolResult RegistryTool::RecordExperience(const nlohmann::json& args) const {
    std::string actionText = args.value("action_text", "");
    if (actionText.empty()) {
        actionText = args.value("text", "");
    }
    std::string result = args.value("result", "");
    if (actionText.empty()) return ToolResult{false, false, "Deneyim eylem metni bos olamaz."};

    registryController_.GetExperienceRegistry().AddExperience(actionText, result);

    // Save to file as well
    try {
        std::string filename = "exp_" + std::to_string(std::time(nullptr)) + ".json";
        std::ofstream off(experiencesDir_ / filename);
        nlohmann::json j = {{"action_text", actionText}, {"result", result}, {"timestamp", std::time(nullptr)}};
        off << j.dump(4);
    } catch (...) {}

    return ToolResult{true, false, "Deneyim basariyla kaydedildi."};
}

ToolResult RegistryTool::RecordNote(const nlohmann::json& args) const {
    std::string text = args.value("text", "");
    if (text.empty()) return ToolResult{false, false, "Not metni bos olamaz."};

    registryController_.GetNoteRegistry().AddNote(text);

    // Save to file as well
    try {
        std::string filename = "note_" + std::to_string(std::time(nullptr)) + ".json";
        std::ofstream off(notesDir_ / filename);
        nlohmann::json j = {{"text", text}, {"timestamp", std::time(nullptr)}};
        off << j.dump(4);
    } catch (...) {}

    return ToolResult{true, false, "Not basariyla kaydedildi."};
}

ToolResult RegistryTool::RecordSkill(const nlohmann::json& args) const {
    std::string name = args.value("name", "");
    std::string description = args.value("description", "");
    std::string body = args.value("body", "");

    if (name.empty() || description.empty() || body.empty()) {
        return ToolResult{false, false, "Skill adi, aciklamasi ve icerigi zorunludur."};
    }

    // Sanitize name for filename
    std::string filename = name;
    std::replace(filename.begin(), filename.end(), ' ', '_');
    filename += ".md";

    std::filesystem::path filePath = skillsDir_ / filename;
    nlohmann::json skillJson = {
        {"name", name},
        {"description", description}
    };

    std::ofstream file(filePath);
    if (!file) return ToolResult{false, false, "Skill dosyasi olusturulamadi: " + filePath.string()};
    file << "---\n" << skillJson.dump(4) << "\n---\n" << body;
    file.close();

    registryController_.GetSkillRegistry().AddSkill(filePath.string());
    return ToolResult{true, false, "Skill basariyla kaydedildi ve indekslendi: " + name};
}

ToolResult RegistryTool::Query(const nlohmann::json& args) const {
    std::string queryText = args.value("query", "");
    if (queryText.empty()) return ToolResult{false, false, "Sorgu metni bos olamaz."};

    auto skills = registryController_.GetSkillRegistry().MatchSkills(queryText, 0.5f, 2);
    auto experiences = registryController_.GetExperienceRegistry().MatchExperiences(queryText, 0.5f, 2);
    auto notes = registryController_.GetNoteRegistry().MatchNotes(queryText, 0.5f, 2);

    nlohmann::json result;
    result["skills"] = nlohmann::json::array();
    for (const auto& s : skills) result["skills"].push_back({{"name", s.name}, {"description", s.description}});
    
    result["experiences"] = nlohmann::json::array();
    for (const auto& e : experiences) result["experiences"].push_back({{"action_text", e.actionText}, {"result", e.resultText}});

    result["notes"] = nlohmann::json::array();
    for (const auto& n : notes) result["notes"].push_back({{"text", n.text}});

    return ToolResult{true, false, "Sorgu sonuclari getirildi.", result};
}

} // namespace voice_agent
