#pragma once
#include "../../plugin.hpp"

namespace StoermelderPackOne {
namespace Mb {

// Ratings (1..5 stars, 0 = unrated), persisted on their own in mb-ratings.json

static const int RATING_MAX = 5;

extern std::map<Model*, int> modelRatings;

int modelRatingGet(Model* model);              // 0 if unrated
void modelRatingSet(Model* model, int rating); // clamps to 0..RATING_MAX; 0 erases; saves the file
void modelRatingsFromJson();
void modelRatingsToJson();

// n x U+2605; the empty string for 0
std::string ratingStarsText(int n);

// Squares for a rating's star column: bottom-right of a box of `size`, one per star,
// stacked upward from the bottom edge. Empty when unrated or too small to read.
std::vector<math::Rect> ratingStarRects(math::Vec size, int rating);

// Draws the rating's star column into a box of `size`.
void drawRatingStars(NVGcontext* vg, math::Vec size, int rating);

} // namespace Mb
} // namespace StoermelderPackOne
