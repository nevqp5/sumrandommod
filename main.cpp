/*
 * Macro Assist — Geode mod for Geometry Dash 2.2081 (Windows)
 *
 * Idea: you load a macro. When YOU press/release a button, the mod looks for the
 * nearest macro event of the same kind that is at the current frame or up to
 * `max-delay` frames in the future, and delays your input so it lands exactly on
 * that macro frame. (You pressed on 996, macro says 1000 -> input fires on 1000.)
 * It can only delay, never move input earlier, so if you press too late
 * (after the macro frame) the input passes through unchanged.
 *
 * Supported macro formats
 * -----------------------
 * 1) .json  — expects {"events":[{"frame":1000,"down":true,"btn":1,"p2":false}, ...]}
 *    NOTE: I could not verify MegaHack's exact JSON key names. All the key lookups
 *    are in loadJson() below — compare with your actual exported file and rename
 *    the keys if they differ. Binary .mhr is NOT supported.
 * 2) .txt   — one event per line:  <frame> <down|up> [button=1] [p1|p2]
 *    example:  1000 down
 *              1003 up
 *              1040 down 1 p2
 *    lines starting with '#' are ignored.
 *
 * Frames are physics steps (what m_gameState.m_currentProgress counts), so run
 * with the same TPS (e.g. 240) that the macro was recorded at.
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

using namespace geode::prelude;

struct MacroEvent {
	int frame;
	bool down;
	int button;
	bool p1;
	bool used = false;
};

struct PendingInput {
	int frame;
	bool down;
	int button;
	bool p1;
};

namespace state {
	inline std::vector<MacroEvent> events;
	inline std::vector<PendingInput> pending;
	inline bool injecting = false;
}

static void loadJson(std::filesystem::path const& path) {
	auto res = file::readJson(path);
	if (!res) {
		log::error("Failed to read macro json: {}", res.unwrapErr());
		return;
	}
	auto json = res.unwrap();
	auto arr = json["events"].asArray();
	if (!arr) {
		log::error("Macro json has no 'events' array — check key names in loadJson()");
		return;
	}
	for (auto& ev : arr.unwrap()) {
		// >>> adjust these key names to match your exported file <<<
		if (!ev.contains("frame") || !ev.contains("down")) continue;
		MacroEvent e;
		e.frame = static_cast<int>(ev["frame"].asInt().unwrapOr(0));
		e.down = ev["down"].asBool().unwrapOr(false);
		e.button = ev.contains("btn") ? static_cast<int>(ev["btn"].asInt().unwrapOr(1)) : 1;
		e.p1 = ev.contains("p2") ? !ev["p2"].asBool().unwrapOr(false) : true;
		state::events.push_back(e);
	}
}

static void loadTxt(std::filesystem::path const& path) {
	std::ifstream in(path);
	if (!in) {
		log::error("Failed to open macro txt");
		return;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ss(line);
		int frame = 0;
		std::string action, extra;
		if (!(ss >> frame >> action)) continue;
		MacroEvent e;
		e.frame = frame;
		e.down = (action == "down");
		e.button = 1;
		e.p1 = true;
		while (ss >> extra) {
			if (extra == "p2") e.p1 = false;
			else if (extra == "p1") e.p1 = true;
			else e.button = std::atoi(extra.c_str());
		}
		state::events.push_back(e);
	}
}

static void loadMacro() {
	state::events.clear();
	state::pending.clear();

	auto path = Mod::get()->getSettingValue<std::filesystem::path>("macro-path");
	if (path.empty()) return;

	if (path.extension() == ".json") loadJson(path);
	else loadTxt(path);

	int offset = static_cast<int>(Mod::get()->getSettingValue<int64_t>("frame-offset"));
	for (auto& e : state::events) e.frame += offset;

	std::stable_sort(state::events.begin(), state::events.end(),
		[](MacroEvent const& a, MacroEvent const& b) { return a.frame < b.frame; });

	log::info("Macro Assist: loaded {} events", state::events.size());
}

class $modify(AssistPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		loadMacro();
		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		for (auto& e : state::events) e.used = false;
		state::pending.clear();
	}
};

class $modify(AssistBaseLayer, GJBaseGameLayer) {
	// Intercept the player's own inputs.
	void handleButton(bool down, int button, bool isPlayer1) {
		auto mod = Mod::get();
		if (state::injecting
			|| state::events.empty()
			|| !mod->getSettingValue<bool>("enabled")
			|| !PlayLayer::get()) {
			return GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}

		int cur = m_gameState.m_currentProgress;
		int maxDelay = static_cast<int>(mod->getSettingValue<int64_t>("max-delay"));

		MacroEvent* match = nullptr;
		for (auto& e : state::events) {
			if (e.frame < cur) continue;       // already in the past
			if (e.frame > cur + maxDelay) break; // sorted, nothing further matters
			if (e.used || e.down != down || e.button != button || e.p1 != isPlayer1) continue;
			match = &e;
			break;
		}

		if (!match) {
			if (mod->getSettingValue<bool>("block-unmatched")) return;
			return GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}

		match->used = true;
		if (match->frame == cur) {
			return GJBaseGameLayer::handleButton(down, button, isPlayer1);
		}
		// Delay: swallow now, fire on the macro's frame.
		state::pending.push_back({match->frame, down, button, isPlayer1});
	}

	// Fire delayed inputs at the start of the right physics step.
	void processCommands(float dt) {
		if (!state::pending.empty() && PlayLayer::get()) {
			int cur = m_gameState.m_currentProgress;
			state::injecting = true;
			for (auto it = state::pending.begin(); it != state::pending.end();) {
				if (it->frame <= cur) {
					GJBaseGameLayer::handleButton(it->down, it->button, it->p1);
					it = state::pending.erase(it);
				} else {
					++it;
				}
			}
			state::injecting = false;
		}
		GJBaseGameLayer::processCommands(dt);
	}
};
