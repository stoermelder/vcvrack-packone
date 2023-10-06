#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

struct LogDisplay : LedTextDisplay {
	std::list<std::tuple<float, std::string>>* buffer;
	bool dirty = true;

	LogDisplay() {
		color = nvgRGB(0xf0, 0xf0, 0xf0);
		bgColor.a = 0.f;
		fontSize = 9.2f;
		textOffset.y += 2.f;
	}

	void step() override {
		LedTextDisplay::step();
		if (dirty) {
			text = "";
			size_t size = std::min(buffer->size(), (size_t)(box.size.x / fontSize) + 1);
			size_t i = 0;
			for (std::tuple<float, std::string> s : *buffer) {
				if (i >= size) break;
				float timestamp = std::get<0>(s);
				if (timestamp >= 0.f) {
					text += string::f("[%9.4f] %s\n", timestamp, std::get<1>(s).c_str());
				}
				else {
					text += string::f("%s\n", std::get<1>(s).c_str());
				}
				i++;
			}
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}
};

}