#include "Ahab.generator.hpp"
#include "AhabPatternScore.hpp"
#include <algorithm>
#include <cassert>

namespace StoermelderPackOne {
namespace Ahab {

// Helper functions
char AhabGenerator::randomBase36() {
	static const char* b36 = "0123456789abcdefghijklmnopqrstuvwxyz";
	return b36[randInt(0, 35)];
}

char AhabGenerator::randomSmallDigit() {
	return '1' + randInt(0, 7);
}

char AhabGenerator::randomNote() {
	static const char* notes = "CDEFGAB";
	return notes[randInt(0, 6)];
}

char AhabGenerator::randomOctave() {
	// Octave bands correlate with the planned channel
	// (ch0 low/bass ... ch3 high/lead) with a little jitter. NB: builders that
	// pass randomChannel() and randomOctave() in one argument list have
	// unspecified evaluation order (C++11), so the band may latch to the
	// previous voice's channel — still correlated patch-wide.
	int band = lastChanIdx_ < 0 ? 0 : (lastChanIdx_ > 3 ? 3 : lastChanIdx_);
	int oct = 2 + band + randInt(0, 1);
	return (char)('2' + (oct > 4 ? 4 : oct)); // octaves 2-6
}

// Base36 helpers for euclid pair generation
static char b36Char(Usz v) {
	return "0123456789abcdefghijklmnopqrstuvwxyz"[v];
}
static Usz b36Val(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'z') return c - 'a' + 10;
	return 0;
}

void AhabGenerator::randomEuclid(char& steps, char& max) {
	// The modulus must DIVIDE the patch bar so every euclidean voice
	// locks to the same groove (the old free-for-all across 3..16 was
	// mathematically interesting but musically incoherent).
	Usz candidates[4];
	int n = 0;
	for (Usz d = 3; d <= barMod_; ++d) {
		if (barMod_ % d == 0 && n < 4) candidates[n++] = d;
	}
	if (n == 0) { max = b36Char(barMod_); }
	else max = b36Char(candidates[randInt(0, n - 1)]);
	Usz maxVal = b36Val(max);
	Usz s = (Usz)randInt(2, (int)maxVal - 1); // 2..maxVal-1: neither empty nor solid
	steps = b36Char(s);
}

char AhabGenerator::barDivisorMod() {
	// A modulus glyph dividing the patch bar. Divisors >= 2 keep
	// periods musical; the bar itself is included so voices can also span a
	// whole bar.
	Usz candidates[4];
	int n = 0;
	for (Usz d = 2; d <= barMod_; ++d) {
		if (barMod_ % d == 0 && n < 4) candidates[n++] = d;
	}
	return b36Char(candidates[randInt(0, n - 1)]);
}

void AhabGenerator::fillPatchWalk(std::string& notes, Usz len) {
	fillScaleWalkWith(notes, len, rng, patchRoot_, patchScaleIdx_);
}

char AhabGenerator::allocateVarName() {
	// Unique names per generate() call; 'g' is excluded from the
	// pool because it is reserved for the arrangement's master clock.
	if (varPool_.empty()) return 0;
	char c = varPool_.back();
	varPool_.pop_back();
	return c;
}

char AhabGenerator::allocateCcNumber() {
	// Control numbers come from a small per-call
	// pool exactly like chanPool_, so two Modulation edges never target the
	// same CC. Numbers stay in '0'..'3': midiCcOffset (default 64) is added
	// downstream and clamped to 127, so high base36 controls would collapse.
	if (ccPool_.empty()) return 0;
	char c = ccPool_.back();
	ccPool_.pop_back();
	return c;
}

char AhabGenerator::randomChannel() {
	// Melodic voices draw UNIQUE channels from a
	// per-call shuffled pool, so two voices never land on the same channel
	// and cut each other off. Drums bypass this (hardcoded channel 9).
	if (!chanPool_.empty()) {
		char c = chanPool_.back();
		chanPool_.pop_back();
		lastChanIdx_ = c - '0';
		return c;
	}
	lastChanIdx_ = randInt(0, 3);
	return '0' + lastChanIdx_; // pool exhausted
}

// Same mapping as sim.c's from_midi_note_number: lowercase glyph == sharp.
static Glyph glyphForSemitone(U8 note) {
	static Glyph const kNotes[12] = {'C', 'c', 'D', 'd', 'E', 'F', 'f', 'G', 'g', 'A', 'a', 'B'};
	return kNotes[note % 12];
}

void AhabGenerator::fillScaleWalkWith(std::string& notes, Usz len, std::mt19937& rng, int root, int scaleIdx) {
	// Scales as semitone offsets from the root — the old hand-typed
	// letter strings were not the scales they claimed to be and contained
	// duplicate degrees.
	static int const kMajor[]        = {0, 2, 4, 5, 7, 9, 11};
	static int const kNaturalMinor[] = {0, 2, 3, 5, 7, 8, 10};
	static int const kPentatonic[]   = {0, 2, 4, 7, 9};
	static int const kDorian[]       = {0, 2, 3, 5, 7, 9, 10};
	struct Scale { int const* semi; int n; };
	static Scale const kScales[] = {
		{kMajor, 7}, {kNaturalMinor, 7}, {kPentatonic, 5}, {kDorian, 7},
	};

	Scale const& scale = kScales[scaleIdx % 4];
	int rootPc = ((root % 12) + 12) % 12;

	// Bounded random walk over scale degrees (~2 octaves) — far more melodic
	// than uniform sampling of the table.
	std::uniform_int_distribution<int> walk(-2, 2);
	int degree = (int)(len / 2);
	if (degree > scale.n * 2) degree = scale.n * 2;
	notes.clear();
	for (Usz i = 0; i < len; ++i) {
		int octave = degree / scale.n;
		int semi = scale.semi[degree % scale.n];
		notes += glyphForSemitone((U8)(rootPc + octave * 12 + semi)); // patch key
		degree += walk(rng);                                          // step -2..+2
		if (degree < 0) degree = 0;
		if (degree > scale.n * 2) degree = scale.n * 2;
	}
}

void AhabGenerator::fillScaleWalk(std::string& notes, Usz len, std::mt19937& rng) {
	fillScaleWalkWith(notes, len, rng, std::uniform_int_distribution<int>(0, 11)(rng), std::uniform_int_distribution<int>(0, 3)(rng));
}

// The mechanical preconditions of "sounding like
// anything at all". Deliberately stricter than the CI gate's aggregate bounds
// because a rejected attempt costs only a re-roll, not a build failure.
static bool acceptable(PatternScore const& s) {
	return !s.silent()
		&& s.firstEventTick < 64
		&& s.distinctPitches >= 2
		// Upper bound relaxed from 0.9: capacity/packing now fill selections
		// properly, and a well-filled arrangement is simply active most
		// ticks — that is the point, not a wall-of-sound defect.
		&& s.density >= 0.05f && s.density <= 0.95f
		// A variable read with no preceding write always yields '.'
		// (vars_slots are wiped every tick). A bug marker, not taste — re-roll.
		&& s.danglingReads == 0
		// a '!' tail that emits but never varies is
		// the classic silent failure (midicc sends a constant 0 for a '.'
		// value).
		&& (s.ccEvents == 0 || s.distinctCcValues > 1);
}

// Pure core generate-and-reject loop: score each attempt in a
// throwaway sim and keep the best. Deterministic — retries derive their seeds
// from the original seed, so the whole chain is reproducible and getSeed()
// still describes the returned result. The returned buffer is selection-local.
ScratchPad AhabGenerator::generate(Usz height, Usz width, Config const& cfg) {
	if (cfg.seed != 0) {
		seed_ = cfg.seed;
	}
	rng.seed(seed_);

	ScratchPad best = generateOnce(height, width, cfg);
	if (!cfg.qualityGate) return best;

	// Bound the attempts: eight scored attempts at 64 ticks is ~a millisecond —
	// fine on the UI thread. Never unbounded.
	constexpr int kMaxAttempts = 8;
	uint32_t const origSeed = seed_;
	PatternScore bestScore = scorePattern(best, /*ticks=*/64);
	int bestDerivedCount = derivedPlaced_; // attempt 0's count
	for (int attempt = 1; attempt < kMaxAttempts && !acceptable(bestScore); ++attempt) {
		uint32_t retrySeed = origSeed + (uint32_t)attempt;
		rng.seed(retrySeed);
		ScratchPad cand = generateOnce(height, width, cfg);
		PatternScore cs = scorePattern(cand, /*ticks=*/64);
		if (cs.noteEvents > bestScore.noteEvents) {
			best = std::move(cand);
			bestScore = cs;
			seed_ = retrySeed; // getSeed() describes what the user got
			bestDerivedCount = derivedPlaced_; // ...and so does the counter
		}
	}
	// The loop may run rejected attempts AFTER the last adoption; the
	// counter must describe the RETURNED pattern, not the last one tried.
	derivedPlaced_ = bestDerivedCount;
	// Always return the best attempt, even if none passed — never nothing.
	return best;
}

// One raw generation pass (no retry loop). Seeding is managed by generate().
ScratchPad AhabGenerator::generateOnce(Usz height, Usz width, Config const& cfg) {
	// Decide the piece's shared identity ONCE per call:
	barMod_ = (randInt(0, 1) == 0) ? 8 : 12; // one bar length for everything
	patchRoot_ = randInt(0, 11);             // one key
	patchScaleIdx_ = randInt(0, 3);          // one scale
	busVars_.clear();                        // clock bus placed by this call
	derivedPlaced_ = 0;                      // Step 0 of the graph plan: counts THIS
											 // attempt only, not discarded retries
	// Refill the variable pool for this call — every letter except
	// the reserved ones ('g' drives the master clock, 'qwer…' the clock
	// bus) — voices must never collide with bus writers. Guarded erase:
	// a typo in the alphabet must throw-free skip, not crash the UI.
	varPool_ = "abcdefghijklmnopqrstuvwxyz";
	static char const* const kReservedVars = "gqwer";
	for (char const* p = kReservedVars; *p; ++p) {
		size_t pos = varPool_.find(*p);
		if (pos != std::string::npos) varPool_.erase(pos, 1);
	}
	std::shuffle(varPool_.begin(), varPool_.end(), rng);
	ScratchPad buf(height, width);

	// For small selections, just fill with simpler patterns
	if (height < 4 || width < 10) {
		// Simple fill mode - individual patterns
		for (Usz y = 0; y < height; ) {
			for (Usz x = 0; x < width; ) {
				if (chance(cfg.density)) {
					Extent used = generateSimplePattern(buf, y, x, height, width);
					x += used.empty() ? 1 : used.w + 1;
				} else {
					x++;
				}
			}
			// The vertical advance is density-dependent too, so low
			// density is genuinely sparse rather than fixed at every 2nd row.
			y += 2 + (chance(1.0f - cfg.density) ? 1 : 0);
		}
	}
	else {
		// Full arrangement mode
		generateArrangement(buf, 0, 0, height, width, cfg.density);
	}

	return buf;
}

// Sim adapter: generates into a buffer, then commits it in ONE queued command.
bool AhabGenerator::randomize(AhabSim* sim, Usz startY, Usz startX,
							   Usz height, Usz width, Config const& cfg) {
	if (!sim) return false;

	Usz fieldH = sim->getFieldHeight();
	Usz fieldW = sim->getFieldWidth();

	// Clamp selection to field bounds
	if (startY >= fieldH || startX >= fieldW) return false;
	if (startY + height > fieldH) height = fieldH - startY;
	if (startX + width > fieldW) width = fieldW - startX;
	if (height == 0 || width == 0) return false;

	ScratchPad buf = generate(height, width, cfg);

	// Nothing generated -> no undo entry, no notify, no queue traffic.
	if (!buf.dirty()) return false;

	// One command, not one per glyph. loadRectFromOrcaRequest already does
	// pushUndo() + notifyTick() internally, so we must NOT do either here.
	// Untouched cells are '.', so the full-rect paste also clears prior content
	// in the rect (cfg.clearFirst); overlay mode would need the existing field
	// contents and is not supported yet.
	Usz outH = 0, outW = 0;
	return sim->loadRectFromOrcaRequest(buf.toOrca(), startY, startX, outH, outW, /*replace_field=*/false);
}

AhabGenerator::Extent AhabGenerator::generateSimplePattern(ScratchPad& buf, Usz y, Usz x, Usz maxY, Usz maxX) {
	Usz availW = maxX > x ? maxX - x : 0;
	Usz availH = maxY > y ? maxY - y : 0;
	
	if (availW < 4 || availH < 1) return {};
	
	int patternType = randInt(0, 13);
	
	switch (patternType) {
		case 0: // Clock
			return placeClockPattern(buf, y, x, availW, randomSmallDigit(), randomBase36());
		case 1: // Delay (modulus divides the patch bar)
			return placeDelayPattern(buf, y, x, availW, randomSmallDigit(), barDivisorMod());
		case 2: // Random
			return placeRandomPattern(buf, y, x, availW, '0', randomBase36());
		case 3: { // Uclid (modulus divides the patch bar)
			char uSteps, uMax;
			randomEuclid(uSteps, uMax);
			return placeUclidPattern(buf, y, x, availW, uSteps, uMax);
		}
		case 4: { // Variable write
			char name = allocateVarName();
			if (!name) return {};
			return placeVarWrite(buf, y, x, availW, name, randomBase36());
		}
		case 5: // Add
			return placeAddPattern(buf, y, x, availW, randomBase36(), randomBase36());
		case 6: // If (compares cell values, not variables)
			return placeIfPattern(buf, y, x, availW, randomBase36(), randomBase36());
		case 7: { // Track with short sequence
			std::string notes;
			fillPatchWalk(notes, 4); // in the patch key
			// Fixed key '0': a standalone pattern has no clock driving the key
			// cell, so a random key would just read one arbitrary fixed slot.
			return placeTrackPattern(buf, y, x, availW, '0', notes);
		}
		// The remaining primitives are wired in as standalone
		// decorations so every builder is reachable from the generator.
		case 8: // Increment counter below each frame
			return placeIncrementPattern(buf, y, x, availW, randomSmallDigit(), randomBase36());
		case 9: // Multiply
			return placeMultiplyPattern(buf, y, x, availW, randomBase36(), randomBase36());
		case 10: // Halt operators below until banged
			return placeHaltPattern(buf, y, x, availW);
		case 11: // Offset read
			return placeOffsetPattern(buf, y, x, availW, randInt(0, 9) + '0', randInt(0, 9) + '0');
		case 12: // MIDI row (silent without a bang neighbour — decoration)
			return placeMidiPattern(buf, y, x, availW, randomChannel(), randomOctave(), randomNote(), 'f', '4');
		case 13: { // Variable round-trip: write then read on the SAME row
			// A standalone var-read can never see a write (vars do
			// not persist across ticks and order matters) — that is exactly a
			// dangling read. Pair it with its own write instead.
			char name = allocateVarName();
			if (!name) return {};
			if (availW < 9) return {}; // 4 (write) + 1 gap + 4 (read)
			placeVarWrite(buf, y, x, availW, name, randomBase36());
			return placeVarRead(buf, y, x + 5, availW - 5, name);
		}
	}
	return {};
}

// Relationships first, layout second. planArrangement() decides
// the cast and their couplings; the layout below emits leads before their
// dependents so every reader sits physically below its writer (vars are
// within-tick only). With only lead->dependent edges, emission
// order IS the topological order of the coupling graph.

// Only these roles publish a pitch variable other voices
// can read. Gates produce bangs; textures publish nothing.
bool publishesPitch(VoiceNode const& vn) {
	return vn.role == VoiceNode::Lead || vn.role == VoiceNode::Harmony;
}

std::vector<VoiceNode> AhabGenerator::planArrangement(Usz h, Usz w, float density, size_t nBus) {
	// Unique melodic channel plan - reset here (not just in generateOnce) so
	// standalone planner calls are self-contained. Shuffled so the hand-out
	// order varies with the seed instead of always running 3,2,1,0.
	chanPool_ = "0123";
	std::shuffle(chanPool_.begin(), chanPool_.end(), rng);
	lastChanIdx_ = 0;
	// Same treatment for the CC control-number pool.
	ccPool_ = "0123";
	std::shuffle(ccPool_.begin(), ccPool_.end(), rng);

	// Capacity scales with AREA so large scratchpads fill proportionally
	// instead of capping at a handful of voices. The divisor is tuned to the
	// real footprint of a voice plus its separator gap (~40 cells): density
	// 0.5 now means "fill the selection", 0.1 leaves it airy. The old
	// area/60 under-planned badly — a 16x32 Medium take planned three voices
	// in a rect that fits ten. Hard-capped at 64 to bound layout work.
	// Density 0 MUST plan nothing (the empty-generation contract pinned by
	// the Phase 1 tests).
	int const capacity = std::min(64, (int)((float)(w * h) / 25.f * density + 0.5f));
	if (capacity <= 0) return {};

	std::vector<VoiceNode> plan;

	// Node 0: the clock bus when present. Clock edges reference division
	// indices via param, so the whole bus is an ordinary graph participant.
	if (nBus > 0) {
		VoiceNode bus;
		bus.role = VoiceNode::Bus;
		plan.push_back(bus);
	}

	// Leads: one per bus division (so divisions get used), at least one.
	size_t const nLeads = std::min(std::max(nBus, (size_t)1), (size_t)std::max<Usz>(1, w / 14));
	for (size_t i = 0; i < nLeads && (int)plan.size() < capacity; ++i) {
		VoiceNode vp;
		vp.role = VoiceNode::Lead;
		vp.channel = randomChannel();
		if (nBus > 0) {
			vp.inputs.push_back(Edge(0, Edge::Clock, (char)i));
		}
		// Sequence length doubles as the clock modulus: pick it from the
		// patch bar's divisors so the voice walks its sequence in one bar.
		Usz divisors[4];
		int nd = 0;
		for (Usz d = 2; d <= barMod_ && d <= 8; ++d) {
			if (barMod_ % d == 0 && nd < 4) divisors[nd++] = d;
		}
		fillPatchWalk(vp.notes, nd ? divisors[randInt(0, nd - 1)] : 4);
		// Occasionally a lead's VELOCITY
		// follows another publisher's pitch contour — layout turns the edge
		// into a K (konkat) row feeding the ':'s note+velocity cells. Sources
		// are pitch publishers only: their glyphs never index to 0, which the
		// midi operator treats as a note-off (a bus counter hitting '0'
		// would silently drop one note in every cycle).
		if (chance(0.3f)) {
			for (size_t j = 0; j < plan.size(); ++j) {
				if (!publishesPitch(plan[j])) continue;
				vp.inputs.push_back(Edge(j, Edge::Operand, Edge::kOpVelocity));
				break;
			}
		}
		plan.push_back(vp);
	}

	// Rounds of harmony/gate voices attached
	// to anything that publishes a pitch. Round 0 attaches to leads; later
	// rounds attach to publishers created in earlier rounds, so coupling
	// depth accumulates. Gates narrow when chained (review Risk 2), so only
	// round 0 makes them — harmonies carry the deeper chains.
	// Coupling depth scales with selection area - small
	// selections stay at one dependent round, large ones reach three. (Risk
	// 2 watch: gates only spawn in round 0, so deep chains are Harmony-only
	// and do not thin out the way stacked gates would.)
	int const rounds = (w * h) < 700 ? 1 : ((w * h) < 2400 ? 2 : 3);
	for (int round = 0; round < rounds && (int)plan.size() < capacity; ++round) {
		size_t const upTo = plan.size(); // snapshot: don't chain within a round
		for (size_t i = 0; i < upTo && (int)plan.size() < capacity; ++i) {
			if (!publishesPitch(plan[i])) continue;
			// Risk 3: stop planning publishers once the variable pool runs
			// low, instead of planning voices that silently drop out. Round 0
			// keeps legacy behaviour (the pool may legitimately be unmanaged
			// when planArrangement is driven standalone).
			if (round >= 1 && varPool_.size() < 4) break;
			if (!chance(round == 0 ? 0.65f : 0.4f)) continue;

			// Occasionally the new node is a
			// Gate playing THIS publisher's pitch, gated on a DIFFERENT
			// publisher hitting one of its notes.
			size_t trigFrom = 0;
			char trigNote = 0;
			bool foundTrig = false;
			if (round >= 1 && chance(0.35f)) {
				for (size_t j = 0; j < upTo; ++j) {
					if (j == i || !publishesPitch(plan[j])) continue;
					if (plan[j].notes.empty()) continue;
					trigFrom = j;
					trigNote = plan[j].notes[randInt(0, (int)plan[j].notes.size() - 1)];
					foundTrig = true;
					break;
				}
			}

			VoiceNode vp;
			vp.channel = randomChannel();
			// `i` is an ABSOLUTE plan index in every round (the bus node
			// included in the iteration), so the edge target needs no offset.
			// Applying the round-0 +1 bus offset here created self-edges when
			// the last lead spawned a dependent - a cycle the topo sort
			// correctly rejected.
			size_t const from = i;
			if (foundTrig) {
				vp.role = VoiceNode::Gate;
				vp.param = trigNote;
				vp.inputs.push_back(Edge(from, Edge::Pitch, '0'));
				vp.inputs.push_back(Edge(trigFrom, Edge::Trigger, trigNote));
			}
			else {
				vp.role = (round == 0 && chance(0.5f)) ? VoiceNode::Gate : VoiceNode::Harmony;
				vp.param = vp.role == VoiceNode::Gate
					? plan[from].notes[randInt(0, (int)plan[from].notes.size() - 1)]
					: (char)('0' + randInt(1, 3));
				vp.inputs.push_back(Edge(from,
					vp.role == VoiceNode::Gate ? Edge::Trigger : Edge::Pitch,
					vp.param));
			}
			// Occasionally hang a Modulation edge
			// off the Pitch this voice already consumes — layout turns it
			// into a '!'-operator CC tail below the voice. This adds an edge
			// to an existing node rather than a new one: no plan capacity,
			// no channel, only a control number from the per-call pool (the
			// producer's published variable is reused, so no var either).
			bool hasPitch = false;
			for (Edge const& e : vp.inputs) {
				if (e.kind == Edge::Pitch) hasPitch = true;
			}
			if (hasPitch && chance(0.35f)) {
				char cc = allocateCcNumber();
				if (cc != 0) vp.inputs.push_back(Edge(from, Edge::Modulation, cc));
			}
			plan.push_back(vp);
		}
	}

	// Rhythm section: tuned percussion hits drawn from the patch scale.
	size_t const nDrums = w >= 24 ? 2 : 1;
	for (size_t i = 0; i < nDrums && (int)plan.size() < capacity; ++i) {
		VoiceNode vp;
		vp.role = VoiceNode::Drums;
		plan.push_back(vp);
	}

	// Texture fills the remaining capacity.
	while ((int)plan.size() < capacity) {
		VoiceNode vp;
		switch (randInt(0, 2)) {
			case 0: vp.role = VoiceNode::Delay; break;
			case 1: vp.role = VoiceNode::Uclid; break;
			default: vp.role = VoiceNode::Chord; break;
		}
		// §4.1: occasionally a texture hit's velocity follows a publisher's
		// pitch — accents that track the melody. Same source rules as the
		// lead case above; the K row costs no extra space in these builders.
		if (chance(0.3f)) {
			size_t pubs[8];
			int np = 0;
			for (size_t j = 0; j < plan.size() && np < 8; ++j)
				if (publishesPitch(plan[j])) pubs[np++] = j;
			if (np > 0) vp.inputs.push_back(Edge(pubs[randInt(0, np - 1)], Edge::Operand, Edge::kOpVelocity));
		}
		plan.push_back(vp);
	}

	return plan;
}



void AhabGenerator::generateArrangement(ScratchPad& buf, Usz y, Usz x, Usz h, Usz w, float density) {
	// Identity + bus. The bus is packed by the same skyline as every other
	// voice — usually the top-left corner — and the voices reading it share
	// its band to the right: ORCA scans left-to-right, so a same-row write
	// left of a read is visible. Reserving a full band for ~15 columns of
	// clocks stranded the entire top-right of wide selections.
	size_t const busUnits = std::min<Usz>(4, (w + 1) / 5);
	bool const busPlaced = h >= 2 && w >= 8 && density > 0.2f;
	size_t const nBus = busPlaced ? busUnits : 0;

	std::vector<VoiceNode> plan = planArrangement(h, w, density, nBus);
	if (plan.empty()) return;

	// Topological layout. Every reader of a PITCH variable is placed below
	// the writer; the only exception is the clock bus, whose writes are
	// visible same-row-left of its readers (scan order).
	Usz const top = y;
	// Density buys breathing room as well as count: the sparser the choice,
	// the wider the margin the packer reserves around every voice, so the
	// few blocks of a Sparse take spread across the whole selection instead
	// of clustering in one corner. Packed (100%) keeps zero margins.
	Usz const gapX = (Usz)((1.f - density) * 6.f);
	Usz const gapY = (Usz)((1.f - density) * 3.f);

	auto availH = [&](Usz cy) { return std::min(h > cy ? h - cy : (Usz)0, (Usz)8); };
	auto availW = [&](Usz cx) { return std::min(x + w > cx ? x + w - cx : (Usz)0, (Usz)16); };

	// One case per role, each calling the same builder its zone did before.
	auto placeVoice = [&](VoiceNode& vn, char srcVar, char velVar, Usz cy, Usz cx, Usz avH, Usz avW) -> Extent {
		switch (vn.role) {
			case VoiceNode::Lead: {
				char sharedVar = 0;
				for (Edge const& e : vn.inputs)
					if (e.kind == Edge::Clock && (size_t)e.param < busVars_.size())
						sharedVar = busVars_[(size_t)e.param];
				char var = 0;
				Extent e = avH >= 5
					? placeArpeggioVoice(buf, cy, cx, avH, avW, vn.channel, vn.notes, sharedVar, &var, velVar)
					: Extent();
				vn.publishedVar = var;
				return e;
			}
			case VoiceNode::Harmony:
			case VoiceNode::Gate: {
				bool const gate = vn.role == VoiceNode::Gate;
				if (srcVar == 0) return Extent();
				if (gate) {
					// A second Trigger edge gates this voice on another
					// publisher's pitch.
					char srcB = 0, parB = 0;
					for (Edge const& e : vn.inputs)
						if (e.kind == Edge::Trigger && plan[e.from].publishedVar != 0) {
							srcB = plan[e.from].publishedVar;
							parB = e.param;
					}
					bool const fanIn = srcB != 0;
					// Both gate layouts span 12 columns; the
					// fan-in merge adds four rows on top of the single gate.
					Usz const needH = fanIn ? 7 : 3;
					Usz const needW = 12;
					if (avH < needH || avW < needW) return Extent();
					Extent e = placeDerivedVoice(buf, cy, cx, avH, avW,
						vn.channel, srcVar, true, vn.param, nullptr, srcB, parB);
					if (e.empty() == false) ++derivedPlaced_;
					return e;
				}
				// Harmony publishes when there is room for the extra V-write
				// row, so chains can attach to this voice.
				char pubVar = 0;
				Extent e;
				if (avH >= 5) {
					e = placeDerivedVoice(buf, cy, cx, avH, avW, vn.channel, srcVar, false, vn.param, &pubVar);
				}
				else if (avH >= 3) {
					e = placeDerivedVoice(buf, cy, cx, avH, avW, vn.channel, srcVar, false, vn.param);
				}
				else {
					return Extent();
				}
				vn.publishedVar = pubVar;
				if (!e.empty()) ++derivedPlaced_;
				return e;
			}
			case VoiceNode::Drums:
				return avH >= 2 ? generateDrumVoice(buf, cy, cx, avH, avW) : Extent();
			case VoiceNode::Delay: {
				std::string note;
				fillPatchWalk(note, 1); // in the patch key
				return avH >= 2
					? placeDelayMidiVoice(buf, cy, cx, avH, avW,
										  randomSmallDigit(), barDivisorMod(),
										  vn.channel, randomOctave(), note[0], velVar)
					: Extent();
			}
			case VoiceNode::Uclid: {
				char uSteps, uMax;
				randomEuclid(uSteps, uMax);
				std::string note;
				fillPatchWalk(note, 1); // in the patch key
				return avH >= 2
					? placeUclidMidiVoice(buf, cy, cx, avH, avW,
										  uSteps, uMax, vn.channel, randomOctave(), note[0], velVar)
					: Extent();
			}
			case VoiceNode::Chord: {
				std::string tones;
				fillPatchWalk(tones, 3 + randInt(0, 1)); // triad-ish, in the patch key
				return placeChordVoice(buf, cy, cx, avH, avW,
									   randomSmallDigit(), barDivisorMod(), vn.channel, randomOctave(),
									   std::vector<char>(tones.begin(), tones.end()), velVar);
			}
			default: {
				return Extent();
			}
		}
	};

	// Skyline packing. The shelf packer before it advanced one cursor left
	// to right and only moved down on wrap, so the space UNDER a short
	// voice — a 2-row texture beside a 5-row lead strands three rows for
	// the rest of the band — stayed blank no matter how many voices were
	// planned. sky[c] tracks the first free row of selection column c; each
	// voice takes the SHALLOWEST column run that clears the skyline and its
	// producers' bottoms, and later voices reclaim exactly those under-band
	// gaps. Processing stays in plan-index order (already topological:
	// every edge points to an earlier node), so leads claim the top first,
	// dependents slot below their producers, texture backfills the rest.
	std::vector<Usz> sky(w, top);
	std::vector<Usz> placedBottom(plan.size(), h); // sentinel: not yet placed

	for (size_t i = 0; i < plan.size(); ++i) {
		VoiceNode& vn = plan[i];

		// A voice must start below the content bottom of every producer whose
		// variables it reads (producers precede it in plan order). For the
		// bus that "bottom" is its WRITE row (see placement below): readers
		// of the bus may share its band, but only at or below the row the
		// write happens on.
		Usz minY = top;
		for (Edge const& e : vn.inputs) {
			minY = std::max(minY, placedBottom[e.from]);
		}

		// Only the Pitch edge feeds the note source; a Gate resolves its own
		// Trigger edges inside placeVoice. An Operand edge feeds the velocity
		// cell from another publisher's pitch — falling back to the literal
		// velocity when it never published.
		char srcVar = 0;
		for (Edge const& e : vn.inputs) {
			if (e.kind == Edge::Pitch) {
				srcVar = plan[e.from].publishedVar;
			}
		}
		char velVar = 0;
		for (Edge const& e : vn.inputs) {
			if (e.kind == Edge::Operand && e.param == Edge::kOpVelocity) {
				velVar = plan[e.from].publishedVar;
			}
		}

		// Upper-bound footprint per role/state, used only to choose the
		// position; builders return their real extent, which drives the
		// skyline update below.
		Usz eh = 2, ew = 9; // drums / delay / uclid
		switch (vn.role) {
			case VoiceNode::Bus:
				eh = 2; ew = nBus * 5 - 1; break;
			case VoiceNode::Lead: eh = 5; ew = 13; break;
			case VoiceNode::Harmony: eh = 5; ew = 8; break;
			case VoiceNode::Gate: {
				ew = 12;
				// Fan-in needs BOTH a Pitch and a Trigger edge; publishedVar is
				// not known at planning time, so it cannot be consulted here.
				bool hasP = false, hasT = false;
				for (Edge const& e : vn.inputs) {
					hasP |= e.kind == Edge::Pitch;
					hasT |= e.kind == Edge::Trigger;
				}
				eh = hasP && hasT ? 7 : 3;
				break;
			}
			case VoiceNode::Chord: eh = 8; break;
			default: break;
		}

		// Shallowest-fitting column run would stack everything at the top; the
		// point is COVERAGE. Map plan order onto depth — leads aim high, their
		// dependents middle, drums/textures progressively deeper — and pick the
		// column run whose required row lands closest to that target while
		// clearing producers AND skyline. Scanning left to right with a strict
		// compare keeps the leftmost of equals.
		float const frac = plan.size() > 1 ? (float)i / (float)(plan.size() - 1) : 0.f;
		Usz const targetY = top + (Usz)(frac * (float)(h - 1 - top));
		// The scanned run includes the horizontal margin, so a follower cannot
		// start closer than gapX columns to this voice's right edge.
		Usz const ewRun = std::min(ew + gapX, w);
		bool found = false;
		Usz bestY = h, bestX = x;
		Usz bestDist = h;
		for (Usz cx = x; cx + ewRun <= x + w; ++cx) {
			Usz req = minY;
			for (Usz dx = 0; dx < ewRun; ++dx)
				req = std::max(req, sky[cx - x + dx]);
			if (req + eh > h) continue;
			Usz const dist = req > targetY ? req - targetY : targetY - req;
			if (!found || dist < bestDist) { bestDist = dist; bestY = req; bestX = cx; found = true; }
		}
		if (!found) continue; // genuinely no room: dropped, consumers degrade

		Extent e;
		bool const isBus = vn.role == VoiceNode::Bus;
		if (isBus) {
			e = generateClockBus(buf, bestY, bestX, eh, ew + 1);
		}
		else {
			Usz const avH = availH(bestY);
			e = placeVoice(vn, srcVar, velVar, bestY, bestX, avH, availW(bestX));

			// A Modulation edge hangs a '!'-operator CC tail directly below the
			// voice, mapping its producer's published pitch onto a control change
			// on THIS voice's channel. Decorative by design: skipped silently
			// when the producer never published or there is no room below —
			// never at the cost of notes.
			if (!e.empty()) {
				for (Edge const& en : vn.inputs) {
					if (en.kind != Edge::Modulation) continue;
					char const ccVar = plan[en.from].publishedVar;
					if (ccVar == 0 || e.h + 2 > avH) continue;
					Extent t = placeModulationTail(buf, bestY + e.h, bestX, avH - e.h,
												   availW(bestX), vn.channel, en.param, ccVar);
					if (!t.empty()) e = Extent(e.h + t.h, std::max(e.w, t.w));
					break;
				}
			}
		}
		if (e.empty()) continue;

		placedBottom[i] = bestY + e.h;
		if (isBus) {
			// Readers of the bus may share its band, but their reads must not
			// precede the WRITE row (it is the bus's SECOND row): for minY
			// purposes the bus ends there, so bus-reading voices start at or
			// below the write row — same-row-right of the write is visible.
			placedBottom[i] = bestY + e.h - 1;
		}
		if (!e.empty()) {
			// Raise the skyline over the real footprint PLUS the density
			// margin (vertically and horizontally). No unconditional separator:
			// every builder keeps all of its pokes inside its own extent (bang
			// rows are part of it; no V-read sits on a last row), so even a
			// zero-margin follower is never clobbered — its top row is
			// uppercase everywhere, so a stray '*' above cannot misfire it.
			for (Usz dx = 0; dx < e.w + gapX && bestX - x + dx < w; ++dx) {
				Usz& s = sky[bestX - x + dx];
				s = std::max(s, bestY + e.h + gapY);
			}
		}
	}
}

AhabGenerator::Extent AhabGenerator::generateClockBus(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW) {
	if (maxH < 2 || maxW < 5) return {};

	// Several clock divisions at the very top of the arrangement,
	// each publishing its counter into a reserved variable (q, w, e, ...).
	// Unit layout at column cx (4 wide, stride 5):
	//   row 0: . r C m   C at cx+2, output lands at (y+1, cx+2)
	//   row 1: v V .     name@cx selects write mode; value@cx+2 is exactly
	//                    where the clock pokes — the old .vV. layout stored a
	//                    literal '.' into the var forever.
	// Writers sit above their readers: vars are wiped every tick and only
	// visible below/right, and voices start 3 rows down.
	static char const kBusVars[] = "qwer";
	int const maxUnits = (int)std::min<Usz>(4, (maxW + 1) / 5);
	if (maxUnits <= 0) return {};

	// Distinct bar-divisor mods per unit, starting at a random offset.
	Usz divs[4];
	int nd = 0;
	for (Usz d = 2; d <= barMod_; ++d) {
		if (barMod_ % d == 0 && nd < 4) divs[nd++] = d;
	}
	int start = nd ? randInt(0, nd - 1) : 0;

	busVars_.clear();
	for (int i = 0; i < maxUnits; ++i) {
		Usz cx = x + (Usz)i * 5;
		char var = kBusVars[i];
		char mod = b36Char(nd ? divs[(start + i) % nd] : barMod_);

		buf.set(y, cx,   '.');
		buf.set(y, cx+1, randomSmallDigit());
		buf.set(y, cx+2, 'C');
		buf.set(y, cx+3, mod);

		buf.set(y+1, cx,   var);
		buf.set(y+1, cx+1, 'V');
		buf.set(y+1, cx+2, '.');

		busVars_ += var;
	}

	return Extent{2, x + (Usz)maxUnits * 5 - 1 - x}; // rows; width covered
}

AhabGenerator::Extent AhabGenerator::generateDrumVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW) {
	if (maxH < 2 || maxW < 9) return {};

	// Euclidean pattern with a varied modulus
	char steps, eMax;
	randomEuclid(steps, eMax);
	
	// Row 0: Uclid - .sUm (U at x+2 pokes its '*' to (y+1, x+2))
	buf.set(y, x, '.');
	buf.set(y, x+1, steps);
	buf.set(y, x+2, 'U');
	buf.set(y, x+3, eMax);
	
	// Row 1: MIDI one column right of the bang column
	buf.set(y+1, x+3, ':');
	buf.set(y+1, x+4, '9');  // MIDI channel 10 (drums)
	buf.set(y+1, x+5, '2');  // Octave 2
	{
		std::string hit;
		fillPatchWalk(hit, 1); // tuned percussion, in the patch key
		buf.set(y+1, x+6, hit[0]);
	}
	buf.set(y+1, x+7, 'f');  // High velocity
	buf.set(y+1, x+8, '1');  // Short duration
	
	return Extent{2, 9};
}

// Pattern building blocks implementation

AhabGenerator::Extent AhabGenerator::placeClockPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod) {
	if (maxW < 4) return {};
	// .rCm format
	buf.set(y, x, '.');
	buf.set(y, x+1, rate);
	buf.set(y, x+2, 'C');
	buf.set(y, x+3, mod);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeDelayPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char rate, char mod) {
	if (maxW < 4) return {};
	// .rDm format
	buf.set(y, x, '.');
	buf.set(y, x+1, rate);
	buf.set(y, x+2, 'D');
	buf.set(y, x+3, mod);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeTrackPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char key, const std::string& values) {
	Usz len = values.size();
	if (maxW < 4 + len) return {};
	
	// .keyTlen_values format: ..kT + values
	buf.set(y, x, '.');
	buf.set(y, x+1, key);
	char lenChar = (len < 10) ? '0' + len : 'a' + (len - 10);
	buf.set(y, x+2, lenChar);
	buf.set(y, x+3, 'T');
	
	for (Usz i = 0; i < len; ++i) {
		buf.set(y, x+4+i, values[i]);
	}
	
	return Extent{1, 4 + len};
}

AhabGenerator::Extent AhabGenerator::placeUclidPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char steps, char max) {
	if (maxW < 4) return {};
	// .sUm format
	buf.set(y, x, '.');
	buf.set(y, x+1, steps);
	buf.set(y, x+2, 'U');
	buf.set(y, x+3, max);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeMidiPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, 
									  char channel, char octave, char note, char vel, char len) {
	if (maxW < 6) return {};
	// :cOnvl format (colon operator for MIDI)
	buf.set(y, x, ':');
	buf.set(y, x+1, channel);
	buf.set(y, x+2, octave);
	buf.set(y, x+3, note);
	buf.set(y, x+4, vel);
	buf.set(y, x+5, len);
	return Extent{1, 6};
}

AhabGenerator::Extent AhabGenerator::placeVarWrite(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name, char value) {
	if (maxW < 4) return {};
	// '.' as the value would store a literal '.' into the variable forever
	// (review §1.3): pass a real initial value even if a producer above
	// overwrites the cell every tick.
	assert(value != '.' && "V-write with '.' value is a permanent no-op store");
	// .nVv format
	buf.set(y, x, '.');
	buf.set(y, x+1, name);
	buf.set(y, x+2, 'V');
	buf.set(y, x+3, value);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeVarRead(ScratchPad& buf, Usz y, Usz x, Usz maxW, char name) {
	// Writes 4 cells (..Vn), so require 4 columns of budget (review §1.4)
	if (maxW < 4) return {};
	// ..Vn format (read variable, output below)
	buf.set(y, x, '.');
	buf.set(y, x+1, '.');
	buf.set(y, x+2, 'V');
	buf.set(y, x+3, name);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeRandomPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char min, char max) {
	if (maxW < 4) return {};
	// .mRx format
	buf.set(y, x, '.');
	buf.set(y, x+1, min);
	buf.set(y, x+2, 'R');
	buf.set(y, x+3, max);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeIncrementPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char step, char mod) {
	if (maxW < 4) return {};
	// .sIm format
	buf.set(y, x, '.');
	buf.set(y, x+1, step);
	buf.set(y, x+2, 'I');
	buf.set(y, x+3, mod);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeAddPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b) {
	if (maxW < 4) return {};
	// .aAb format
	buf.set(y, x, '.');
	buf.set(y, x+1, a);
	buf.set(y, x+2, 'A');
	buf.set(y, x+3, b);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeMultiplyPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b) {
	if (maxW < 4) return {};
	// .aMb format
	buf.set(y, x, '.');
	buf.set(y, x+1, a);
	buf.set(y, x+2, 'M');
	buf.set(y, x+3, b);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeIfPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char a, char b) {
	if (maxW < 4) return {};
	// .aFb format
	buf.set(y, x, '.');
	buf.set(y, x+1, a);
	buf.set(y, x+2, 'F');
	buf.set(y, x+3, b);
	return Extent{1, 4};
}

AhabGenerator::Extent AhabGenerator::placeHaltPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW) {
	if (maxW < 1) return {};
	buf.set(y, x, 'H');
	return Extent{1, 1};
}

AhabGenerator::Extent AhabGenerator::placeOffsetPattern(ScratchPad& buf, Usz y, Usz x, Usz maxW, char offX, char offY) {
	if (maxW < 4) return {};
	// .y x O format — O reads its x operand from (0,-1) and y from (0,-2), so
	// the cell next to 'O' is offX and the one before it is offY (review §1.5)
	buf.set(y, x, '.');
	buf.set(y, x+1, offY);
	buf.set(y, x+2, offX);
	buf.set(y, x+3, 'O');
	return Extent{1, 4};
}

// Compound pattern implementations

AhabGenerator::Extent AhabGenerator::placeDelayMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										   char rate, char mod, char channel, char octave, char note,
										   char velocityVar) {
	// Needs 9 columns now: '.' r D m  +  ':' c o n v l starting at x+3
	if (maxH < 2 || maxW < 9) return {};
	// Row 0: .rDm — D at x+2 pokes its '*' output to (y+1, x+2)
	placeDelayPattern(buf, y, x, maxW, rate, mod);
	// §4.1: with a velocity source, a K row pokes it into the velocity cell
	// below — same footprint, one live operand instead of a fixed 'f'.
	if (velocityVar != 0) {
		buf.set(y, x + 5, '1');      // K length: velocity only
		buf.set(y, x + 6, 'K');      // pokes (y+1, x+7)
		buf.set(y, x + 7, velocityVar);
	}
	// Row 1: ':' at x+3 is the right-hand neighbour of the bang column, so
	// oper_has_neighboring_bang() sees it. (A ':' ON the bang cell would be
	// overwritten by the '*')
	buf.set(y+1, x+3, ':');
	buf.set(y+1, x+4, channel);
	buf.set(y+1, x+5, octave);
	buf.set(y+1, x+6, note);
	buf.set(y+1, x+7, 'f'); // velocity (non-zero: 0 is a note-off; K-fed when sourced)
	buf.set(y+1, x+8, '4'); // length
	return Extent{2, 9};
}

AhabGenerator::Extent AhabGenerator::placeUclidMidiVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										   char steps, char max, char channel, char octave, char note,
										   char velocityVar) {
	// Same alignment as placeDelayMidiVoice: U at x+2, ':' at x+3
	if (maxH < 2 || maxW < 9) return {};
	// Row 0: .sUm — U at x+2 pokes its '*' output to (y+1, x+2)
	placeUclidPattern(buf, y, x, maxW, steps, max);
	// §4.1: with a velocity source, a K row pokes it into the velocity cell
	// below — same footprint, one live operand instead of a fixed 'c'.
	if (velocityVar != 0) {
		buf.set(y, x + 5, '1');      // K length: velocity only
		buf.set(y, x + 6, 'K');      // pokes (y+1, x+7)
		buf.set(y, x + 7, velocityVar);
	}
	// Row 1: MIDI one column right of the bang column
	buf.set(y+1, x+3, ':');
	buf.set(y+1, x+4, channel);
	buf.set(y+1, x+5, octave);
	buf.set(y+1, x+6, note);
	buf.set(y+1, x+7, 'c'); // velocity (K-fed when sourced)
	buf.set(y+1, x+8, '2'); // length
	return Extent{2, 9};
}

AhabGenerator::Extent AhabGenerator::placeArpeggioVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
										  char channel, const std::string& notes, char sharedVar, char* allocatedVar,
										  char velocityVar) {
	Usz noteLen = notes.size();
	// 5 rows: clock / track / V-write / bang+V-read / MIDI. Width: the ':'
	// block spans x..x+8; track values extend to x+5+noteLen-1.
	if (maxH < 5 || maxW < std::max((Usz)9, 5 + noteLen)) return {};

	char varName = allocateVarName();
	if (!varName) return {}; // variable pool exhausted
	if (allocatedVar) *allocatedVar = varName;

	char mod = (noteLen < 10) ? '0' + noteLen : 'a' + (noteLen - 10);
	char octave = randomOctave();
	char bangRate = randomSmallDigit();

	// Anchor every column off the ':' so operand offsets stay explicit — bare
	// x+2/x+3 literals are exactly how review §1's off-by-ones hid.
	const Usz colMidi = x + 3;       // ':' itself
	const Usz colNote = colMidi + 3; // channel +1, octave +2, note +3

	// Row 0 drives the track KEY slot at (y+1, x+2): either the voice's own
	// clock (.rCm, output lands exactly on the key cell), or a read of a
	// clock-bus division published above.
	if (sharedVar) {
		buf.set(y, x,   '.');
		buf.set(y, x+1, '.');
		buf.set(y, x+2, 'V'); // read mode; output lands at (y+1, x+2)
		buf.set(y, x+3, sharedVar);
	}
	else {
		placeClockPattern(buf, y, x, maxW, randomSmallDigit(), mod);
	}

	// Row 1: track — T at x+4 reads key at -2 (= x+2, fed by the clock above)
	// and len at -1 (= x+3); values start at +1 (= x+5). Its output pokes
	// (y+2, x+4).
	buf.set(y+1, x+1, '.');
	buf.set(y+1, x+2, '.');          // key slot, written by the clock each tick
	buf.set(y+1, x+3, mod);          // len == number of notes
	buf.set(y+1, x+4, 'T');
	for (Usz i = 0; i < noteLen; ++i) {
		buf.set(y+1, x+5+i, notes[i]);
	}

	// Row 2: V-write — left operand varName selects write mode; the right
	// operand (y+2, x+4) is exactly where the track output lands.
	buf.set(y+2, x+2, varName);
	buf.set(y+2, x+3, 'V');
	buf.set(y+2, x+4, '.');          // value slot, poked by the track each tick

	// Row 3: bang source (D pokes '*' to (y+4, x+2)) plus the read that fills
	// the note cell. With a velocity source (§4.1), a K (konkat) row replaces
	// the V-read: it reads TWO variable names and pokes their values into the
	// note AND velocity cells below — exactly the V-read's footprint, one
	// more live operand. The velocity cell keeps its literal as a prefill:
	// K skips '.' names without writing, so no stale glyph can linger.
	buf.set(y+3, x+1, bangRate);
	buf.set(y+3, x+2, 'D');
	buf.set(y+3, x+3, '2');
	if (velocityVar != 0) {
		buf.set(y+3, x+4, '2');      // K length: note + velocity
		buf.set(y+3, x+5, 'K');      // pokes (y+4, colNote) and (y+4, colNote+1)
		buf.set(y+3, x+6, varName);
		buf.set(y+3, x+7, velocityVar);
	}
	else {
		buf.set(y+3, colNote - 1, '.');
		buf.set(y+3, colNote,     'V');
		buf.set(y+3, colNote + 1, varName);
	}

	// Row 4: ':' — banged from the left, note cell filled by the V-read/K above.
	buf.set(y+4, colMidi,     ':');
	buf.set(y+4, colMidi + 1, channel);
	buf.set(y+4, colMidi + 2, octave);
	buf.set(y+4, colNote,     '.');  // note cell, poked by the V-read
	buf.set(y+4, colMidi + 4, 'f');  // velocity (non-zero: 0 is a note-off)
	buf.set(y+4, colMidi + 5, '4');  // length

	return Extent{5, std::max((Usz)9, 5 + noteLen)};
}

AhabGenerator::Extent AhabGenerator::placeChordVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
													   char rate, char mod, char channel, char octave,
													   const std::vector<char>& notes, char velocityVar) {
	// A delay pokes exactly ONE cell, so the old single-delay fan-out left
	// chord notes 2..N without a bang neighbour and permanently silent.
	// Fix: N stacked delay+MIDI pairs with IDENTICAL rate/mod - D fires when
	// Tick%(rate*mod)==0 regardless of position, so every note bang lands on
	// the same tick: a true simultaneous chord.
	Usz const n = notes.size();
	if (n == 0 || maxH < 2 * n || maxW < 9) return {};

	for (Usz i = 0; i < n; ++i) {
		Usz ry = y + 2 * i;
		placeDelayPattern(buf, ry, x, maxW, rate, mod); // D at x+2
		// §4.1: with a velocity source, a K row feeds every chord note's
		// velocity cell from one publisher — accents on whole stacks.
		if (velocityVar != 0) {
			buf.set(ry, x + 5, '1');     // K length: velocity only
			buf.set(ry, x + 6, 'K');     // pokes (ry+1, x+7)
			buf.set(ry, x + 7, velocityVar);
		}
		buf.set(ry + 1, x + 3, ':');                    // right of the bang column
		buf.set(ry + 1, x + 4, channel);
		buf.set(ry + 1, x + 5, octave);
		buf.set(ry + 1, x + 6, notes[i]);
		buf.set(ry + 1, x + 7, 'c');                    // velocity (K-fed when sourced)
		buf.set(ry + 1, x + 8, '8');                    // length
	}

	return Extent{2 * n, 9};
}

AhabGenerator::Extent AhabGenerator::placeDerivedVoice(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
														 char channel, char sourceVar, bool gate, char param,
														 char* allocatedVar,
														 char sourceVarB, char paramB) {
	// Columns anchor on colV (the read's V).
	// The source variable MUST be written above this voice (vars are
	// within-tick). Requires maxH >= 3; a harmony that PUBLISHES
	// (allocatedVar != null) needs 5 rows for the extra V-write row.
	bool const publish = allocatedVar != nullptr && !gate;
	bool const fanIn = gate && sourceVarB != 0;
	if (maxH < (fanIn ? 7 : 3)) return {};
	Usz const needed = fanIn ? 12 : (gate ? 12 : 8);
	if (maxW < needed) return {};
	if (publish && (maxH < 5 || maxW < 8)) return {};

	char v = 0;
	if (publish) {
		v = allocateVarName();
		if (v == 0) return {}; // pool exhausted: skip rather than degrade
		*allocatedVar = v;
	}

	Usz const colV = x + (publish ? 5 : 4);
	char octave = randomOctave();

	// Row 0: read the lead pitch — '.' left of V selects read mode; the
	// output pokes (y+1, colV).
	buf.set(y, colV - 1, '.');
	buf.set(y, colV,     'V');
	buf.set(y, colV + 1, sourceVar);

	if (!gate) {
		if (!publish) {
			// Harmony: 'A' adds param letter-steps to the lead pitch (C+2=E in
			// base36 letter space). Its left operand is the poked cell
			// (y+1, colV); output lands at (y+2, colV+1) == the note cell below.
			buf.set(y+1, colV + 1, 'A');
			buf.set(y+1, colV + 2, param);

			// Bang: D at (y+1, colV-3) pokes '*' to (y+2, colV-3), the ':'
			// operator's left neighbour.
			buf.set(y+1, colV - 4, randomSmallDigit());
			buf.set(y+1, colV - 3, 'D');
			buf.set(y+1, colV - 2, '2');

			// MIDI row: note cell at colV+1 is fed by the A output.
			buf.set(y+2, colV - 2, ':');
			buf.set(y+2, colV - 1, channel);
			buf.set(y+2, colV,     octave);
			buf.set(y+2, colV + 1, '.');      // note, poked by the A each tick
			buf.set(y+2, colV + 2, 'f');      // velocity
			buf.set(y+2, colV + 3, '4');      // length

			return Extent{3, 8};
		}

		// Publishing harmony: same chain, plus a V-write
		// on row y+2 whose VALUE cell (y+2, colV+1) is exactly where the A
		// pokes — so var v holds the transposed pitch every tick, and this
		// voice can itself be a Pitch source for chains.
		//
		//   r0: . V s        read src -> poke (r1, c)
		//   r1: A amt        A reads (r1,c), pokes (r2, c+1)
		//   r2: v V .        write vars[v] = transposed pitch
		//   r3: . V v  D     read v -> poke (r4, c); D pokes (r4, c-4)
		//   r4: * : c o n f l  banged from the left, note cell fed by the read
		Usz const c = colV;

		// Row 0: read the source pitch.
		buf.set(y,   c - 1, '.');
		buf.set(y,   c,     'V');
		buf.set(y,   c + 1, sourceVar);

		// Row 1: transpose; bang source for the MIDI row below.
		buf.set(y+1, c - 5, randomSmallDigit());
		buf.set(y+1, c - 4, 'D');
		buf.set(y+1, c - 3, '2');
		buf.set(y+1, c + 1, 'A');
		buf.set(y+1, c + 2, param);

		// Row 2: publish the transposed pitch into v.
		buf.set(y+2, c - 1, v);
		buf.set(y+2, c,     'V');
		buf.set(y+2, c + 1, '.');         // value cell, poked by the A each tick

		// Row 3: read v back; bang source for the MIDI row below.
		buf.set(y+3, c - 1, '.');
		buf.set(y+3, c,     'V');
		buf.set(y+3, c + 1, v);
		buf.set(y+3, c - 5, randomSmallDigit());
		buf.set(y+3, c - 4, 'D');
		buf.set(y+3, c - 3, '2');

		// Row 4: ':' banged from the left, note cell fed by the read above.
		buf.set(y+4, c - 4, '*');         // poked by the D each period
		buf.set(y+4, c - 3, ':');
		buf.set(y+4, c - 2, channel);
		buf.set(y+4, c - 1, octave);
		buf.set(y+4, c,     '.');         // note, poked by the V-read each tick
		buf.set(y+4, c + 1, 'f');         // velocity
		buf.set(y+4, c + 2, '4');         // length

		return Extent{5, 8};
	}

	if (sourceVarB != 0) {
		// This voice sounds only when BOTH
		// conditions hold — the lead pitch equals param AND the second
		// publisher's pitch equals paramB. The two compares are merged
		// exactly, and every operator in the chain is uppercase, so each
		// cell is refreshed EVERY tick — no stale '*' can linger:
		//
		//   r0:  .  V  sA                      read lead pitch -> (r1,x+4)
		//   r1:           F  p                 oA = '*' iff lead == param
		//   r2:  .  V  sB       oA             read 2nd pitch; oA lands here
		//   r3:        F  q  J                 oB = '*' iff 2nd == paramB;
		//   r4:      oB L  oA                  L: '.' if either input is '.',
		//                              else min. '*' has index 0, so two
		//                              stars min to index 0 -> glyph '0'
		//   r5:         F  0                   '*' iff the merge saw two stars
		//   r6:         :  c  o  n  f  4       banged by that F on its left
		//
		// (Columns relative to x; the lead read on r0 was emitted above.)
		buf.set(y + 2, x + 1, '.');
		buf.set(y + 2, x + 2, 'V');
		buf.set(y + 2, x + 3, sourceVarB);

		buf.set(y + 1, x + 5, 'F'); // left operand: lead pitch poked at (y+1,x+4)
		buf.set(y + 1, x + 6, param);
		// (y+2,x+5) stays empty: the lead F above pokes its '*'/'.' there.

		buf.set(y + 3, x + 3, 'F'); // left operand: pitch poked at (y+3,x+2)
		buf.set(y + 3, x + 4, paramB);
		buf.set(y + 3, x + 5, 'J'); // reads oA above, relays it down one row

		buf.set(y + 4, x + 4, 'L');

		buf.set(y + 5, x + 5, 'F');
		buf.set(y + 5, x + 6, '0');

		buf.set(y + 6, x + 6, ':');
		buf.set(y + 6, x + 7, channel);
		buf.set(y + 6, x + 8, octave);
		buf.set(y + 6, x + 9, randomNote()); // its own note, doubly gated
		buf.set(y + 6, x + 10, 'f');
		buf.set(y + 6, x + 11, '4');

		return Extent{7, 12};
	}

	// Call-and-response gate: 'F' compares the lead pitch with param and
	// pokes '*' (bang) or '.' to (y+2, colV+1) — the ':'s left neighbour,
	// so this voice sounds ONLY when the lead hits that exact pitch.
	buf.set(y+1, colV + 1, 'F');
	buf.set(y+1, colV + 2, param);

	buf.set(y+2, colV + 2, ':');
	buf.set(y+2, colV + 3, channel);
	buf.set(y+2, colV + 4, octave);
	buf.set(y+2, colV + 5, randomNote()); // its own note, gated
	buf.set(y+2, colV + 6, 'f');
	buf.set(y+2, colV + 7, '4');

	return Extent{3, 12};
}

AhabGenerator::Extent AhabGenerator::placeModulationTail(ScratchPad& buf, Usz y, Usz x, Usz maxH, Usz maxW,
														   char channel, char control, char sourceVar) {
	// Two rows hung below the voice they express;
	// the producer's V-write must sit above (vars are within-tick):
	//
	//   r0: . r D 2 . V s    D pokes '*' to (r1,c+1); the read pokes (r1,c+5)
	//   r1: . * ! c k .      '!' banged from its left; value cell fed above
	//
	// (columns relative to x; midicc operands are channel +1, control +2,
	// value +3 from the '!' — so the V-read parks exactly over the value
	// cell.) midicc needs a neighbouring bang exactly like ':' and does NOT
	// early-return on a '.' value: an unwritten variable would emit a
	// constant 0 forever — the failure signature of distinctCcValues
	// exists to catch. The plan's optional 'A' scaling row is deliberately
	// omitted: 'A' only SHIFTS the value (note glyphs C..B sit at base36
	// indices 12..23, i.e. mid-range CC ~43..83 through midicc's
	// idx*127/35), it cannot compress, so the extra row bought nothing.
	if (maxH < 2 || maxW < 7) return {};

	Usz const colV = x + 5; // the read's V; also the value cell below it

	// Row 0: bang source + read of the producer's published pitch. The bang
	// period is pinned to 2 (rate '1'): D drives its output cell BOTH ways
	// ('*' on fire, '.' off), so the tail samples the source every 2nd tick.
	// A random period can be a multiple of the source's track length and
	// phase-lock onto one pitch — the stuck-CC failure all over again.
	buf.set(y, x,         '1');
	buf.set(y, x + 1,     'D');
	buf.set(y, x + 2,     '2');
	buf.set(y, colV - 1,  '.');
	buf.set(y, colV,      'V');
	buf.set(y, colV + 1,  sourceVar);

	// Row 1: '!' banged from the left, value cell fed by the read above.
	buf.set(y + 1, x + 1, '.');       // poked by the D each period
	buf.set(y + 1, x + 2, '!');
	buf.set(y + 1, x + 3, channel);
	buf.set(y + 1, x + 4, control);
	buf.set(y + 1, colV,  '.');       // value, poked by the V-read each tick

	return Extent{2, 7};
}

} // namespace Ahab
} // namespace StoermelderPackOne
