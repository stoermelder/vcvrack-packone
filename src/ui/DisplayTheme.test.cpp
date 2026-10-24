#include "../test/test_plugin.hpp"
#include "DisplayTheme.hpp"

using namespace StoermelderPackOne;

namespace {
struct ThemeScope {
	std::string saved = rack::settings::uiTheme;
	explicit ThemeScope(const char* theme) { rack::settings::uiTheme = theme; }
	~ThemeScope() { rack::settings::uiTheme = saved; }
};
float luma(NVGcolor c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }
}

TEST_CASE("Only the light UI theme gives a light display", "[DisplayTheme]") {
	{ ThemeScope t("dark");   REQUIRE_FALSE(DisplayTheme::isLight()); }
	{ ThemeScope t("hcdark"); REQUIRE_FALSE(DisplayTheme::isLight()); }
	{ ThemeScope t("light");  REQUIRE(DisplayTheme::isLight()); }
}

TEST_CASE("Dark themes keep the original dark palette", "[DisplayTheme]") {
	ThemeScope t("dark");
	REQUIRE(luma(DisplayTheme::backgroundTop()) < 0.2f);
	REQUIRE(luma(DisplayTheme::ink()) == Catch::Approx(1.f));
	REQUIRE(DisplayTheme::scrim(0.5f).r == 0.f);
	NVGcolor accent = nvgRGBf(0.35f, 0.80f, 0.85f);
	REQUIRE(DisplayTheme::tone(accent).g == accent.g);
	REQUIRE(DisplayTheme::gold().g == Catch::Approx(0.85f));
}

TEST_CASE("Light theme flips background, foreground and accents", "[DisplayTheme]") {
	ThemeScope t("light");
	REQUIRE(luma(DisplayTheme::backgroundTop()) > 0.8f);
	REQUIRE(luma(DisplayTheme::backgroundBottom()) > 0.7f);
	REQUIRE(luma(DisplayTheme::pane()) > 0.7f);
	REQUIRE(luma(DisplayTheme::paneRecessed()) > 0.7f);
	REQUIRE(luma(DisplayTheme::ink()) == Catch::Approx(0.f));
	REQUIRE(DisplayTheme::ink(0.3f).a == Catch::Approx(0.3f));
	REQUIRE(DisplayTheme::scrim(0.5f).r == 1.f);
	// Accents get darker, keep alpha, and gold stays contrasty on light.
	NVGcolor accent = nvgRGBAf(0.35f, 0.80f, 0.85f, 0.5f);
	NVGcolor toned = DisplayTheme::tone(accent);
	REQUIRE(luma(toned) < luma(accent));
	REQUIRE(toned.a == Catch::Approx(0.5f));
	REQUIRE(luma(DisplayTheme::gold()) < 0.6f);
	// Vignette is weaker than on dark.
	REQUIRE(DisplayTheme::vignette(0.45f).a < 0.45f);
}
