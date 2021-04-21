#pragma once
#include "../plugin.hpp"

namespace StoermelderPackOne {

struct OverlayMessageProvider {
	struct Message {
		std::string title;
		std::string subtitle;
		bool empty() { return title.size() == 0 && subtitle.size() == 0; }
	};

	virtual int nextOverlayMessageId() { return -1; }
	virtual void getOverlayMessage(int id, Message& m) { }
};

struct OverlayMessageWidget : TransparentWidget {
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
			float width = 340.f;
			float height = 80.f;
			//float xOffset = 22.f;
			float yOffset = 40.f;
			NVGcolor fgColor = bndGetTheme()->menuTheme.textColor;

			for (auto it = items.begin(); it != items.end(); it++) {
				OverlayMessageProvider* p = std::get<0>(it->first);
				int id = std::get<1>(it->first);

				OverlayMessageProvider::Message m;
				p->getOverlayMessage(id, m);

				if (now - it->second > std::chrono::seconds{1}) {
					items.erase(it);
					break;
				}

				if (m.empty()) continue;

				float x = args.clipBox.pos.x + args.clipBox.size.x / 2.f; // + (-1.f * (n - 1.f) / 2.f + i) * (width + xOffset);
				float y = args.clipBox.pos.y + args.clipBox.size.y - height - yOffset - (i * (height + 16.f));

				bndMenuBackground(args.vg, x - width / 2.f, y, width, height, BND_CORNER_NONE);

				nvgFontFaceId(args.vg, font->handle);
				nvgTextLetterSpacing(args.vg, -1.2f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
				nvgFillColor(args.vg, fgColor);
				NVGtextRow textRow;

				if (m.title.size() > 0) {
					nvgFontSize(args.vg, 32.f);
					nvgTextBreakLines(args.vg, m.title.c_str(), NULL, width - 10.f, &textRow, 1);
					nvgTextBox(args.vg, x - width / 2.f, y + 10.f, width, textRow.start, textRow.end);
				}
				if (m.subtitle.size() > 0) {
					nvgFontSize(args.vg, 20.f);
					nvgTextBreakLines(args.vg, m.subtitle.c_str(), NULL, width - 10.f, &textRow, 2);
					nvgTextBox(args.vg, x - width / 2.f, y + 50.f, width, textRow.start, textRow.end);
				}

				i++;
			}
		}
	}
};

} // namespace StoermelderPackOne