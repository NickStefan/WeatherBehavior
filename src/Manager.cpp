#include "Manager.h"

#include <chrono>
#include <random>
#include <unordered_set>
#include <vector>

#include "Config.h"
#include "Util.h"

namespace WeatherBehavior
{
	Manager& Manager::GetSingleton()
	{
		static Manager instance;
		return instance;
	}

	Manager::~Manager()
	{
		Stop();
	}

	void Manager::Start()
	{
		if (_running.exchange(true)) {
			return;
		}
		_worker = std::thread(&Manager::PollLoop, this);
	}

	void Manager::Stop()
	{
		if (!_running.exchange(false)) {
			return;
		}
		_sleepCv.notify_all();
		if (_worker.joinable()) {
			_worker.join();
		}
	}

	void Manager::RequestApply()
	{
		_wake.store(true);
		_sleepCv.notify_all();
	}

	void Manager::PollLoop()
	{
		while (_running.load()) {
			if (auto* task = SKSE::GetTaskInterface()) {
				task->AddTask([this]() { Tick(); });
			}

			const auto seconds =
				std::max<std::uint32_t>(1, Config::GetSingleton().pollSeconds.load(std::memory_order_relaxed));
			std::unique_lock lock(_sleepMutex);
			_sleepCv.wait_for(lock, std::chrono::seconds(seconds), [this]() {
				return !_running.load() || _wake.load();
			});
			_wake.store(false);
		}
	}

	void Manager::Tick()
	{
		if (!Config::GetSingleton().enabled.load(std::memory_order_relaxed)) {
			if (!_forced.empty()) {
				RevertAll();
			}
			return;
		}
		Apply();
	}

	namespace
	{
		struct ActiveRule
		{
			std::uint32_t           id{ 0 };
			Target                  target{ Target::kAllNPCs };
			std::uint32_t           chance{ 100 };
			std::vector<RE::FormID> pool;
		};

		bool IsEligible(const RE::Actor* a_actor, const RE::PlayerCharacter* a_player)
		{
			if (!a_actor || a_actor == a_player || a_actor->IsDead() || a_actor->IsDisabled() ||
				a_actor->IsChild()) {
				return false;
			}
			const auto race = a_actor->GetRace();
			return race && race->HasKeywordString("ActorTypeNPC");
		}
	}

	void Manager::Apply()
	{
		auto& config = Config::GetSingleton();
		const auto processLists = RE::ProcessLists::GetSingleton();
		const auto equipManager = RE::ActorEquipManager::GetSingleton();
		const auto sky = RE::Sky::GetSingleton();
		const auto calendar = RE::Calendar::GetSingleton();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!processLists || !equipManager || !sky || !calendar) {
			return;
		}

		const std::uint32_t weather = WeatherClassOf(sky->currentWeather);
		const std::uint32_t season = SeasonBitOf(calendar->GetMonth());
		const bool onlyOutdoors = config.onlyOutdoors.load(std::memory_order_relaxed);

		std::vector<ActiveRule> active;
		{
			std::scoped_lock lock(config.rulesMutex);
			for (const auto& rule : config.rules) {
				if (!rule.EnvMatches(weather, season) || rule.items.empty()) {
					continue;
				}
				ActiveRule ar{ rule.id, rule.target, rule.chance, {} };
				ar.pool.reserve(rule.items.size());
				for (const auto& edid : rule.items) {
					if (const auto armor = RE::TESForm::LookupByEditorID<RE::TESObjectARMO>(edid)) {
						ar.pool.push_back(armor->GetFormID());
					}
				}
				if (!ar.pool.empty()) {
					active.push_back(std::move(ar));
				}
			}
		}

		if (active.empty() && _forced.empty()) {
			return;
		}

		processLists->ForEachHighActor([&](RE::Actor* a_actor) {
			if (!IsEligible(a_actor, player)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const RE::FormID actorID = a_actor->GetFormID();
			const bool isFollower = a_actor->IsPlayerTeammate();
			const auto cell = a_actor->GetParentCell();
			const bool indoors = onlyOutdoors && cell && cell->IsInteriorCell();

			std::unordered_set<RE::FormID> desired;
			if (!indoors) {
				for (const auto& rule : active) {
					if (rule.target == Target::kFollowersOnly && !isFollower) {
						continue;
					}
					std::mt19937 rng(actorID * 2654435761u ^ (rule.id * 40503u));
					if (rule.chance < 100 && (rng() % 100u) >= rule.chance) {
						continue;
					}
					desired.insert(rule.pool[rng() % rule.pool.size()]);
				}
			}

			const auto forcedIt = _forced.find(actorID);
			auto*      worn = forcedIt != _forced.end() ? &forcedIt->second : nullptr;
			if (!worn && desired.empty()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			std::unordered_set<RE::FormID> removing;
			if (worn) {
				for (auto it = worn->begin(); it != worn->end();) {
					if (!desired.contains(it->first)) {
						if (const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(it->first)) {
							equipManager->UnequipObject(a_actor, armor, nullptr, 1, nullptr, true, true, false, false);
							if (it->second) {
								a_actor->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
							}
						}
						removing.insert(it->first);
						it = worn->erase(it);
					} else {
						++it;
					}
				}
			}

			std::vector<RE::TESObjectARMO*> toAdd;
			for (const auto id : desired) {
				if (worn && worn->contains(id)) {
					continue;
				}
				if (const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
					toAdd.push_back(armor);
				}
			}

			if (!toAdd.empty()) {
				std::uint32_t                  occupied = 0;
				std::unordered_set<RE::FormID> owned;
				const auto inventory = a_actor->GetInventory([](RE::TESBoundObject& a_obj) { return a_obj.IsArmor(); });
				for (const auto& [obj, data] : inventory) {
					if (!obj || data.first <= 0) {
						continue;
					}
					const auto id = obj->GetFormID();
					owned.insert(id);
					if (data.second && data.second->IsWorn() && !removing.contains(id) &&
						!(worn && worn->contains(id))) {
						if (const auto wornArmor = obj->As<RE::TESObjectARMO>()) {
							occupied |= static_cast<std::uint32_t>(wornArmor->GetSlotMask());
						}
					}
				}

				for (const auto armor : toAdd) {
					const auto mask = static_cast<std::uint32_t>(armor->GetSlotMask());
					if ((mask & occupied) != 0) {
						continue;
					}
					const auto id = armor->GetFormID();
					const bool added = !owned.contains(id);
					if (added) {
						a_actor->AddObjectToContainer(armor, nullptr, 1, nullptr);
					}
					equipManager->EquipObject(a_actor, armor, nullptr, 1, nullptr, true, true, false, false);
					if (!worn) {
						worn = &_forced[actorID];
					}
					(*worn)[id] = added;
					occupied |= mask;
				}
			}

			if (worn && worn->empty()) {
				_forced.erase(actorID);
			}

			return RE::BSContainer::ForEachResult::kContinue;
		});
	}

	void Manager::RevertAll()
	{
		const auto equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}

		for (auto it = _forced.begin(); it != _forced.end();) {
			const auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
			if (!actor) {
				++it;
				continue;
			}
			for (const auto& [itemID, addedCopy] : it->second) {
				if (const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(itemID)) {
					equipManager->UnequipObject(actor, armor, nullptr, 1, nullptr, true, true, false, false);
					if (addedCopy) {
						actor->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					}
				}
			}
			it = _forced.erase(it);
		}
	}

	namespace
	{
		constexpr std::uint32_t kSerUID = 'WBHV';
		constexpr std::uint32_t kRecForced = 'FRCD';
		constexpr std::uint32_t kRecVersion = 1;

		void SaveCallback(SKSE::SerializationInterface* a_intfc) { Manager::GetSingleton().SaveState(a_intfc); }
		void LoadCallback(SKSE::SerializationInterface* a_intfc) { Manager::GetSingleton().LoadState(a_intfc); }
		void RevertCallback(SKSE::SerializationInterface*) { Manager::GetSingleton().RevertState(); }
	}

	void Manager::RegisterSerialization()
	{
		const auto ser = SKSE::GetSerializationInterface();
		if (!ser) {
			return;
		}
		ser->SetUniqueID(kSerUID);
		ser->SetSaveCallback(SaveCallback);
		ser->SetLoadCallback(LoadCallback);
		ser->SetRevertCallback(RevertCallback);
	}

	void Manager::SaveState(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc->OpenRecord(kRecForced, kRecVersion)) {
			return;
		}
		const std::uint32_t actorCount = static_cast<std::uint32_t>(_forced.size());
		a_intfc->WriteRecordData(actorCount);
		for (const auto& [actorID, worn] : _forced) {
			a_intfc->WriteRecordData(actorID);
			const std::uint32_t itemCount = static_cast<std::uint32_t>(worn.size());
			a_intfc->WriteRecordData(itemCount);
			for (const auto& [itemID, added] : worn) {
				a_intfc->WriteRecordData(itemID);
				const std::uint8_t flag = added ? 1 : 0;
				a_intfc->WriteRecordData(flag);
			}
		}
	}

	void Manager::LoadState(SKSE::SerializationInterface* a_intfc)
	{
		_forced.clear();

		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kRecForced) {
				continue;
			}
			std::uint32_t actorCount = 0;
			if (!a_intfc->ReadRecordData(actorCount)) {
				return;
			}
			for (std::uint32_t i = 0; i < actorCount; ++i) {
				RE::FormID    actorID = 0;
				std::uint32_t itemCount = 0;
				if (!a_intfc->ReadRecordData(actorID) || !a_intfc->ReadRecordData(itemCount)) {
					return;
				}

				RE::FormID newActorID = 0;
				const bool actorOk = a_intfc->ResolveFormID(actorID, newActorID);

				std::unordered_map<RE::FormID, bool> worn;
				for (std::uint32_t j = 0; j < itemCount; ++j) {
					RE::FormID   itemID = 0;
					std::uint8_t flag = 0;
					if (!a_intfc->ReadRecordData(itemID) || !a_intfc->ReadRecordData(flag)) {
						return;
					}

					RE::FormID newItemID = 0;
					if (actorOk && a_intfc->ResolveFormID(itemID, newItemID)) {
						worn[newItemID] = flag != 0;
					}
				}

				if (actorOk && !worn.empty()) {
					_forced[newActorID] = std::move(worn);
				}
			}
		}
	}

	void Manager::RevertState()
	{
		_forced.clear();
	}
}
