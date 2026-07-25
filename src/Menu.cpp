#include "Menu.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <mutex>
#include <unordered_set>

#include <Windows.h>

#include "Config.h"
#include "Manager.h"
#include "Util.h"

#include "SKSEMenuFramework.h"

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImVec4;

namespace WeatherBehavior
{
	namespace
	{
		constexpr int    kMaxShown = 300;
		constexpr double kStatusSeconds = 6.0;

		constexpr ImVec4 kOkColor{ 0.45f, 0.85f, 0.45f, 1.0f };
		constexpr ImVec4 kWarnColor{ 0.95f, 0.75f, 0.35f, 1.0f };
		constexpr ImVec4 kErrorColor{ 0.90f, 0.40f, 0.40f, 1.0f };

		char gItemSearch[128]{};
		bool gInventoryOnly{ false };
		bool gDirty{ false };

		std::string gStatus;
		ImVec4      gStatusColor{ kOkColor };
		double      gStatusTime{ -kStatusSeconds };

		char        gRenameBuf[128]{};
		std::string gRenaming;

		void SetStatus(std::string a_text, const ImVec4& a_color)
		{
			gStatus = std::move(a_text);
			gStatusColor = a_color;
			gStatusTime = ImGui::GetTime();
		}

		void CopyToBuf(char* a_buf, std::size_t a_size, std::string_view a_text)
		{
			const auto len = std::min(a_text.size(), a_size - 1);
			std::memcpy(a_buf, a_text.data(), len);
			a_buf[len] = '\0';
		}

		std::string ToLower(std::string_view a_text)
		{
			std::string out(a_text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
			return out;
		}

		using GetFormEditorIDFunc = const char* (*)(std::uint32_t);

		GetFormEditorIDFunc Po3GetEditorID()
		{
			static const auto func = []() -> GetFormEditorIDFunc {
				if (const auto tweaks = ::GetModuleHandleW(L"po3_Tweaks")) {
					return reinterpret_cast<GetFormEditorIDFunc>(::GetProcAddress(tweaks, "GetFormEditorID"));
				}
				return nullptr;
			}();
			return func;
		}

		const char* GetEditorID(const RE::TESForm* a_form)
		{
			if (const auto edid = a_form->GetFormEditorID(); edid && edid[0]) {
				return edid;
			}
			if (const auto func = Po3GetEditorID()) {
				return func(a_form->GetFormID());
			}
			return nullptr;
		}

		bool EditString(const char* a_label, std::string& a_value)
		{
			char buf[256];
			CopyToBuf(buf, sizeof(buf), a_value);
			if (ImGui::InputText(a_label, buf, sizeof(buf))) {
				a_value = buf;
				return true;
			}
			return false;
		}

		bool FlagCheckbox(const char* a_label, std::uint32_t& a_mask, std::uint32_t a_bit)
		{
			bool on = (a_mask & a_bit) != 0;
			if (ImGui::Checkbox(a_label, &on)) {
				if (on) {
					a_mask |= a_bit;
				} else {
					a_mask &= ~a_bit;
				}
				return true;
			}
			return false;
		}

		bool ContainsString(const std::vector<std::string>& a_list, std::string_view a_value)
		{
			return std::find(a_list.begin(), a_list.end(), a_value) != a_list.end();
		}

		bool RenderItemList(std::vector<std::string>& a_list)
		{
			bool changed = false;
			for (int i = 0; i < static_cast<int>(a_list.size()); ++i) {
				ImGui::PushID(i);
				if (ImGui::SmallButton("X")) {
					a_list.erase(a_list.begin() + i);
					changed = true;
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
				ImGui::SameLine();
				const auto form = RE::TESForm::LookupByEditorID<RE::TESObjectARMO>(a_list[i]);
				if (form && form->GetName() && form->GetName()[0]) {
					ImGui::Text("%s  [%s]", form->GetName(), a_list[i].c_str());
				} else {
					ImGui::TextUnformatted(a_list[i].c_str());
				}
			}
			return changed;
		}

		bool RenderItemPicker(std::vector<std::string>& a_items)
		{
			bool changed = false;
			if (!Po3GetEditorID()) {
				ImGui::TextWrapped("powerofthree's Tweaks is required to list clothing here.");
			}
			ImGui::Checkbox("Player inventory only", &gInventoryOnly);
			ImGui::InputTextWithHint("##itemsearch", "Search clothing/armor...", gItemSearch, sizeof(gItemSearch));

			const auto handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return changed;
			}

			std::unordered_set<RE::FormID> playerItems;
			if (gInventoryOnly) {
				if (const auto player = RE::PlayerCharacter::GetSingleton()) {
					for (const auto& [obj, count] : player->GetInventoryCounts()) {
						if (obj && count > 0) {
							playerItems.insert(obj->GetFormID());
						}
					}
				}
			}

			const std::string needle = ToLower(gItemSearch);

			if (ImGui::BeginChild("##itemlist", ImVec2(0, 200), true)) {
				int shown = 0;
				for (const auto armor : handler->GetFormArray<RE::TESObjectARMO>()) {
					if (!armor || armor->IsShield()) {
						continue;
					}
					if (gInventoryOnly && !playerItems.contains(armor->GetFormID())) {
						continue;
					}
					const auto edid = GetEditorID(armor);
					if (!edid || !edid[0]) {
						continue;
					}
					const auto name = armor->GetName();
					if (!name || !name[0]) {
						continue;
					}
					if (!needle.empty() && ToLower(name).find(needle) == std::string::npos &&
						ToLower(edid).find(needle) == std::string::npos) {
						continue;
					}
					if (shown++ >= kMaxShown) {
						ImGui::TextDisabled("...refine search to see more");
						break;
					}

					ImGui::PushID(static_cast<int>(armor->GetFormID()));
					if (ContainsString(a_items, edid)) {
						ImGui::BeginDisabled();
						ImGui::SmallButton("Added");
						ImGui::EndDisabled();
					} else if (ImGui::SmallButton("Add")) {
						a_items.emplace_back(edid);
						changed = true;
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGui::Text("%s  [%s]", name, edid);
				}
			}
			ImGui::EndChild();
			return changed;
		}

		void RenderRule(Rule& a_rule, bool& a_deleteRequested)
		{
			ImGui::PushID(static_cast<int>(a_rule.uid));

			const std::string header =
				std::format("{}###rulehdr", a_rule.name.empty() ? "(unnamed)" : a_rule.name);
			if (ImGui::CollapsingHeader(header.c_str())) {
				ImGui::Indent();

				gDirty |= ImGui::Checkbox("Enabled", &a_rule.enabled);
				gDirty |= EditString("Name", a_rule.name);

				gDirty |= EditString("Preset", a_rule.preset);
				ImGui::SameLine();
				ImGui::TextDisabled("(saved as %s.json)", Config::SanitizeFileName(a_rule.preset).c_str());

				int target = static_cast<int>(a_rule.target);
				const char* targets[] = { "All NPCs", "Followers only" };
				if (ImGui::Combo("Applies to", &target, targets, 2)) {
					a_rule.target = static_cast<Target>(target);
					gDirty = true;
				}

				int chance = static_cast<int>(a_rule.chance);
				if (ImGui::SliderInt("Chance per NPC (%)", &chance, 0, 100)) {
					a_rule.chance = static_cast<std::uint32_t>(chance);
					gDirty = true;
				}

				ImGui::SeparatorText("Weather (none checked = any)");
				for (std::uint32_t i = 0; i < kWeatherNames.size(); ++i) {
					gDirty |= FlagCheckbox(kWeatherNames[i], a_rule.weatherMask, 1u << i);
					if (i + 1 < kWeatherNames.size()) {
						ImGui::SameLine();
					}
				}

				ImGui::SeparatorText("Season (none checked = any)");
				for (std::uint32_t i = 0; i < kSeasonNames.size(); ++i) {
					gDirty |= FlagCheckbox(kSeasonNames[i], a_rule.seasonMask, 1u << i);
					if (i + 1 < kSeasonNames.size()) {
						ImGui::SameLine();
					}
				}

				ImGui::Spacing();
				if (ImGui::TreeNode(std::format("Items - one is picked per NPC ({})###items", a_rule.items.size()).c_str())) {
					gDirty |= RenderItemList(a_rule.items);
					gDirty |= RenderItemPicker(a_rule.items);
					ImGui::TreePop();
				}

				ImGui::Spacing();
				ImGui::PushStyleColor(ImGui::ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
				if (ImGui::Button("Delete rule")) {
					a_deleteRequested = true;
				}
				ImGui::PopStyleColor();

				ImGui::Unindent();
			}

			ImGui::PopID();
		}

		void __stdcall RenderConfig()
		{
			auto& config = Config::GetSingleton();

			bool enabled = config.enabled.load(std::memory_order_relaxed);
			if (ImGui::Checkbox("Enabled", &enabled)) {
				config.enabled.store(enabled, std::memory_order_relaxed);
				gDirty = true;
			}
			ImGui::SameLine();
			bool outdoors = config.onlyOutdoors.load(std::memory_order_relaxed);
			if (ImGui::Checkbox("Outdoors only", &outdoors)) {
				config.onlyOutdoors.store(outdoors, std::memory_order_relaxed);
				gDirty = true;
			}
			ImGui::SameLine();
			if (ImGui::Button(gDirty ? "Save *" : "Save")) {
				const auto result = config.Save();
				Manager::GetSingleton().RequestApply();
				gDirty = !result.ok;
				if (result.ok) {
					SetStatus(std::format("Saved {} rule(s) to {} preset file(s).", result.rules, result.presets),
						kOkColor);
				} else {
					SetStatus("Save failed - see WeatherBehavior.log.", kErrorColor);
				}
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Writes the settings and preset files to\n%s", Config::PresetsLocation().c_str());
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload")) {
				const auto count = config.Load();
				Manager::GetSingleton().RequestApply();
				gDirty = false;
				gRenaming.clear();
				SetStatus(std::format("Reloaded {} rule(s) from disk.", count), kOkColor);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Discards unsaved changes and re-reads the files from disk.");
			}

			if (ImGui::GetTime() - gStatusTime < kStatusSeconds && !gStatus.empty()) {
				ImGui::SameLine();
				ImGui::TextColored(gStatusColor, "%s", gStatus.c_str());
			} else if (gDirty) {
				ImGui::SameLine();
				ImGui::TextColored(kWarnColor, "Unsaved changes");
			}

			ImGui::TextDisabled("Changes apply in game within a few seconds. Save writes them to disk.");

			ImGui::SeparatorText("Rules");

			std::scoped_lock lock(config.rulesMutex);

			if (ImGui::Button("+ Add rule")) {
				config.rules.emplace_back();
				gDirty = true;
			}
			ImGui::Spacing();

			int deleteIndex = -1;
			for (int i = 0; i < static_cast<int>(config.rules.size()); ++i) {
				bool deleteRequested = false;
				RenderRule(config.rules[i], deleteRequested);
				if (deleteRequested) {
					deleteIndex = i;
				}
			}
			if (deleteIndex >= 0) {
				config.rules.erase(config.rules.begin() + deleteIndex);
				gDirty = true;
			}

			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Presets")) {
				ImGui::TextWrapped("Rules are saved as preset .json files, one per preset name. Renaming a preset "
				                   "renames its file on the next Save. Share a file with others, or drop one into "
				                   "the folder below and press Reload.");
				ImGui::TextDisabled("%s", Config::PresetsLocation().c_str());
				ImGui::Spacing();

				std::map<std::string, int> counts;
				for (const auto& r : config.rules) {
					counts[Config::SanitizeFileName(r.preset)]++;
				}

				for (const auto& [name, count] : counts) {
					ImGui::PushID(name.c_str());
					ImGui::BulletText("%s.json  -  %d rule(s)", name.c_str(), count);
					if (gRenaming != name) {
						ImGui::SameLine();
						if (ImGui::SmallButton("Rename")) {
							gRenaming = name;
							CopyToBuf(gRenameBuf, sizeof(gRenameBuf), name);
						}
					} else {
						ImGui::Indent();
						ImGui::SetNextItemWidth(220.0f);
						ImGui::InputText("##rename", gRenameBuf, sizeof(gRenameBuf));
						const std::string target = Config::SanitizeFileName(gRenameBuf);
						const bool       merges = target != name && counts.contains(target);

						ImGui::SameLine();
						if (ImGui::Button("Apply")) {
							if (target != name) {
								for (auto& r : config.rules) {
									if (Config::SanitizeFileName(r.preset) == name) {
										r.preset = target;
									}
								}
								gDirty = true;
								SetStatus(std::format("Preset renamed to {}.json - press Save to write it.", target),
									kWarnColor);
							}
							gRenaming.clear();
						}
						ImGui::SameLine();
						if (ImGui::Button("Cancel")) {
							gRenaming.clear();
						}

						if (!gRenaming.empty()) {
							ImGui::TextDisabled("Saves as %s.json", target.c_str());
							if (merges) {
								ImGui::TextColored(kWarnColor, "%s.json already exists - the rules will be merged.",
									target.c_str());
							}
						}
						ImGui::Unindent();
					}
					ImGui::PopID();
				}
			}
		}
	}

	void Menu::Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSEMenuFramework not installed; configuration menu unavailable");
			return;
		}
		SKSEMenuFramework::SetSection("Weather Behavior");
		SKSEMenuFramework::AddSectionItem("Configuration", RenderConfig);
		SKSE::log::info("Registered configuration menu");
	}
}
