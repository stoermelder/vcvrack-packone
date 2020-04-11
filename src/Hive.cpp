/**
 * q = column
 * r = row
 * 												-r
 * 
 * 
 * 									-q		 *		+q
 * 
 * 
 * 										+r 
 * 
 * 
 *  
 * 									  +0 , -1	     +1 , -1
 * 
 * 
 *							    -1 , +0		 q=0 , r=0 	   +1 , +0 
 * 
 * 
 * 									  -1 , +1	     +0 , +1
 */

#include "plugin.hpp"
#include "digital.hpp"
#include <random>
#include <initializer_list>


namespace Hive {

enum GRIDSTATE {
	OFF = 0,
	ON = 1,
	RANDOM = 2
};

enum TURNMODE {
	SIXTY = 0,				///
	NINETY = 1,				///
	ONETWENTY = 2,			///
	ONEEIGHTY = 3			///
};

enum OUTMODE {
	BI_5V = 0,
	UNI_5V = 1,
	UNI_3V = 2,
	UNI_1V = 3
};

enum MODULESTATE {
	GRID = 0,
	EDIT = 1
};

enum DIRECTION {			///
	NE = 0,
	E = 1,
	SE = 2,
	SW = 3,
	W = 4,
	NW = 5
};

const int MAX_RADIUS = 17;						/// Max of 17 ensures the area of a cell does not shrink beyond that of one in Maze
const int MIN_RADIUS = 2;						///

const int ARRAY_SIZE = 2 * (MAX_RADIUS - 1) + 1;

const float BOX_WIDTH = 262.563f;
const float BOX_HEIGHT = 227.f;					/// Grid origin at (131.2815, 113.5)

struct CubeVec {								///
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	CubeVec() {}
	CubeVec(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct RoundAxialVec {							///
	int q = 0;
	int r = 0;

	RoundAxialVec() {}
	RoundAxialVec(int q, int r) : q(q), r(r) {}
};

template < int SIZE, int NUM_PORTS >
struct HiveModule : Module {
	enum ParamIds {
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(CLK_INPUT, NUM_PORTS),
		ENUMS(RESET_INPUT, NUM_PORTS),
		ENUMS(TURN_INPUT, NUM_PORTS),
		SHIFT_R1_INPUT,							///
		SHIFT_R2_INPUT,							///
		SHIFT_L1_INPUT,							///
		SHIFT_L2_INPUT,							///
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(TRIG_OUTPUT, NUM_PORTS),
		ENUMS(CV_OUTPUT, NUM_PORTS),
		NUM_OUTPUTS
	};
	enum LightIds {
		ENUMS(TRIG_LIGHT, NUM_PORTS),
		ENUMS(CV_LIGHT, NUM_PORTS * 2),
		NUM_LIGHTS
	};

	const int numPorts = NUM_PORTS;

	std::default_random_engine randGen{(uint16_t)std::chrono::system_clock::now().time_since_epoch().count()};
	std::geometric_distribution<int>* geoDist[NUM_PORTS] = {};

	/** [Stored to JSON] */
	int panelTheme = 0;

	/** [Stored to JSON] */
	int usedRadius = 7;															///
	/** [Stored to JSON] */
	int usedSize = 2 * (usedRadius - 1) + 1;									///

	/** [Stored to JSON] */														///
	float cellH = BOX_HEIGHT / (((2 * usedRadius - 2) * (3.f / 4.f)) + 1);
	/** [Stored to JSON] */														///
	float cellH3d4 = cellH * 3.f / 4.f;
	/** [Stored to JSON] */														///
	float cellHd2 = cellH / 2.f;
	/** [Stored to JSON] */														///
	float cellHd4 = cellH / 4.f;
	/** [Stored to JSON] */														///
	float cellW = sqrt(3) * (cellH / 2.f);
	/** [Stored to JSON] */														///
	float cellWd2 = cellW / 2.f;
	/** [Stored to JSON] */														///
	float hexSizeFactor = cellHd2;
	/** [Stored to JSON] */														///
	float pad = (BOX_WIDTH - (2 * usedRadius - 1) * cellW) / 2.f;

	/** [Stored to JSON] */
	GRIDSTATE grid[SIZE][SIZE];
	/** [Stored to JSON] */
	float gridCv[SIZE][SIZE];

	/** [Stored to JSON] */
	DIRECTION startDir[NUM_PORTS];												///
	/** [Stored to JSON] */
	int qStartPos[NUM_PORTS];													///
	/** [Stored to JSON] */
	int rStartPos[NUM_PORTS];													///
	/** [Stored to JSON] */
	DIRECTION dir[NUM_PORTS];													///
	/** [Stored to JSON] */
	int qPos[NUM_PORTS];														///
	/** [Stored to JSON] */
	int rPos[NUM_PORTS];														///

	/** [Stored to JSON] */
	TURNMODE turnMode[NUM_PORTS];
    /** [Stored to JSON] */
    TURNMODE ninetyState[NUM_PORTS];											///
	/** [Stored to JSON] */
	OUTMODE outMode[NUM_PORTS];
	/** [Stored to JSON] */
	bool normalizePorts;

	/** [Stored to JSON] */
	bool ratchetingEnabled[NUM_PORTS];
	/** [Stored to JSON] */
	float ratchetingProb[NUM_PORTS];

	dsp::SchmittTrigger clockTrigger[NUM_PORTS];
	bool clockTrigger0;
	dsp::SchmittTrigger resetTrigger[NUM_PORTS];
	bool resetTrigger0;
	dsp::SchmittTrigger turnTrigger[NUM_PORTS];
	bool turnTrigger0;
	dsp::Timer resetTimer[NUM_PORTS];
	float resetTimer0;
	dsp::PulseGenerator outPulse[NUM_PORTS];
	ClockMultiplier multiplier[NUM_PORTS];

	dsp::SchmittTrigger shiftR1Trigger;											///
	dsp::SchmittTrigger shiftR2Trigger;											///
	dsp::SchmittTrigger shiftL1Trigger;											///
	dsp::SchmittTrigger shiftL2Trigger;											///

	bool active[NUM_PORTS];
	MODULESTATE currentState = MODULESTATE::GRID;
	bool gridDirty = true;
	CubeVec mirrorCenters[6];													///

	dsp::ClockDivider lightDivider;


	HiveModule() {
		panelTheme = pluginSettings.panelThemeDefault;
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(128);
		// mapLinear();
		updateHexSize();														///
		updateMirrorCenters();													///
		onReset();
	}

	~HiveModule() {
		for (int i = 0; i < NUM_PORTS; i++) {
			delete geoDist[i];
		}
	}

	void onReset() override {
		gridClear();
		for (int i = 0; i < NUM_PORTS; i++) {
			qPos[i] = qStartPos[i] = 0 + (MAX_RADIUS - usedRadius);															///	Start along SW edge
			rPos[i] = rStartPos[i] = usedRadius / NUM_PORTS * i + (usedRadius - 1) + (MAX_RADIUS - usedRadius);				/// Start along SW edge
			dir[i] = startDir[i] = NE;																						/// Start direction NE
			turnMode[i] = TURNMODE::SIXTY;																					/// Start with small turns 
            ninetyState[i] = TURNMODE::SIXTY;																				/// Turnmode 90 starts with a small turn first
			outMode[i] = OUTMODE::UNI_3V;
			resetTimer[i].reset();
			ratchetingEnabled[i] = true;
			ratchetingSetProb(i);
		}

		normalizePorts = true;
		gridDirty = true;
		Module::onReset();
	}

	CubeVec axialToCube(Vec axialVec) {																					///
		float x = axialVec.x;
		float z = axialVec.y;
		float y = -x-z;

		return CubeVec(x, y, z);
	}

	RoundAxialVec hexRound(Vec axialVec) {																				///
		CubeVec cubeVec = (axialToCube(axialVec));

		float rx = roundf(cubeVec.x), 		ry = roundf(cubeVec.y), 	rz = roundf(cubeVec.z);
		float dx = fabsf(rx - cubeVec.x),	dy = fabsf(ry - cubeVec.y),	dz = fabsf(rz - cubeVec.z);
		
		if 		(dx > dy && dx > dz)	rx = -ry-rz;
		else if (dy > dz)				ry = -rx-rz;
		else							rz = -rx-ry;

		return RoundAxialVec(rx, rz);
	}

	bool gridHovered(Vec pixelVec) {																					///
		pixelVec.x -= BOX_WIDTH / 2.f;																					///Move origin (0px, 0px) to the center of the grid
		pixelVec.y -= BOX_HEIGHT / 2.f;
		float q = (2.f/3.f * pixelVec.x) / (BOX_WIDTH / 2.f);
    	float r = (-1.f/3.f * pixelVec.x  +  sqrt(3.f)/3.f * pixelVec.y) / (BOX_WIDTH / 2.f);
		RoundAxialVec roundVec = hexRound(Vec(q, r));
		if (!roundVec.q && !roundVec.r)
			return true;
		else return false;
	}

	RoundAxialVec pixelToHex(Vec pixelVec, float sizeFactor) {															///
		pixelVec.x -= BOX_WIDTH / 2.f;																					///Move origin (0px, 0px) to the center of the grid
		pixelVec.y -= BOX_HEIGHT / 2.f;
		float q = (sqrt(3.f)/3.f * pixelVec.x - (1.f/3.f) * pixelVec.y) / sizeFactor;
    	float r = ((2.f/3.f) * pixelVec.y) / sizeFactor;
		RoundAxialVec roundVec = hexRound(Vec(q, r));
		roundVec.q += SIZE / 2;																							//Rescale +/-x to 0->2*x
		roundVec.r += SIZE / 2;
		return roundVec;	
	}

	Vec hexToPixel(Vec axialVec, float sizeFactor) {																							///
		float x = (sqrt(3.f) * (axialVec.x - (SIZE / 2)) + sqrt(3.f)/2.f * (axialVec.y - (SIZE / 2))) * sizeFactor + BOX_WIDTH / 2.f;
    	float y = (3.f/2.f * (axialVec.y - (SIZE / 2))) * sizeFactor + BOX_HEIGHT / 2.f;
		return Vec(x, y);
	}

	int distance(CubeVec a, CubeVec b) {
		return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
	}

	bool cellVisible(int q, int r, int size) {												///
		bool visible = false;
		int radius = ((size - 1) / 2) + 1;

		q -= MAX_RADIUS - radius;
		r -= MAX_RADIUS - radius;

		if ((q >= 0 && r >= 0) && (q < size && r < size)) {
			if (q > radius - 1) {
				if (r < size - abs((radius - 1) - q))
					visible = true;
			}
			else if (q < radius - 1) {
				if (r >= size - (size - std::abs((radius - 1) - q)))
					visible = true;
			}
			else if (r < size)
				visible = true;
		}
		return visible;
	}

	void updateHexSize() {																	///
		cellH = BOX_HEIGHT / (((2 * usedRadius - 2) * (3.f / 4.f)) + 1);
		cellH3d4 = cellH * 3.f / 4.f;
		cellHd2 = cellH / 2.f;
		cellHd4 = cellH / 4.f;

		cellW = sqrt(3) * (cellH / 2.f);
		cellWd2 = cellW / 2.f;

		pad = (BOX_WIDTH - (2 * usedRadius - 1) * cellW) / 2.f;
		hexSizeFactor = cellHd2;
	}

	void updateMirrorCenters() {															///
		mirrorCenters[0] = CubeVec(	-(usedRadius - 1),				2 * (usedRadius - 1) + 1,		-(usedRadius - 1) - 1),				/// ( x,  y,  z)
		mirrorCenters[1] = CubeVec(	(usedRadius - 1) + 1,			(usedRadius - 1), 				-(2 * (usedRadius - 1) + 1)),		/// (-z, -x, -y)
		mirrorCenters[2] = CubeVec(	2 * (usedRadius - 1) + 1,		-(usedRadius - 1) - 1,			-(usedRadius - 1)),					/// ( y,  z,  x)
		mirrorCenters[3] = CubeVec(	(usedRadius - 1),				-(2 * (usedRadius - 1) + 1), 	(usedRadius - 1) + 1),				/// (-x, -y, -z)
		mirrorCenters[4] = CubeVec(	-(usedRadius - 1) - 1,			-(usedRadius - 1),				2 * (usedRadius - 1) + 1),			/// ( z,  x,  y)
		mirrorCenters[5] = CubeVec(	-(2 * (usedRadius - 1 ) + 1),	(usedRadius - 1) + 1,			(usedRadius - 1));					/// (-y, -z, -x)
	}

	void wrapHex(int port) {																///
		qPos[port] -= (SIZE - 1) / 2;														//Shift origin to 0, 0
		rPos[port] -= (SIZE - 1) / 2;
		CubeVec c = axialToCube(Vec(qPos[port], rPos[port]));
		for (int i = 0; i < 6; i++) {
			if (distance(c, mirrorCenters[i]) <= (usedRadius - 1)) {						//If distance from mirror center i is less than distance to grid center
				qPos[port] -= mirrorCenters[i].x;
				rPos[port] -= mirrorCenters[i].z;
			}
		}
		qPos[port] += (SIZE - 1) / 2;
		rPos[port] += (SIZE - 1) / 2;
	}

	void moveHex(int i, DIRECTION d) {														///
		switch (d) {
			case NE:
				qPos[i] += 1;
				rPos[i] -= 1;
				break;
			case E:
				qPos[i] += 1;
				break;
			case SE:
				rPos[i] += 1;
				break;
			case SW:
				qPos[i] -= 1;
				rPos[i] += 1;
				break;
			case W:
				qPos[i] -= 1;
				break;
			case NW:
				rPos[i] -= 1;
				break;
		}
		if (!cellVisible(qPos[i], rPos[i], usedSize))
			wrapHex(i);
	}

	void process(const ProcessArgs& args) override {
		if (shiftR1Trigger.process(inputs[SHIFT_R1_INPUT].getVoltage()))				///
			for (int i = 0; i < NUM_PORTS; i++)
				moveHex(i, (DIRECTION)((dir[i] + 1 ) % 6));
		if (shiftR2Trigger.process(inputs[SHIFT_R2_INPUT].getVoltage()))				///
			for (int i = 0; i < NUM_PORTS; i++)
				moveHex(i, (DIRECTION)((dir[i] + 2 ) % 6));
		if (shiftL1Trigger.process(inputs[SHIFT_L1_INPUT].getVoltage()))				///
			for (int i = 0; i < NUM_PORTS; i++)
				moveHex(i, (DIRECTION)((dir[i] + 5 ) % 6));
		if (shiftL1Trigger.process(inputs[SHIFT_L1_INPUT].getVoltage()))				///
			for (int i = 0; i < NUM_PORTS; i++)
				moveHex(i, (DIRECTION)((dir[i] + 4 ) % 6));

		for (int i = 0; i < NUM_PORTS; i++) {
			active[i] = outputs[TRIG_OUTPUT + i].isConnected() || outputs[CV_OUTPUT + i].isConnected();
			bool doPulse = false;

			if (processResetTrigger(i)) {
				qPos[i] = qStartPos[i];									///
				rPos[i] = rStartPos[i];									///
				dir[i] = startDir[i];									///
				multiplier[i].reset();
			}

			if (processClockTrigger(i, args.sampleTime)) {
				moveHex(i, dir[i]);										///
				multiplier[i].tick();

				switch (grid[qPos[i]][rPos[i]]) {            			///
					case GRIDSTATE::OFF:
						break;
					case GRIDSTATE::ON:
						doPulse = true;
						break;
					case GRIDSTATE::RANDOM:
						if (ratchetingEnabled[i]) {
							if (geoDist[i])
								multiplier[i].trigger((*geoDist[i])(randGen));
						}
						else {
							doPulse = random::uniform() >= 0.5f;
						}
						break;
				}
			}

			if (processTurnTrigger(i)) {								///
				switch (turnMode[i]) {
					case SIXTY:
						if (dir[i] == NW)
							dir[i] = NE;
						else
							dir[i] = (DIRECTION)(dir[i] + 1);
						break;
					case NINETY:
						if (ninetyState[i] == SIXTY) {
							if (dir[i] == NW)
								dir[i] = NE;
							else
								dir[i] = (DIRECTION)(dir[i] + 1);
							ninetyState[i] = ONETWENTY;
						}
						else {
							if (dir[i] < 4)
								dir[i] = (DIRECTION)(dir[i] + 2);
							else
								dir[i] = (DIRECTION)(dir[i] - 4);
							ninetyState[i] = SIXTY;
						}
						break;
					case ONETWENTY:
						if (dir[i] < 4)
								dir[i] = (DIRECTION)(dir[i] + 2);
							else
								dir[i] = (DIRECTION)(dir[i] - 4);
						break;
					case ONEEIGHTY:
						if (dir[i] < 3)
							dir[i] = (DIRECTION)(dir[i] + 3);
						else
							dir[i] = (DIRECTION)(dir[i] - 3);
						break;
				}
			}

			float outGate = 0.f;
			float outCv = outputs[CV_OUTPUT + i].getVoltage();

			if (multiplier[i].process() || doPulse) {
				outPulse[i].trigger();
				switch (outMode[i]) {
					case OUTMODE::BI_5V:
						outCv = rescale(gridCv[qPos[i]][rPos[i]], 0.f, 1.f, -5.f, 5.f);				///
						break;
					case OUTMODE::UNI_5V:
						outCv = rescale(gridCv[qPos[i]][rPos[i]], 0.f, 1.f, 0.f, 5.f);				///
						break;
					case OUTMODE::UNI_3V:
						outCv = rescale(gridCv[qPos[i]][rPos[i]], 0.f, 1.f, 0.f, 3.f);				///
						break;
					case OUTMODE::UNI_1V:
						outCv = gridCv[qPos[i]][rPos[i]];											///
						break;
				}
			}

			if (outPulse[i].process(args.sampleTime))
				outGate = 10.f;

			outputs[TRIG_OUTPUT + i].setVoltage(outGate);
			outputs[CV_OUTPUT + i].setVoltage(outCv);
		}

		// Set channel lights infrequently
		if (lightDivider.process()) {
			float s = args.sampleTime * lightDivider.division;
			for (int i = 0; i < NUM_PORTS; i++) {
				float l = outputs[TRIG_OUTPUT + i].isConnected() && outputs[TRIG_OUTPUT + i].getVoltage() > 0.f;
				lights[TRIG_LIGHT + i].setSmoothBrightness(l, s);

				float l1 = outputs[CV_OUTPUT + i].getVoltage() * outputs[CV_OUTPUT + i].isConnected();
				float l2 = l1;
				if (l1 > 0.f) l1 = rescale(l1, 0.f, 5.f, 0.f, 1.f);
				lights[CV_LIGHT + i * 2].setSmoothBrightness(l1, s);
				if (l2 < 0.f) l2 = rescale(l2, -5.f, 0.f, 1.f, 0.f);
				lights[CV_LIGHT + i * 2 + 1].setSmoothBrightness(l2, s);
			}
		}
	}

	inline bool processResetTrigger(int port) {
		if (port == 0) {
			resetTrigger0 = resetTrigger[0].process(inputs[RESET_INPUT].getVoltage() + params[RESET_PARAM].getValue());
			if (resetTrigger0) resetTimer[0].reset();
			return resetTrigger0;
		}
		else {
			if (inputs[RESET_INPUT + port].isConnected()) {
				bool r = resetTrigger[port].process(inputs[RESET_INPUT + port].getVoltage() + params[RESET_PARAM].getValue());
				if (r) resetTimer[port].reset();
				return r;
			}
			else {
				return normalizePorts && resetTrigger0;
			}
		}
	}

	inline bool processClockTrigger(int port, float sampleTime) {
		if (port == 0) {
			resetTimer0 = resetTimer[0].process(sampleTime);
			clockTrigger0 = resetTimer0 >= 1e-3f && clockTrigger[0].process(inputs[CLK_INPUT].getVoltage());
			return clockTrigger0;
		}
		else {
			bool r = resetTimer0 >= 1e-3f;
			if (inputs[RESET_INPUT + port].isConnected()) {
				r = resetTimer[port].process(sampleTime) >= 1e-3f;
			}
			if (inputs[CLK_INPUT + port].isConnected()) {
				return r && clockTrigger[port].process(inputs[CLK_INPUT + port].getVoltage());
			}
			else {
				return normalizePorts && clockTrigger0;
			}
		}
	}

	inline bool processTurnTrigger(int port) {
		if (port == 0) {
			turnTrigger0 = turnTrigger[0].process(inputs[TURN_INPUT].getVoltage());
			return turnTrigger0;
		}
		else {
			if (inputs[TURN_INPUT + port].isConnected()) {
				return turnTrigger[port].process(inputs[TURN_INPUT + port].getVoltage());
			}
			else {
				return normalizePorts && turnTrigger0;
			}
		}
	}

	void gridClear() {
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				grid[i][j] = GRIDSTATE::OFF;
				gridCv[i][j] = 0.f;
			}
		}
		gridDirty = true;
	}

	void gridResize(int radius) {						///
		if (radius == usedRadius) return;
		usedRadius = radius;
		usedSize = 2 * (radius - 1) + 1;
		updateHexSize();
		updateMirrorCenters();

		for (int i = 0; i < NUM_PORTS; i++) {
			qStartPos[i] = 0 + (MAX_RADIUS - usedRadius);													/// SW edge
			rStartPos[i] = usedRadius / NUM_PORTS * i + (usedRadius - 1) + (MAX_RADIUS - usedRadius);		/// Divide across SW edge

			if (!cellVisible(qPos[i], rPos[i], radius))
				wrapHex(i);
		}
		gridDirty = true;
	}

	void gridRandomize(bool useRandom = true) {
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				if (cellVisible(i, j, SIZE)) {				///
					float rand = random::uniform();
					if (rand > 0.8f) {
						grid[i][j] = useRandom ? GRIDSTATE::RANDOM : GRIDSTATE::ON;
						gridCv[i][j] = random::uniform();
					}
					else if (rand > 0.6f) {
						grid[i][j] = GRIDSTATE::ON;
						gridCv[i][j] = random::uniform();
					}
					else {
						grid[i][j] = GRIDSTATE::OFF;
						gridCv[i][j] = 0.f;
					}
				}
			}
		}
		gridDirty = true;
	}

	void gridNextState(int i, int j) {
		grid[i][j] = (GRIDSTATE)((grid[i][j] + 1) % 3);
		if (grid[i][j] == GRIDSTATE::ON) gridCv[i][j] = random::uniform();
		gridDirty = true;
	}

	void gridSetState(int i, int j, GRIDSTATE s, float cv) {
		grid[i][j] = s;
		gridCv[i][j] = cv;
		gridDirty = true;
	}

	void ratchetingSetProb(int id, float prob = 0.35f) {
		auto geoDistOld = geoDist[id];
		geoDist[id] = new std::geometric_distribution<int>(prob);
		if (geoDistOld) delete geoDistOld;
		ratchetingProb[id] = prob;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();

		json_object_set_new(rootJ, "panelTheme", json_integer(panelTheme));

		json_t* gridJ = json_array();
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				json_array_append_new(gridJ, json_integer(grid[i][j]));
			}
		}
		json_object_set_new(rootJ, "grid", gridJ);

		json_t* gridCvJ = json_array();
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				json_array_append_new(gridCvJ, json_real(gridCv[i][j]));
			}
		}
		json_object_set_new(rootJ, "gridCv", gridCvJ);



		json_t* portsJ = json_array();
		for (int i = 0; i < NUM_PORTS; i++) {
			json_t* portJ = json_object();
			json_object_set_new(portJ, "qStartPos", json_integer(qStartPos[i]));					///
			json_object_set_new(portJ, "rStartPos", json_integer(rStartPos[i]));					///
			json_object_set_new(portJ, "startDir", json_integer(startDir[i]));						///
			json_object_set_new(portJ, "qPos", json_integer(qPos[i]));								///
			json_object_set_new(portJ, "rPos", json_integer(rPos[i]));								///
			json_object_set_new(portJ, "dir", json_integer(dir[i]));								///
			json_object_set_new(portJ, "turnMode", json_integer(turnMode[i]));
			json_object_set_new(portJ, "ninetyState", json_integer(ninetyState[i]));    			///
			json_object_set_new(portJ, "outMode", json_integer(outMode[i]));
			json_object_set_new(portJ, "ratchetingProb", json_real(ratchetingProb[i]));
			json_object_set_new(portJ, "ratchetingEnabled", json_boolean(ratchetingEnabled[i]));
			json_array_append_new(portsJ, portJ);
		}
		json_object_set_new(rootJ, "ports", portsJ);

		json_object_set_new(rootJ, "usedRadius", json_integer(usedRadius));							///
		json_object_set_new(rootJ, "usedSize", json_integer(usedSize));

		json_object_set_new(rootJ, "cellH", json_real(cellH));
		json_object_set_new(rootJ, "cellH3d4", json_real(cellH3d4));
		json_object_set_new(rootJ, "cellHd2", json_real(cellHd2));
		json_object_set_new(rootJ, "cellHd4", json_real(cellHd4));
		json_object_set_new(rootJ, "cellW", json_real(cellW));
		json_object_set_new(rootJ, "cellWd2", json_real(cellWd2));
		json_object_set_new(rootJ, "hexSizeFactor", json_real(hexSizeFactor));
		json_object_set_new(rootJ, "pad", json_real(pad));

		json_object_set_new(rootJ, "normalizePorts", json_boolean(normalizePorts));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		panelTheme = json_integer_value(json_object_get(rootJ, "panelTheme"));

		json_t* gridJ = json_object_get(rootJ, "grid");
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				grid[i][j] = (GRIDSTATE)json_integer_value(json_array_get(gridJ, i * SIZE + j));
			}
		}
		
		json_t* gridCvJ = json_object_get(rootJ, "gridCv");
		for (int i = 0; i < SIZE; i++) {
			for (int j = 0; j < SIZE; j++) {
				gridCv[i][j] = json_real_value(json_array_get(gridCvJ, i * SIZE + j));
			}
		}

		json_t* portsJ = json_object_get(rootJ, "ports");
		json_t* portJ;
		size_t portIndex;
		json_array_foreach(portsJ, portIndex, portJ) {
			qStartPos[portIndex] = json_integer_value(json_object_get(portJ, "qStartPos"));							///
			rStartPos[portIndex] = json_integer_value(json_object_get(portJ, "rStartPos"));							///	
			startDir[portIndex] = (DIRECTION)json_integer_value(json_object_get(portJ, "startDir"));				///
			qPos[portIndex] = json_integer_value(json_object_get(portJ, "qPos"));									///
			rPos[portIndex] = json_integer_value(json_object_get(portJ, "rPos"));									///
			dir[portIndex] = (DIRECTION)json_integer_value(json_object_get(portJ, "dir"));							///
			turnMode[portIndex] = (TURNMODE)json_integer_value(json_object_get(portJ, "turnMode"));
			ninetyState[portIndex] = (TURNMODE)json_integer_value(json_object_get(portJ, "ninetyState"));       	///
			outMode[portIndex] = (OUTMODE)json_integer_value(json_object_get(portJ, "outMode"));
			ratchetingEnabled[portIndex] = json_boolean_value(json_object_get(portJ, "ratchetingEnabled"));

			json_t* ratchetingProbJ = json_object_get(portJ, "ratchetingProb");
			if (ratchetingProbJ) {
				ratchetingSetProb(portIndex, json_real_value(ratchetingProbJ));
			}
		}

		usedRadius = json_integer_value(json_object_get(rootJ, "usedRadius"));				///
		usedSize = json_integer_value(json_object_get(rootJ, "usedSize"));

		cellH = json_real_value(json_object_get(rootJ, "cellH"));		
		cellH3d4 = json_real_value(json_object_get(rootJ, "cellH3d4"));		
		cellHd2 = json_real_value(json_object_get(rootJ, "cellHd2"));		
		cellHd4 = json_real_value(json_object_get(rootJ, "cellHd4"));		
		cellW = json_real_value(json_object_get(rootJ, "cellW"));		
		cellWd2 = json_real_value(json_object_get(rootJ, "cellWd2"));		
		hexSizeFactor = json_real_value(json_object_get(rootJ, "hexSizeFactor"));		
		pad = json_real_value(json_object_get(rootJ, "pad"));		

		json_t* normalizePortsJ = json_object_get(rootJ, "normalizePorts");
		if (normalizePortsJ) normalizePorts = json_boolean_value(normalizePortsJ);

		json_t* ratchetingEnabledJ = json_object_get(rootJ, "ratchetingEnabled");
		json_t* ratchetingProbJ = json_object_get(rootJ, "ratchetingProb");
		if (ratchetingEnabledJ) {
			for (int i = 0; i < NUM_PORTS; i++) {
				ratchetingEnabled[i] = json_boolean_value(ratchetingEnabledJ);
				ratchetingSetProb(i, json_real_value(ratchetingProbJ));
			}
		}

		gridDirty = true;
	}
};


// Context menus

template < typename MODULE >
struct ModuleStateMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action &e) override {
		module->currentState = module->currentState == MODULESTATE::GRID ? MODULESTATE::EDIT : MODULESTATE::GRID;
		module->gridDirty = true;
	}
};

template < typename MODULE >
struct GridCellChangeAction : history::ModuleAction {
	int q, r;																///
	GRIDSTATE oldGrid, newGrid;
	float oldGridCv, newGridCv;

	GridCellChangeAction() {
		name = "stoermelder HIVE cell";
	}

	void undo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		assert(m);
		m->gridSetState(q, r, oldGrid, oldGridCv);							///
	}

	void redo() override {
		app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
		assert(mw);
		MODULE* m = dynamic_cast<MODULE*>(mw->module);
		assert(m);
		m->gridSetState(q, r, newGrid, newGridCv);							///
	}
};


template < typename MODULE >
struct GridSizeSlider : ui::Slider {
	struct GridSizeQuantity : Quantity {
		MODULE* module;
		float v = -1.f;

		GridSizeQuantity(MODULE* module) {
			this->module = module;
		}
		void setValue(float value) override {
			v = clamp(value, 2.f, 17.f);								///
			module->gridResize(int(v));
		}
		float getValue() override {
			if (v < 0.f) v = module->usedRadius;						///
			return v;
		}
		float getDefaultValue() override {
			return 5.f;													///
		}
		float getMinValue() override {
			return float(MIN_RADIUS);
		}
		float getMaxValue() override {
			return float(MAX_RADIUS);
		}
		float getDisplayValue() override {
			return getValue();
		}
		std::string getDisplayValueString() override {
			int i = int(getValue());
			return string::f("%i", i);									///
		}
		void setDisplayValue(float displayValue) override {
			setValue(displayValue);
		}
		std::string getLabel() override {
			return "Dimension";
		}
		std::string getUnit() override {
			return "";
		}
	};

	GridSizeSlider(MODULE* module) {
		quantity = new GridSizeQuantity(module);
	}
	~GridSizeSlider() {
		delete quantity;
	}
	void onDragMove(const event::DragMove& e) override {
		if (quantity) {
			quantity->moveScaledValue(0.002f * e.mouseDelta.x);
		}
	}
};

template < typename MODULE >
struct GridRandomizeMenuItem : MenuItem {
	MODULE* module;
	bool useRandom = true;
	
	void onAction(const event::Action& e) override {
		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "stoermelder HIVE grid randomize";
		h->moduleId = module->id;
		h->oldModuleJ = module->toJson();

		module->gridRandomize(useRandom);

		h->newModuleJ = module->toJson();
		APP->history->push(h);
	}
};

template < typename MODULE >
struct GridClearMenuItem : MenuItem {
	MODULE* module;
	
	void onAction(const event::Action& e) override {
		// history::ModuleChange
		history::ModuleChange* h = new history::ModuleChange;
		h->name = "stoermelder HIVE grid clear";
		h->moduleId = module->id;
		h->oldModuleJ = module->toJson();

		module->gridClear();

		h->newModuleJ = module->toJson();
		APP->history->push(h);
	}
};


// Widgets

template < typename MODULE >
struct HiveGridWidget : FramebufferWidget {
	struct HiveGridDrawWidget : OpaqueWidget {
		MODULE* module;
		NVGcolor gridColor = color::WHITE;

		HiveGridDrawWidget(MODULE* module) {
			this->module = module;
		}

		void draw(const Widget::DrawArgs& args) override {								///
			if (!module) return;

			float boxYd2 = box.size.y / 2.f;
			float boxX3d4 = box.size.x * 3.f / 4.f;
			float boxXd4 = box.size.x / 4.f;

			// Draw background
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0.f, boxYd2);
			nvgLineTo(args.vg, boxXd4, 0.f);
			nvgLineTo(args.vg, boxX3d4, 0.f);
			nvgLineTo(args.vg, box.size.x, boxYd2);
			nvgLineTo(args.vg, boxX3d4, box.size.y);
			nvgLineTo(args.vg, boxXd4, box.size.y);
			nvgClosePath(args.vg);
			nvgFillColor(args.vg, nvgRGB(0, 16, 90));
			nvgFill(args.vg);

			// Draw grid
			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			nvgStrokeWidth(args.vg, 0.6f);
			for (int i = 0; i < module->usedRadius; i++) {
				if (i == 0) {
					for (int j = 0; j < module->usedRadius * 2 - 1; j++) {
						float a = 0.075f;
						float x = module->pad + module->cellW * float(j);
						float y = boxYd2 - module->cellHd4;
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, x, y);
						nvgLineTo(args.vg, x + module->cellWd2, y - module->cellHd4	);
						nvgLineTo(args.vg, x + module->cellW, 	y					);
						nvgLineTo(args.vg, x + module->cellW,	y + module->cellHd2	);
						nvgLineTo(args.vg, x + module->cellWd2, y + module->cellH3d4);
						nvgLineTo(args.vg, x, 					y + module->cellHd2	);
						nvgClosePath(args.vg);
						nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
						nvgStroke(args.vg);
					}
				}
				else {
					for (int j = 0; j < module->usedRadius * 2 - 1 - i; j++) {
						for (int k = -1; k < 2; k += 2) {
							float a = 0.075f;
							float x = module->pad + (module->cellWd2 * i) + (module->cellW * float(j));
							float y = boxYd2 + module->cellH3d4 * i * k - module->cellHd4;
							nvgBeginPath(args.vg);
							nvgMoveTo(args.vg, x, y);
							nvgLineTo(args.vg, x + module->cellWd2, y - module->cellHd4	);
							nvgLineTo(args.vg, x + module->cellW, 	y					);
							nvgLineTo(args.vg, x + module->cellW, 	y + module->cellHd2	);
							nvgLineTo(args.vg, x + module->cellWd2, y + module->cellH3d4);
							nvgLineTo(args.vg, x, 					y + module->cellHd2	);
							nvgClosePath(args.vg);
							nvgStrokeColor(args.vg, color::mult(color::WHITE, a));
							nvgStroke(args.vg);
						}
					}
				}
			}

			// Draw outer edge
			for (int i = 0; i < module->usedRadius; i++) {
				if (i == 0) {
					float x = module->pad + module->cellWd2;
					float y = boxYd2 + module->cellHd2;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, x, y);
					nvgLineTo(args.vg, x - module->cellWd2, y - module->cellHd4	);
					nvgLineTo(args.vg, x - module->cellWd2, y - module->cellH3d4);
					nvgLineTo(args.vg, x, 					y - module->cellH	);
					nvgStrokeWidth(args.vg, 0.7f);
					nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.125f));
					nvgStroke(args.vg);

					x = box.size.x - module->pad - module->cellWd2;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, x, y);
					nvgLineTo(args.vg, x + module->cellWd2, y - module->cellHd4	);
					nvgLineTo(args.vg, x + module->cellWd2, y - module->cellH3d4);
					nvgLineTo(args.vg, x, y - module->cellH);
					nvgStrokeWidth(args.vg, 0.7f);
					nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.125f));
					nvgStroke(args.vg);
				}
				else {
					for (int k = -1; k < 2; k += 2) {
						float x = module->pad + (module->cellWd2 * i);
						float y = boxYd2 + module->cellH3d4 * i * k - module->cellHd4 * k;
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, x, y);
						nvgLineTo(args.vg, x, 					y + module->cellHd2 * k	);
						nvgLineTo(args.vg, x + module->cellWd2, y + module->cellH3d4 * k);
						nvgStrokeWidth(args.vg, 0.7f);
						nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.125f));
						nvgStroke(args.vg);

						x = box.size.x - module->pad - (module->cellWd2 * i);
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, x, y);
						nvgLineTo(args.vg, x, 					y + module->cellHd2 * k	);
						nvgLineTo(args.vg, x - module->cellWd2, y + module->cellH3d4 * k);
						nvgStrokeWidth(args.vg, 0.7f);
						nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.125f));
						nvgStroke(args.vg);

						if (i == module->usedRadius - 1) {
							float x = module->pad + (module->cellWd2 * (i + 1));
							float y = boxYd2 + module->cellH3d4 * (i + 1) * k - module->cellHd4 * k;
							nvgBeginPath(args.vg);
							nvgMoveTo(args.vg, x, y);
							for (int j = 0; j < module->usedRadius - 1; j++) {
								nvgLineTo(args.vg, x += module->cellWd2, y -= module->cellHd4 * k);
								nvgLineTo(args.vg, x += module->cellWd2, y += module->cellHd4 * k);
							}
							nvgStrokeWidth(args.vg, 0.7f);
							nvgStrokeColor(args.vg, color::mult(color::WHITE, 0.125f));
							nvgStroke(args.vg);
						}
					}
				}
			}
			

			// Draw grid cells
			float stroke = 0.7f;

			float onCellW = module->cellW - stroke;
			float onCellWd2 = onCellW / 2.f;
			float onCellH = module->cellH - stroke;
			float onCellH3d4 = onCellH * 3.f / 4.f;
			float onCellHd2 = onCellH / 2.f;
			float onCellHd4 = onCellH / 4.f;

			float rCellW = module->cellW - stroke * 2.f;
			float rCellWd2 = rCellW / 2.f;
			float rCellH = module->cellH - stroke * 2.f;
			float rCellH3d4 = rCellH * 3.f / 4.f;
			float rCellHd2 = rCellH / 2.f;
			float rCellHd4 = rCellH / 4.f;

			float sCellW = module->cellWd2;
			float sCellWd2 = sCellW / 2.f;
			float sCellH = module->cellHd2;
			float sCellH3d4 = sCellH * 3.f / 4.f;
			float sCellHd2 = sCellH / 2.f;
			float sCellHd4 = sCellH / 4.f;

			Vec c;
			for (int i = 0; i < ARRAY_SIZE; i++) {
				for (int j = 0; j < ARRAY_SIZE; j++) {
					if (module->cellVisible(i, j, module->usedSize)) {
						switch (module->grid[i][j]) {
							case GRIDSTATE::ON:
								c = module->hexToPixel(Vec(i, j), module->hexSizeFactor);
								c.x = c.x - module->cellWd2 + stroke / 2.f;
								c.y = c.y - module->cellHd4 + stroke / 2.f;
								nvgBeginPath(args.vg);
								nvgMoveTo(args.vg, c.x, 			c.y				);
								nvgLineTo(args.vg, c.x + onCellWd2, c.y - onCellHd4	);
								nvgLineTo(args.vg, c.x + onCellW, 	c.y				);
								nvgLineTo(args.vg, c.x + onCellW, 	c.y + onCellHd2	);
								nvgLineTo(args.vg, c.x + onCellWd2, c.y + onCellH3d4);
								nvgLineTo(args.vg, c.x,				c.y + onCellHd2	);
								nvgClosePath(args.vg);
								nvgFillColor(args.vg, color::mult(gridColor, 0.7f));
								nvgFill(args.vg);
								break;
							case GRIDSTATE::RANDOM:
								c = module->hexToPixel(Vec(i, j), module->hexSizeFactor);
								c.x = c.x - module->cellWd2 + stroke;
								c.y = c.y - module->cellHd4 + stroke;
								nvgBeginPath(args.vg);
								nvgMoveTo(args.vg, c.x, 			c.y				);
								nvgLineTo(args.vg, c.x + rCellWd2,	c.y - rCellHd4	);
								nvgLineTo(args.vg, c.x + rCellW, 	c.y				);
								nvgLineTo(args.vg, c.x + rCellW, 	c.y + rCellHd2	);
								nvgLineTo(args.vg, c.x + rCellWd2,	c.y + rCellH3d4	);
								nvgLineTo(args.vg, c.x, 			c.y + rCellHd2	);
								nvgClosePath(args.vg);
								nvgStrokeWidth(args.vg, stroke);
								nvgStrokeColor(args.vg, color::mult(gridColor, 0.6f));
								nvgStroke(args.vg);

								nvgBeginPath(args.vg);
								c.x = c.x + module->cellWd2 - stroke - sCellWd2;
								c.y = c.y + module->cellHd4 - stroke - sCellHd4;
								nvgMoveTo(args.vg, c.x, 			c.y				);
								nvgLineTo(args.vg, c.x + sCellWd2,	c.y - sCellHd4	);
								nvgLineTo(args.vg, c.x + sCellW,	c.y				);
								nvgLineTo(args.vg, c.x + sCellW,	c.y + sCellHd2	);
								nvgLineTo(args.vg, c.x + sCellWd2,	c.y + sCellH3d4	);
								nvgLineTo(args.vg, c.x, 			c.y + sCellHd2	);
								nvgClosePath(args.vg);
								nvgFillColor(args.vg, color::mult(gridColor, 0.4f));
								nvgFill(args.vg);
								break;
							case GRIDSTATE::OFF:
								break;
						}
					}
				}
			}
		}
	};

	MODULE* module;
	HiveGridDrawWidget* w;
	
	HiveGridWidget(MODULE* module) {
		this->module = module;
		w = new HiveGridDrawWidget(module);
		addChild(w);
	}

	void step() override{
		if (module && module->gridDirty) {
			FramebufferWidget::dirty = true;
			w->box.size = box.size;
			w->gridColor = module->currentState == MODULESTATE::EDIT ? color::mult(color::WHITE, 0.35f) : color::WHITE;
			module->gridDirty = false;
		}
		FramebufferWidget::step();
	}
};


template < typename MODULE >
struct HiveDrawHelper {
	MODULE* module;
	int* qpos;
	int* rpos;

	NVGcolor colors[4] = { color::YELLOW, color::RED, color::CYAN, color::BLUE };

	void draw(const Widget::DrawArgs& args, Rect box) {										///
		float radius = module->cellWd2;

		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		for (int i = 0; i < module->numPorts; i++) {
			if (module->currentState == MODULESTATE::EDIT || module->active[i]) {
				Vec c = module->hexToPixel(Vec(qpos[i], rpos[i]), module->hexSizeFactor);
				// Inner circle
				nvgGlobalCompositeOperation(args.vg, NVG_ATOP);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, radius * 0.75f);
				nvgFillColor(args.vg, color::mult(colors[i], 0.35f));
				nvgFill(args.vg);
				// Outer cirlce
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, radius - 0.7f);
				nvgStrokeColor(args.vg, color::mult(colors[i], 0.9f));
				nvgStrokeWidth(args.vg, 0.7f);
				nvgStroke(args.vg);
			}
		}
		for (int i = 0; i < module->numPorts; i++) {
			if (module->currentState == MODULESTATE::EDIT || module->active[i]) {
				Vec c = module->hexToPixel(Vec(qpos[i], rpos[i]), module->hexSizeFactor);
				// Halo
				NVGpaint paint;
				NVGcolor icol = color::mult(colors[i], 0.25f);
				NVGcolor ocol = nvgRGB(0, 0, 0);
				nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, c.x, c.y, radius * 1.5f);
				paint = nvgRadialGradient(args.vg, c.x, c.y, radius, radius * 1.5f, icol, ocol);
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
			}
		}
	}
};


template < typename MODULE >
struct HiveStartPosEditWidget : OpaqueWidget, HiveDrawHelper<MODULE> {
	MODULE* module;
	std::shared_ptr<Font> font;
	int selectedId = -1;
	math::Vec dragPos;

	HiveStartPosEditWidget(MODULE* module) {
		font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		this->module = module;
		HiveDrawHelper<MODULE>::module = module;
		HiveDrawHelper<MODULE>::qpos = module->qStartPos;
		HiveDrawHelper<MODULE>::rpos = module->rStartPos;
	}

	void draw(const DrawArgs& args) override {											///
		if (module && module->currentState == MODULESTATE::EDIT) {
			float boxYd2 = box.size.y / 2.f;
			float boxX3d4 = box.size.x * 3.f / 4.f;
			float boxXd4 = box.size.x / 4.f;
			NVGcolor c = color::mult(color::WHITE, 0.7f);
			float stroke = 1.f;
			nvgGlobalCompositeOperation(args.vg, NVG_ATOP);

			// Outer border																///
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0.f, boxYd2);
			nvgLineTo(args.vg, boxXd4, 0.f);
			nvgLineTo(args.vg, boxX3d4, 0.f);
			nvgLineTo(args.vg, box.size.x, boxYd2);
			nvgLineTo(args.vg, boxX3d4, box.size.y);
			nvgLineTo(args.vg, boxXd4, box.size.y);
			nvgClosePath(args.vg);
			nvgStrokeWidth(args.vg, stroke);
			nvgStrokeColor(args.vg, c);
			nvgStroke(args.vg);

			// Draw "EDIT" text
			nvgFontSize(args.vg, 22);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -2.2);
			nvgFillColor(args.vg, c);
			nvgTextBox(args.vg, box.size.x - 101.25f, box.size.y - 6.f, 120, "EDIT", NULL);			///

			HiveDrawHelper<MODULE>::draw(args, box);

			float radius = module->cellWd2 * 0.75f;													///

			nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
			for (int i = 0; i < module->numPorts; i++) {
				// Direction triangle
				Vec c = module->hexToPixel(Vec(module->qStartPos[i], module->rStartPos[i]), module->hexSizeFactor);	
				Vec p1 = Vec(radius, 0);
				Vec p2 = Vec(0, -radius);
				Vec p3 = Vec(0, radius);
				switch (module->startDir[i]) {
					case NE:
						p1 = c.plus(p1.rotate(M_PI / -3.f));
						p2 = c.plus(p2.rotate(M_PI / -3.f));
						p3 = c.plus(p3.rotate(M_PI / -3.f));
						break;
					case E:
						p1 = c.plus(p1);
						p2 = c.plus(p2);
						p3 = c.plus(p3);
						break;
					case SE:
						p1 = c.plus(p1.rotate(M_PI / 3.f));
						p2 = c.plus(p2.rotate(M_PI / 3.f));
						p3 = c.plus(p3.rotate(M_PI / 3.f));
						break;
					case SW:
						p1 = c.plus(p1.rotate(2.f * M_PI / 3.f));
						p2 = c.plus(p2.rotate(2.f * M_PI / 3.f));
						p3 = c.plus(p3.rotate(2.f * M_PI / 3.f));
						break;
					case W:
						p1 = c.minus(p1);
						p2 = c.minus(p2);
						p3 = c.minus(p3);				
						break;
					case NW:
						p1 = c.plus(p1.rotate(2.f * M_PI / -3.f));
						p2 = c.plus(p2.rotate(2.f * M_PI / -3.f));
						p3 = c.plus(p3.rotate(2.f * M_PI / -3.f));
						break;
				}
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, p1.x, p1.y);
				nvgLineTo(args.vg, p2.x, p2.y);
				nvgLineTo(args.vg, p3.x, p3.y);
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, color::mult(color::WHITE, 0.9f));
				nvgFill(args.vg);
			}

			OpaqueWidget::draw(args);
		}
	}

	void onButton(const event::Button& e) override {
		if (module && module->currentState == MODULESTATE::EDIT) {
			if (e.action == GLFW_PRESS) {
				selectedId = -1;
				if (module->gridHovered(e.pos)) {															///
					RoundAxialVec c = module->pixelToHex(e.pos, module->hexSizeFactor);						///
					for (int i = 0; i < module->numPorts; i++) {
						if (module->qStartPos[i] == c.q && module->rStartPos[i] == c.r) {					///
							selectedId = i;
							break;
						}
					}

					if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
						dragPos = APP->scene->rack->mousePos.minus(e.pos);
						e.consume(this);
					}
					if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
						if (selectedId == -1) 
							createContextMenu();
						else 
							createDirectionContextMenu();
						e.consume(this);
					}
				}
			} 
			OpaqueWidget::onButton(e);
		}
	}

	void onDragMove(const event::DragMove& e) override {							///
		if (module && module->currentState == MODULESTATE::EDIT) {
			if (e.button != GLFW_MOUSE_BUTTON_LEFT)
				return;
			if (selectedId == -1)
				return;

			math::Vec pos = APP->scene->rack->mousePos.minus(dragPos);
			RoundAxialVec hex = module->pixelToHex(pos, module->hexSizeFactor);		///
			if (module->cellVisible(hex.q, hex.r, module->usedSize)) {				///
				module->qStartPos[selectedId] = hex.q;								///
				module->rStartPos[selectedId] = hex.r;								///
			}
		}
	}

	void createDirectionContextMenu() {
		ui::Menu* menu = createMenu();

		struct DirectionItem : MenuItem {
			MODULE* module;
			DIRECTION dir;											///
			int id;

			void onAction(const event::Action &e) override {
				module->startDir[id] = dir;							///
			}

			void step() override {
				bool s = module->startDir[id] == dir;				///
				rightText = s ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Start direction"));
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "NE", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::dir, NE));			///
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "E", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::dir, E));				///
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "SE", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::dir, SE));			///
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "SW", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::dir, SW));			///
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "W", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::dir, W));				///
		menu->addChild(construct<DirectionItem>(&MenuItem::text, "NW", &DirectionItem::module, module, &DirectionItem::id, selectedId, &DirectionItem::dir, NW));			///

		struct TurnModeItem : MenuItem {
			MODULE* module;
			TURNMODE turnMode;
			int id;

			void onAction(const event::Action &e) override {
				module->turnMode[id] = turnMode;
			}

			void step() override {
				rightText = module->turnMode[id] == turnMode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Turn mode"));
		menu->addChild(construct<TurnModeItem>(&MenuItem::text, "Sixty", &TurnModeItem::module, module, &TurnModeItem::id, selectedId, &TurnModeItem::turnMode, TURNMODE::SIXTY));					///
		menu->addChild(construct<TurnModeItem>(&MenuItem::text, "Ninety", &TurnModeItem::module, module, &TurnModeItem::id, selectedId, &TurnModeItem::turnMode, TURNMODE::NINETY));
		menu->addChild(construct<TurnModeItem>(&MenuItem::text, "One-Twenty", &TurnModeItem::module, module, &TurnModeItem::id, selectedId, &TurnModeItem::turnMode, TURNMODE::ONETWENTY));			///
		menu->addChild(construct<TurnModeItem>(&MenuItem::text, "One-Eighty", &TurnModeItem::module, module, &TurnModeItem::id, selectedId, &TurnModeItem::turnMode, TURNMODE::ONEEIGHTY));

		struct OutModeItem : MenuItem {
			MODULE* module;
			OUTMODE outMode;
			int id;

			void onAction(const event::Action &e) override {
				module->outMode[id] = outMode;
			}

			void step() override {
				rightText = module->outMode[id] == outMode ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "CV mode"));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "-5..5V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::BI_5V));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "0..5V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::UNI_5V));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "0..3V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::UNI_3V));
		menu->addChild(construct<OutModeItem>(&MenuItem::text, "0..1V", &OutModeItem::module, module, &OutModeItem::id, selectedId, &OutModeItem::outMode, OUTMODE::UNI_1V));

		struct RatchetingMenuItem : MenuItem {
			MODULE* module;
			int id;

			void onAction(const event::Action& e) override {
				module->ratchetingEnabled[id] ^= true;
			}

			void step() override {
				rightText = module->ratchetingEnabled[id] ? "✔" : "";
				MenuItem::step();
			}
		};

		struct RatchetingProbMenuItem : MenuItem {
			int id;

			RatchetingProbMenuItem() {
				rightText = RIGHT_ARROW;
			}

			struct RatchetingProbItem : MenuItem {
				MODULE* module;
				float ratchetingProb;
				int id;

				void onAction(const event::Action& e) override {
					module->ratchetingSetProb(id, ratchetingProb);
				}

				void step() override {
					rightText = module->ratchetingProb[id] == ratchetingProb ? "✔" : "";
					MenuItem::step();
				}
			};

			MODULE* module;
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "50%", &RatchetingProbItem::module, module, &RatchetingProbItem::id, id, &RatchetingProbItem::ratchetingProb, 0.5f));
				menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "60%", &RatchetingProbItem::module, module, &RatchetingProbItem::id, id, &RatchetingProbItem::ratchetingProb, 0.4f));
				menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "65%", &RatchetingProbItem::module, module, &RatchetingProbItem::id, id, &RatchetingProbItem::ratchetingProb, 0.35f));
				menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "70%", &RatchetingProbItem::module, module, &RatchetingProbItem::id, id, &RatchetingProbItem::ratchetingProb, 0.3f));
				menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "80%", &RatchetingProbItem::module, module, &RatchetingProbItem::id, id, &RatchetingProbItem::ratchetingProb, 0.2f));
				menu->addChild(construct<RatchetingProbItem>(&MenuItem::text, "90%", &RatchetingProbItem::module, module, &RatchetingProbItem::id, id, &RatchetingProbItem::ratchetingProb, 0.1f));
				return menu;
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<RatchetingMenuItem>(&MenuItem::text, "Ratcheting", &RatchetingMenuItem::module, module, &RatchetingMenuItem::id, selectedId));
		menu->addChild(construct<RatchetingProbMenuItem>(&MenuItem::text, "Ratcheting probability", &RatchetingProbMenuItem::module, module, &RatchetingProbMenuItem::id, selectedId));
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<ModuleStateMenuItem<MODULE>>(&MenuItem::text, "Exit Edit-mode", &ModuleStateMenuItem<MODULE>::module, module));
	}
};


template < typename MODULE >
struct HiveScreenWidget : OpaqueWidget, HiveDrawHelper<MODULE> {
	MODULE* module;

	HiveScreenWidget(MODULE* module) {
		this->module = module;
		HiveDrawHelper<MODULE>::module = module;
		HiveDrawHelper<MODULE>::qpos = module->qPos;
		HiveDrawHelper<MODULE>::rpos = module->rPos;	
	}

	void draw(const DrawArgs& args) override {
		if (module && module->currentState == MODULESTATE::GRID) {
			HiveDrawHelper<MODULE>::draw(args, box);
			OpaqueWidget::draw(args);
		}
	}

	void onButton(const event::Button& e) override {
		if (module && module->currentState == MODULESTATE::GRID) {
			if (module->gridHovered(e.pos)) {
				if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
					RoundAxialVec c = module->pixelToHex(e.pos, module->hexSizeFactor);				///

					if (module->cellVisible(c.q, c.r, module->usedSize)) {							///
						// History
						GridCellChangeAction<MODULE>* h = new GridCellChangeAction<MODULE>;
						h->moduleId = module->id;
						h->q = c.q;																	///
						h->r = c.r;																	///
						h->oldGrid = module->grid[c.q][c.r];          							 	///
						h->oldGridCv = module->gridCv[c.q][c.r];									///

						module->gridNextState(c.q, c.r);             								///
						
						h->newGrid = module->grid[c.q][c.r];         								///
						h->newGridCv = module->gridCv[c.q][c.r];   							 	    ///
						APP->history->push(h);
					}

					e.consume(this);
				}
				if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
					createContextMenu();
					e.consume(this);
				}
			}
			OpaqueWidget::onButton(e);
		}
	}

	void createContextMenu() {
		ui::Menu* menu = createMenu();
		menu->addChild(construct<ModuleStateMenuItem<MODULE>>(&MenuItem::text, "Enter Edit-mode", &ModuleStateMenuItem<MODULE>::module, module));
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Grid"));
		GridSizeSlider<MODULE>* sizeSlider = new GridSizeSlider<MODULE>(module);
		sizeSlider->box.size.x = 200.0;
		menu->addChild(sizeSlider);

		menu->addChild(construct<GridRandomizeMenuItem<MODULE>>(&MenuItem::text, "Randomize", &GridRandomizeMenuItem<MODULE>::module, module));
		menu->addChild(construct<GridRandomizeMenuItem<MODULE>>(&MenuItem::text, "Randomize certainty", &GridRandomizeMenuItem<MODULE>::module, module, &GridRandomizeMenuItem<MODULE>::useRandom, false));
		menu->addChild(construct<GridClearMenuItem<MODULE>>(&MenuItem::text, "Clear", &GridClearMenuItem<MODULE>::module, module));
	}
};


struct HiveWidget : ThemedModuleWidget<HiveModule<ARRAY_SIZE, 4>> {
	typedef HiveModule<ARRAY_SIZE, 4> MODULE;
	HiveWidget(MODULE* module)
		: ThemedModuleWidget<HiveModule<ARRAY_SIZE, 4>>(module, "Hive") {
		setModule(module);

		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<StoermelderBlackScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		HiveGridWidget<MODULE>* gridWidget = new HiveGridWidget<MODULE>(module);
		gridWidget->box.pos = Vec(33.709f, 40.3f);
		gridWidget->box.size = Vec(BOX_WIDTH, BOX_HEIGHT);
		addChild(gridWidget);

		HiveScreenWidget<MODULE>* turnWidget = new HiveScreenWidget<MODULE>(module);
		turnWidget->box.pos = gridWidget->box.pos;
		turnWidget->box.size = gridWidget->box.size;
		addChild(turnWidget);

		HiveStartPosEditWidget<MODULE>* resetEditWidget = new HiveStartPosEditWidget<MODULE>(module);
		resetEditWidget->box.pos = turnWidget->box.pos;
		resetEditWidget->box.size = turnWidget->box.size;
		addChild(resetEditWidget);

		addInput(createInputCentered<StoermelderPort>(Vec(23.8f, 67.047f), module, MODULE::SHIFT_L1_INPUT));				///
		addInput(createInputCentered<StoermelderPort>(Vec(23.8f, 256.0f), module, MODULE::SHIFT_L2_INPUT));					///
		addInput(createInputCentered<StoermelderPort>(Vec(306.2f, 67.047f), module, MODULE::SHIFT_R1_INPUT));				///
		addInput(createInputCentered<StoermelderPort>(Vec(306.2f, 256.0f), module, MODULE::SHIFT_R2_INPUT));				///

		addInput(createInputCentered<StoermelderPort>(Vec(119.4f, 292.2f), module, MODULE::CLK_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(119.4f, 327.6f), module, MODULE::CLK_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(210.6f, 292.2f), module, MODULE::CLK_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(210.6f, 327.6f), module, MODULE::CLK_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(146.6f, 292.2f), module, MODULE::RESET_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(146.6f, 327.6f), module, MODULE::RESET_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(183.4f, 292.2f), module, MODULE::RESET_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(183.4f, 327.6f), module, MODULE::RESET_INPUT + 3));

		addInput(createInputCentered<StoermelderPort>(Vec(82.8f, 292.2f), module, MODULE::TURN_INPUT + 0));
		addInput(createInputCentered<StoermelderPort>(Vec(82.8f, 327.6f), module, MODULE::TURN_INPUT + 1));
		addInput(createInputCentered<StoermelderPort>(Vec(247.2f, 292.2f), module, MODULE::TURN_INPUT + 2));
		addInput(createInputCentered<StoermelderPort>(Vec(247.2f, 327.6f), module, MODULE::TURN_INPUT + 3));

		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(51.9f, 292.2f), module, MODULE::TRIG_LIGHT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(51.9f, 292.2f), module, MODULE::TRIG_OUTPUT + 0));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(51.9f, 327.6f), module, MODULE::TRIG_LIGHT + 1));
		addOutput(createOutputCentered<StoermelderPort>(Vec(51.9f, 327.6f), module, MODULE::TRIG_OUTPUT + 1));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(278.2f, 292.2f), module, MODULE::TRIG_LIGHT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(278.2f, 292.2f), module, MODULE::TRIG_OUTPUT + 2));
		addChild(createLightCentered<StoermelderPortLight<GreenLight>>(Vec(278.2f, 327.6f), module, MODULE::TRIG_LIGHT + 3));
		addOutput(createOutputCentered<StoermelderPort>(Vec(278.2f, 327.6f), module, MODULE::TRIG_OUTPUT + 3));

		addChild(createLightCentered<StoermelderPortLight<GreenRedLight>>(Vec(23.8f, 292.2f), module, MODULE::CV_LIGHT + 0));
		addOutput(createOutputCentered<StoermelderPort>(Vec(23.8f, 292.2f), module, MODULE::CV_OUTPUT + 0));
		addChild(createLightCentered<StoermelderPortLight<GreenRedLight>>(Vec(23.8f, 327.6f), module, MODULE::CV_LIGHT + 2));
		addOutput(createOutputCentered<StoermelderPort>(Vec(23.8f, 327.6f), module, MODULE::CV_OUTPUT + 1));
		addChild(createLightCentered<StoermelderPortLight<GreenRedLight>>(Vec(306.2f, 292.2f), module, MODULE::CV_LIGHT + 4));
		addOutput(createOutputCentered<StoermelderPort>(Vec(306.2f, 292.2f), module, MODULE::CV_OUTPUT + 2));
		addChild(createLightCentered<StoermelderPortLight<GreenRedLight>>(Vec(306.2f, 327.6f), module, MODULE::CV_LIGHT + 6));
		addOutput(createOutputCentered<StoermelderPort>(Vec(306.2f, 327.6f), module, MODULE::CV_OUTPUT + 3));
	}

	void appendContextMenu(Menu* menu) override {
		ThemedModuleWidget<MODULE>::appendContextMenu(menu);
		MODULE* module = dynamic_cast<MODULE*>(this->module);

		struct NormalizePortsItem : MenuItem {
			MODULE* module;
			
			void onAction(const event::Action& e) override {
				module->normalizePorts ^= true;
			}

			void step() override {
				rightText = module->normalizePorts ? "✔" : "";
				MenuItem::step();
			}
		};

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<NormalizePortsItem>(&MenuItem::text, "Normalize inputs to Yellow", &NormalizePortsItem::module, module));
	}
};

} // namespace Hive

Model* modelHive = createModel<Hive::HiveModule<Hive::ARRAY_SIZE, 4>, Hive::HiveWidget>("Hive");