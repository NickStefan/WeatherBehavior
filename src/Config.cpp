#include "Config.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <unordered_set>

#include <Windows.h>

#include <nlohmann/json.hpp>

namespace WeatherBehavior
{
	using json = nlohmann::json;

	namespace
	{
		const std::filesystem::path& PluginFolder()
		{
			static const std::filesystem::path folder = []() -> std::filesystem::path {
				HMODULE handle = nullptr;
				if (::GetModuleHandleExW(
						GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCWSTR>(&PluginFolder), &handle) &&
					handle) {
					wchar_t buffer[MAX_PATH]{};
					const auto len = ::GetModuleFileNameW(handle, buffer, static_cast<DWORD>(std::size(buffer)));
					if (len > 0 && len < std::size(buffer)) {
						auto dir = std::filesystem::path(buffer).parent_path();
						if (!dir.empty()) {
							return dir;
						}
					}
				}
				return std::filesystem::path(L"Data") / L"SKSE" / L"Plugins";
			}();
			return folder;
		}

		std::filesystem::path SettingsPath() { return PluginFolder() / L"WeatherBehavior.json"; }
		std::filesystem::path PresetsFolder() { return PluginFolder() / L"WeatherBehavior"; }
	}

	std::uint32_t MakeRuleID()
	{
		static std::mt19937 rng{ std::random_device{}() };
		std::uint32_t id = 0;
		while (id == 0) {
			id = rng();
		}
		return id;
	}

	bool Rule::EnvMatches(std::uint32_t a_weather, std::uint32_t a_season) const
	{
		if (!enabled) {
			return false;
		}
		if (weatherMask != 0 && (weatherMask & a_weather) == 0) {
			return false;
		}
		if (seasonMask != 0 && (seasonMask & a_season) == 0) {
			return false;
		}
		return true;
	}

	Config& Config::GetSingleton()
	{
		static Config instance;
		return instance;
	}

	static std::vector<std::string> ParseStrings(const json& a_array)
	{
		std::vector<std::string> out;
		if (!a_array.is_array()) {
			return out;
		}
		for (const auto& entry : a_array) {
			if (entry.is_string()) {
				auto s = entry.get<std::string>();
				if (!s.empty()) {
					out.push_back(std::move(s));
				}
			}
		}
		return out;
	}

	static Rule RuleFromJson(const json& a_jr, std::string_view a_preset)
	{
		Rule rule;
		rule.id = a_jr.value("id", 0u);
		if (rule.id == 0) {
			rule.id = MakeRuleID();
		}
		rule.name = a_jr.value("name", std::string{ "Rule" });
		rule.preset = std::string(a_preset);
		rule.enabled = a_jr.value("enabled", true);
		rule.target = static_cast<Target>(a_jr.value("target", 0u));
		rule.chance = std::min<std::uint32_t>(100, a_jr.value("chance", 100u));
		rule.weatherMask = a_jr.value("weatherMask", 0u);
		rule.seasonMask = a_jr.value("seasonMask", 0u);
		if (a_jr.contains("items")) {
			rule.items = ParseStrings(a_jr["items"]);
		}
		return rule;
	}

	static json RuleToJson(const Rule& a_rule)
	{
		json jr;
		jr["id"] = a_rule.id;
		jr["name"] = a_rule.name;
		jr["enabled"] = a_rule.enabled;
		jr["target"] = static_cast<std::uint32_t>(a_rule.target);
		jr["chance"] = a_rule.chance;
		jr["weatherMask"] = a_rule.weatherMask;
		jr["seasonMask"] = a_rule.seasonMask;
		jr["items"] = a_rule.items;
		return jr;
	}

	std::string Config::SanitizeFileName(std::string_view a_name)
	{
		std::string out;
		for (const char c : a_name) {
			switch (c) {
			case '\\':
			case '/':
			case ':':
			case '*':
			case '?':
			case '"':
			case '<':
			case '>':
			case '|':
				out.push_back('_');
				break;
			default:
				if (static_cast<unsigned char>(c) >= 0x20) {
					out.push_back(c);
				}
			}
		}
		while (!out.empty() && (out.front() == ' ')) {
			out.erase(out.begin());
		}
		while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
			out.pop_back();
		}
		return out.empty() ? std::string{ "Preset" } : out;
	}

	void Config::Load()
	{
		std::scoped_lock lock(rulesMutex);

		rules.clear();
		std::unordered_set<std::uint32_t> seen;

		std::ifstream file(SettingsPath());
		if (file.good()) {
			try {
				json root;
				file >> root;
				enabled.store(root.value("enabled", true), std::memory_order_relaxed);
				onlyOutdoors.store(root.value("onlyOutdoors", true), std::memory_order_relaxed);
				pollSeconds.store(std::clamp<std::uint32_t>(root.value("pollSeconds", 5u), 1, 600), std::memory_order_relaxed);

				if (root.contains("rules") && root["rules"].is_array()) {
					for (const auto& jr : root["rules"]) {
						Rule rule = RuleFromJson(jr, jr.value("preset", std::string{ "Default" }));
						if (seen.insert(rule.id).second) {
							rules.push_back(std::move(rule));
						}
					}
				}
			} catch (const std::exception& e) {
				SKSE::log::error("Failed to parse settings: {}", e.what());
			}
		} else {
			SKSE::log::info("No settings file, using defaults");
		}

		std::error_code ec;
		const std::filesystem::path dir = PresetsFolder();
		if (std::filesystem::is_directory(dir, ec)) {
			try {
				for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
					if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
						continue;
					}
					try {
						std::ifstream pf(entry.path());
						if (!pf.good()) {
							continue;
						}
						json proot;
						pf >> proot;

						const json* jrules = nullptr;
						if (proot.is_array()) {
							jrules = &proot;
						} else if (proot.contains("rules") && proot["rules"].is_array()) {
							jrules = &proot["rules"];
						}
						if (!jrules) {
							continue;
						}

						const std::string preset = entry.path().stem().string();
						for (const auto& jr : *jrules) {
							Rule rule = RuleFromJson(jr, preset);
							if (seen.insert(rule.id).second) {
								rules.push_back(std::move(rule));
							}
						}
					} catch (const std::exception& e) {
						SKSE::log::error("Failed to load preset {}: {}", entry.path().filename().string(), e.what());
					}
				}
			} catch (const std::exception& e) {
				SKSE::log::error("Failed to scan presets folder: {}", e.what());
			}
		}

		SKSE::log::info("Loaded {} rule(s)", rules.size());
	}

	void Config::Save()
	{
		std::scoped_lock lock(rulesMutex);

		json root;
		root["enabled"] = enabled.load(std::memory_order_relaxed);
		root["onlyOutdoors"] = onlyOutdoors.load(std::memory_order_relaxed);
		root["pollSeconds"] = pollSeconds.load(std::memory_order_relaxed);
		std::ofstream mainFile(SettingsPath());
		if (mainFile.good()) {
			mainFile << root.dump(2);
		} else {
			SKSE::log::error("Failed to open settings for writing: {}", SettingsPath().string());
		}

		std::error_code ec;
		const std::filesystem::path dir = PresetsFolder();
		std::filesystem::create_directories(dir, ec);
		if (ec) {
			SKSE::log::error("Failed to create presets folder {}: {}", dir.string(), ec.message());
		}

		std::map<std::string, json> presets;
		for (auto& rule : rules) {
			rule.preset = SanitizeFileName(rule.preset);
			auto& arr = presets[rule.preset];
			if (!arr.is_array()) {
				arr = json::array();
			}
			arr.push_back(RuleToJson(rule));
		}

		std::unordered_set<std::string> written;
		for (const auto& [preset, arr] : presets) {
			json proot;
			proot["rules"] = arr;
			const auto path = dir / (preset + ".json");
			std::ofstream pf(path);
			if (pf.good()) {
				pf << proot.dump(2);
				written.insert(preset);
			} else {
				SKSE::log::error("Failed to write preset {}", path.string());
			}
		}
		SKSE::log::info("Saved {} preset file(s) to {}", written.size(), dir.string());

		if (std::filesystem::is_directory(dir, ec)) {
			try {
				for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
					if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
						continue;
					}
					if (!written.contains(entry.path().stem().string())) {
						std::filesystem::remove(entry.path(), ec);
					}
				}
			} catch (const std::exception& e) {
				SKSE::log::error("Failed to prune presets folder: {}", e.what());
			}
		}
	}

	std::string Config::PresetsLocation()
	{
		return PresetsFolder().string();
	}
}
