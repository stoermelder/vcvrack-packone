#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

struct OverlayMessageProvider {
	struct Message {
		std::string title;
		std::string subtitle[2];
		bool empty() { return title.size() == 0 && subtitle[0].size() == 0; }
	};

	virtual int nextOverlayMessageId() { return -1; }
	virtual void getOverlayMessage(int id, Message& m) { }
};

struct OverlayMessageWidget : TransparentWidget {
	enum class VPOS {
		BOTTOM = 0,
		TOP = 1
	};
	enum HPOS {
		CENTER = 0,
		LEFT = 1,
		RIGHT = 2
	};

	const float xOffset = 22.f;
	const float yOffset = 40.f;

	std::list<OverlayMessageProvider*> registeredProviders;
	std::map<std::tuple<OverlayMessageProvider*, int>, std::chrono::time_point<std::chrono::system_clock>> items;
	std::shared_ptr<Font> font;

	static OverlayMessageWidget& instance() {
		static OverlayMessageWidget overlayMessageWidget;
		return overlayMessageWidget;
	}

	static void registerProvider(OverlayMessageProvider* p) {
		if (instance().registeredProviders.size() == 0) {
			APP->scene->rackScroll->addChild(&instance());
		}
		instance().registeredProviders.push_back(p);
	}

	static void unregisterProvider(OverlayMessageProvider* p) {
		instance().registeredProviders.remove(p);
		if (instance().registeredProviders.size() == 0) {
			APP->scene->rackScroll->removeChild(&instance());
		}
	}

	OverlayMessageWidget() {
		font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
	}

	void draw(const DrawArgs& args) override {
		auto now = std::chrono::system_clock::now();
		for (OverlayMessageProvider* p : registeredProviders) {
			while (true) {
				int id = p->nextOverlayMessageId();
				if (id < 0) break;
				items[std::make_tuple(p, id)] = now;
			}
		}

		int n = items.size();
		if (n > 0) {
			float i = 0.f;
			float width = 360.f;
			float height = 100.f;
			NVGcolor fgColor = pluginSettings.overlayTextColor;

			for (auto it = items.begin(); it != items.end(); it++) {
				if (now - it->second > std::chrono::seconds{1}) {
					items.erase(it);
					break;
				}

				OverlayMessageProvider* p = std::get<0>(it->first);
				int id = std::get<1>(it->first);

				OverlayMessageProvider::Message m;
				p->getOverlayMessage(id, m);
				if (m.empty()) continue;

				float x, y;

				switch ((HPOS)pluginSettings.overlayHpos) { 
					case HPOS::CENTER:
						x = args.clipBox.pos.x + args.clipBox.size.x / 2.f; // + (-1.f * (n - 1.f) / 2.f + i) * (width + xOffset);
						break;
					case HPOS::LEFT:
						x = args.clipBox.pos.x + 30.f + width / 2.f;
						break;
					case HPOS::RIGHT:
						x = args.clipBox.pos.x + args.clipBox.size.x - 30.f - width / 2.f;
						break;
				}

				switch ((VPOS)pluginSettings.overlayVpos) {
					case VPOS::BOTTOM:
						y = args.clipBox.pos.y + args.clipBox.size.y - height - yOffset - (i * (height + 16.f));
						break;
					case VPOS::TOP:
						y = args.clipBox.pos.y + yOffset + (i * (height * 16.f));
						break;
				}

				bndMenuBackground(args.vg, x - width / 2.f, y, width, height, BND_CORNER_NONE);

				nvgFontFaceId(args.vg, font->handle);
				nvgTextLetterSpacing(args.vg, -1.2f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
				nvgFillColor(args.vg, fgColor);
				NVGtextRow textRow;

				y += 10.f;

				if (m.title.size() > 0) {
					nvgFontSize(args.vg, 32.f);
					nvgTextBreakLines(args.vg, m.title.c_str(), NULL, width - 10.f, &textRow, 1);
					nvgTextBox(args.vg, x - width / 2.f, y, width, textRow.start, textRow.end);
					y += 40.f;
				}
				if (m.subtitle[0].size() > 0) {
					nvgFontSize(args.vg, 20.f);
					nvgTextBreakLines(args.vg, m.subtitle[0].c_str(), NULL, width - 10.f, &textRow, 1);
					nvgTextBox(args.vg, x - width / 2.f, y, width, textRow.start, textRow.end);
					y += 20.f;
				}
				if (m.subtitle[1].size() > 0) {
					nvgFontSize(args.vg, 20.f);
					nvgTextBreakLines(args.vg, m.subtitle[1].c_str(), NULL, width - 10.f, &textRow, 1);
					nvgTextBox(args.vg, x - width / 2.f, y, width, textRow.start, textRow.end);
					y += 20.f;
				}

				i++;
			}
		}
	}
};

} // namespace StoermelderPackOne