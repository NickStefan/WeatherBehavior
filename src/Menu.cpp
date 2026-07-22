#include "Menu.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <unordered_set>

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
		constexpr std::uint32_t kWearableSlots =
			(1u << 0) | (1u << 1) | (1u << 2) | (1u << 11) | (1u << 12) | (1u << 13) |
			(1u << 16) | (1u << 17) | (1u << 26) | (1u << 27);

		constexpr int kMaxShown = 300;

		char gItemSearch[128]{};
		char gRegionSearch[128]{};
		char gRaceSearch[128]{};
		bool gWearableOnly{ true };
		bool gInventoryOnly{ false };

		std::string ToLower(std::string_view a_text)
		{
			std::string out(a_text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
			return out;
		}

		std::string DisplayName(const RE::TESForm* a_form)
		{
			if (const auto name = a_form->GetName(); name && name[0]) {
				return name;
			}
			if (const auto edid = a_form->GetFormEditorID(); edid && edid[0]) {
				return edid;
			}
			return std::format("[{:08X}]", a_form->GetFormID());
		}

		void EditString(const char* a_label, std::string& a_value)
		{
			char buf[256];
			std::strncpy(buf, a_value.c_str(), sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			if (ImGui::InputText(a_label, buf, sizeof(buf))) {
				a_value = buf;
			}
		}

		void FlagCheckbox(const char* a_label, std::uint32_t& a_mask, std::uint32_t a_bit)
		{
			bool on = (a_mask & a_bit) != 0;
			if (ImGui::Checkbox(a_label, &on)) {
				if (on) {
					a_mask |= a_bit;
				} else {
					a_mask &= ~a_bit;
				}
			}
		}

		bool ContainsRef(const std::vector<FormRef>& a_list, const FormRef& a_ref)
		{
			return std::find(a_list.begin(), a_list.end(), a_ref) != a_list.end();
		}

		void RenderRefList(std::vector<FormRef>& a_list)
		{
			const auto handler = RE::TESDataHandler::GetSingleton();
			for (int i = 0; i < static_cast<int>(a_list.size()); ++i) {
				ImGui::PushID(i);
				if (ImGui::SmallButton("X")) {
					a_list.erase(a_list.begin() + i);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
				ImGui::SameLine();
				const auto form = (a_list[i].Valid() && handler) ?
				                      handler->LookupForm(a_list[i].localID, a_list[i].plugin) :
				                      nullptr;
				ImGui::TextUnformatted(form ? DisplayName(form).c_str() : a_list[i].Serialize().c_str());
			}
		}

		template <class T>
		void RenderRefPicker(std::vector<FormRef>& a_list, char* a_search, std::size_t a_searchSize, const char* a_hint)
		{
			ImGui::InputTextWithHint("##search", a_hint, a_search, a_searchSize);

			const auto handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return;
			}
			const std::string needle = ToLower(a_search);

			if (ImGui::BeginChild("##list", ImVec2(0, 160), true)) {
				int shown = 0;
				for (const auto form : handler->GetFormArray<T>()) {
					if (!form) {
						continue;
					}
					const std::string name = DisplayName(form);
					if (!needle.empty() && ToLower(name).find(needle) == std::string::npos) {
						continue;
					}
					if (shown++ >= kMaxShown) {
						ImGui::TextDisabled("...refine search to see more");
						break;
					}

					const FormRef ref = FormRef::From(form);
					ImGui::PushID(static_cast<int>(form->GetFormID()));
					if (ContainsRef(a_list, ref)) {
						ImGui::BeginDisabled();
						ImGui::SmallButton("Added");
						ImGui::EndDisabled();
					} else if (ImGui::SmallButton("Add")) {
						a_list.push_back(ref);
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGui::TextUnformatted(name.c_str());
				}
			}
			ImGui::EndChild();
		}

		void RenderItemPicker(std::vector<FormRef>& a_items)
		{
			ImGui::Checkbox("Wearable only", &gWearableOnly);
			ImGui::SameLine();
			ImGui::Checkbox("Player inventory only", &gInventoryOnly);
			ImGui::InputTextWithHint("##itemsearch", "Search clothing/armor...", gItemSearch, sizeof(gItemSearch));

			const auto handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return;
			}

			std::unordered_set<RE::FormID> playerItems;
			if (gInventoryOnly) {
				if (const auto player = RE::PlayerCharacter::GetSingleton()) {
					for (const auto& [obj, count] : player->GetInventoryCounts()) {
						if (count > 0) {
							playerItems.insert(obj->GetFormID());
						}
					}
				}
			}

			const std::string needle = ToLower(gItemSearch);

			if (ImGui::BeginChild("##itemlist", ImVec2(0, 200), true)) {
				int shown = 0;
				for (const auto armor : handler->GetFormArray<RE::TESObjectARMO>()) {
					if (!armor) {
						continue;
					}
					if (gWearableOnly &&
						(static_cast<std::uint32_t>(armor->GetSlotMask()) & kWearableSlots) == 0) {
						continue;
					}
					if (gInventoryOnly && !playerItems.contains(armor->GetFormID())) {
						continue;
					}
					const std::string name = DisplayName(armor);
					if (!needle.empty() && ToLower(name).find(needle) == std::string::npos) {
						continue;
					}
					if (shown++ >= kMaxShown) {
						ImGui::TextDisabled("...refine search to see more");
						break;
					}

					const FormRef ref = FormRef::From(armor);
					ImGui::PushID(static_cast<int>(armor->GetFormID()));
					if (ContainsRef(a_items, ref)) {
						ImGui::BeginDisabled();
						ImGui::SmallButton("Added");
						ImGui::EndDisabled();
					} else if (ImGui::SmallButton("Add")) {
						a_items.push_back(ref);
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGui::TextUnformatted(name.c_str());
				}
			}
			ImGui::EndChild();
		}

		void RenderRule(Rule& a_rule, bool& a_deleteRequested)
		{
			ImGui::PushID(static_cast<int>(a_rule.id));

			const std::string header = std::format("{}  [{}]###rulehdr",
			                                       a_rule.name.empty() ? "(unnamed)" : a_rule.name,
			                                       a_rule.preset.empty() ? "Default" : a_rule.preset);
			if (ImGui::CollapsingHeader(header.c_str())) {
				ImGui::Indent();

				ImGui::Checkbox("Enabled", &a_rule.enabled);
				EditString("Name", a_rule.name);
				EditString("Preset (shareable file)", a_rule.preset);

				int target = static_cast<int>(a_rule.target);
				const char* targets[] = { "All NPCs", "Followers only" };
				if (ImGui::Combo("Applies to", &target, targets, 2)) {
					a_rule.target = static_cast<Target>(target);
				}

				int chance = static_cast<int>(a_rule.chance);
				if (ImGui::SliderInt("Chance per NPC (%)", &chance, 0, 100)) {
					a_rule.chance = static_cast<std::uint32_t>(chance);
				}

				ImGui::SeparatorText("Weather (none = any)");
				for (std::uint32_t i = 0; i < kWeatherNames.size(); ++i) {
					FlagCheckbox(kWeatherNames[i], a_rule.weatherMask, 1u << i);
					if (i + 1 < kWeatherNames.size()) {
						ImGui::SameLine();
					}
				}

				ImGui::SeparatorText("Season (none = any)");
				for (std::uint32_t i = 0; i < kSeasonNames.size(); ++i) {
					FlagCheckbox(kSeasonNames[i], a_rule.seasonMask, 1u << i);
					if (i + 1 < kSeasonNames.size()) {
						ImGui::SameLine();
					}
				}

				ImGui::Spacing();
				if (ImGui::TreeNode(std::format("Regions ({}) - none = any###regions", a_rule.regions.size()).c_str())) {
					RenderRefList(a_rule.regions);
					RenderRefPicker<RE::TESRegion>(a_rule.regions, gRegionSearch, sizeof(gRegionSearch), "Search regions...");
					ImGui::TreePop();
				}
				if (ImGui::TreeNode(std::format("Excluded races ({})###races", a_rule.excludedRaces.size()).c_str())) {
					RenderRefList(a_rule.excludedRaces);
					RenderRefPicker<RE::TESRace>(a_rule.excludedRaces, gRaceSearch, sizeof(gRaceSearch), "Search races...");
					ImGui::TreePop();
				}
				if (ImGui::TreeNode(std::format("Items to equip ({})###items", a_rule.items.size()).c_str())) {
					RenderRefList(a_rule.items);
					RenderItemPicker(a_rule.items);
					ImGui::TreePop();
				}

				ImGui::Spacing();
				ImGui::PushStyleColor(ImGui::ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
				if (ImGui::Button("Delete this rule")) {
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

			ImGui::TextWrapped("Rules equip weather-appropriate clothing on NPCs. A rule with no weather, "
			                   "season or region set applies always. Chance and item selection are randomized "
			                   "per NPC, so a crowd won't all wear the same thing.");
			ImGui::Spacing();

			ImGui::Checkbox("Mod enabled", &config.enabled);
			ImGui::SameLine();
			ImGui::Checkbox("Outdoors only", &config.onlyOutdoors);

			int poll = static_cast<int>(config.pollSeconds.load(std::memory_order_relaxed));
			ImGui::SetNextItemWidth(240.0f);
			if (ImGui::SliderInt("Check interval (seconds)", &poll, 1, 30)) {
				config.pollSeconds.store(static_cast<std::uint32_t>(poll), std::memory_order_relaxed);
			}

			ImGui::Spacing();
			if (ImGui::Button("Save & Apply")) {
				config.Save();
				config.Bump();
				Manager::GetSingleton().RequestApply();
			}
			ImGui::SameLine();
			if (ImGui::Button("Reload from disk")) {
				config.Load();
				Manager::GetSingleton().RequestApply();
			}

			ImGui::SeparatorText("Presets");
			ImGui::TextWrapped("Each preset is saved as its own shareable .json file. Set a rule's Preset to group it "
			                   "into a file. Drop a shared preset into the folder below and Reload to use it. Under a "
			                   "mod manager these writes land in your overwrite folder.");
			ImGui::TextDisabled("Folder: %s", Config::PresetsLocation().c_str());
			{
				std::map<std::string, int> counts;
				for (const auto& r : config.rules) {
					counts[r.preset.empty() ? "Default" : r.preset]++;
				}
				for (const auto& [name, count] : counts) {
					ImGui::BulletText("%s.json  -  %d rule(s)", name.c_str(), count);
				}
			}

			ImGui::SeparatorText("Rules");
			if (ImGui::Button("+ Add rule")) {
				Rule rule;
				rule.id = MakeRuleID();
				config.rules.push_back(std::move(rule));
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
