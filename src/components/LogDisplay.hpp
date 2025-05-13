#pragma once
#include <rack.hpp>
#include "LedTextField.hpp"

namespace StoermelderPackOne {

enum class LOG_FORMAT {
	RESET,
	TIMESTAMP,
	INDENTED,
	TEXT
};

struct LogDisplay : LedTextDisplay {
	std::list<std::tuple<LOG_FORMAT, float, std::string>>* buffer;
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
			for (std::tuple<LOG_FORMAT, float, std::string> s : *buffer) {
				if (i >= size) break;
				LOG_FORMAT f = std::get<0>(s);
				float timestamp = std::get<1>(s);
				switch (f) {
					case LOG_FORMAT::TIMESTAMP:
						text += string::f("[%9.4f] %s\n", timestamp, std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::TEXT:
						text += string::f("%s\n", std::get<2>(s).c_str());
						break;
					case LOG_FORMAT::INDENTED:
						text += string::f("     %s\n", std::get<2>(s).c_str());
						break;
					default:
						break;
				};
			}
		}
	}

	void reset() {
		buffer->clear();
		dirty = true;
	}
};

}