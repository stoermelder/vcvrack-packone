#pragma once
namespace StoermelderPackOne {
namespace EightFace {

// List of module slugs which need to be handled by the GUI-thread instead of 8FACE's worker-thread.
// https://github.com/stoermelder/vcvrack-packone/issues/76
const std::set<std::tuple<std::string, std::string>> guiModuleSlugs = {
	std::make_tuple("Entrian-Free", "Player-Timeline"),
	std::make_tuple("Entrian-Free", "Player-Melody"),
	std::make_tuple("Entrian-Free", "Player-Drummer"),
	std::make_tuple("Entrian-Sequencers", "Timeline"),
	std::make_tuple("Entrian-Sequencers", "Melody"),
	std::make_tuple("Entrian-Sequencers", "Drummer"),
	std::make_tuple("Entrian-Sequencers", "CV"),
	std::make_tuple("Entrian-AcousticDrums", "AcousticDrums"),
	std::make_tuple("Entrian-AcousticDrums", "Drummer")
};

} // namespace EightFace
} // namespace StoermelderPackOne