#include "plugin.hpp"

enum CELLTYPE {
    EMPTY = 0,
    SAND = 1
};

enum ORIENTATION {
    FLAT = 0,
    POINTY = 1
};

struct HexCell {
    bool diagonalState = false;

    HexCell() {}
};

template < typename CELL, int SIZE, ORIENTATION CELL_SHAPE >
struct HexGrid {
    CELL grid[SIZE][SIZE];
    CubeVec mirrorCenters[6];
    int maxRadius = ((SIZE - 1) / 2) + 1;
    int usedRadius;
    int usedSize;

    HexGrid(int usedRadius) {
        usedSize = 2 * usedRadius + 1;
        updateMirrorCenters();
    }
    
    void setRadius(int r) {
        radius = r;

        updateMirrorCenters();
    }

    void updateMirrorCenters() {
        mirrorCenters[0] = CubeVec(	-(radius - 1),				2 * (radius - 1) + 1,		-(radius - 1) - 1),				/// ( x,  y,  z)
        mirrorCenters[1] = CubeVec(	(radius - 1) + 1,			(radius - 1), 				-(2 * (radius - 1) + 1)),		/// (-z, -x, -y)
        mirrorCenters[2] = CubeVec(	2 * (radius - 1) + 1,		-(radius - 1) - 1,			-(radius - 1)),					/// ( y,  z,  x)
        mirrorCenters[3] = CubeVec(	(radius - 1),				-(2 * (radius - 1) + 1), 	(radius - 1) + 1),				/// (-x, -y, -z)
        mirrorCenters[4] = CubeVec(	-(radius - 1) - 1,			-(radius - 1),				2 * (radius - 1) + 1),			/// ( z,  x,  y)
        mirrorCenters[5] = CubeVec(	-(2 * (radius - 1 ) + 1),	(radius - 1) + 1,			(radius - 1));					/// (-y, -z, -x)
    }

    RoundAxialVec wrapHex(int q, int r) {
        CubeVec c = axialToCube(Vec(q, r));
        for (int i = 0; i < 6; i++) {
            if (distance(c, mirrorCenters[i]) <= (radius - 1)) {						//If distance from mirror center i is less than distance to grid center
                q -= mirrorCenters[i].x;
                r -= mirrorCenters[i].z;
            }
        }
        return RoundAxialVec(q, r);
    }

    RoundAxialVec moveHex(int q, int r, int direction) {
        if (CELL_SHAPE == FLAT) {
            switch (direction) {
                case 0:
                    r -= 1;
                    break;
                case 1:
                    if (!grid[q][r].diagonalState) {
                        q += 1;
                        r -= 1;
                    }
                    else
                        r -= 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 2:
                    q += 1;
                    r -= 1;
                    break;
                case 3:
                    if (!grid[q][r].diagonalState)
                        q += 1;
                    else {
                        q += 1;
                        r -= 1;
                    }
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 4:
                    q += 1;
                    break;
                case 5:
                    if (!grid[q][r].diagonalState)
                        r += 1;
                    else
                        q += 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 6:
                    r += 1;
                    break;
                case 7:
                    if (!grid[q][r].diagonalState) {
                        q -= 1;
                        r += 1;
                    }
                    else
                        r += 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 8:
                    q -= 1;
                    r += 1;
                    break;
                case 9:
                    if (!grid[q][r].diagonalState)
                        q -= 1;
                    else {
                        q -= 1;
                        r += 1;
                    }
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 10:
                    q -= 1;
                    break;
                case 11:
                    if (!grid[q][r].diagonalState)
                        r -= 1;
                    else
                        q -= 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
            }
        }
        else {
            switch (direction) {
                case 0:
                    if (!grid[q][r].diagonalState) {
                        q += 1;
                        r -= 1;
                    }
                    else
                        r -= 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 1:
                    q += 1;
                    r -= 1;
                    break;
                case 2:
                    if (!grid[q][r].diagonalState)
                        q += 1;
                    else {
                        q += 1;
                        r -= 1;
                    }
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 3:
                    q += 1;
                    break;
                case 4:
                    if (!grid[q][r].diagonalState)
                        r += 1;
                    else
                        q += 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 5:
                    r += 1;
                    break;
                case 6:
                    if (!grid[q][r].diagonalState) {
                        q -= 1;
                        r += 1;
                    }
                    else
                        r += 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 7:
                    q -= 1;
                    r += 1;
                    break;
                case 8:
                    if (!grid[q][r].diagonalState)
                        q -= 1;
                    else {
                        q -= 1;
                        r += 1;
                    }
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 9:
                    q -= 1;
                    break;
                case 10:
                    if (!grid[q][r].diagonalState)
                        r -= 1;
                    else
                        q -= 1;
                    grid[q][r].diagonalState = !grid[q][r].diagonalState;
                    break;
                case 11:
                    r -= 1;
                    break;
            }
        }
        if (!cellVisible(q, r, usedSize))
            wrapHex(q, r);
        return RoundAxialVec(q, r);
    }

    HexGrid() {}
    HexGrid(int usedRadius) : usedRadius(usedRadius) {}
};

struct CubeVec {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	CubeVec() {}
	CubeVec(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct RoundAxialVec {
	int q = 0;
	int r = 0;

	RoundAxialVec() {}
	RoundAxialVec(int q, int r) : q(q), r(r) {}
};

CubeVec axialToCube(Vec axialVec) {
    float x = axialVec.x;
    float z = axialVec.y;
    float y = -x-z;

    return CubeVec(x, y, z);
}

RoundAxialVec hexRound(Vec axialVec) {
    CubeVec cubeVec = (axialToCube(axialVec));

    float rx = roundf(cubeVec.x), 		ry = roundf(cubeVec.y), 	rz = roundf(cubeVec.z);
    float dx = fabsf(rx - cubeVec.x),	dy = fabsf(ry - cubeVec.y),	dz = fabsf(rz - cubeVec.z);
    
    if 		(dx > dy && dx > dz)	rx = -ry-rz;
    else if (dy > dz)				ry = -rx-rz;
    else							rz = -rx-ry;

    return RoundAxialVec(rx, rz);
}

RoundAxialVec pixelToHex(Vec pixelVec, float sizeFactor, ORIENTATION shape) {
    RoundAxialVec hex;
    float q, r;
    if (shape == FLAT) {
        q = (2.f/3.f * pixelVec.x) / sizeFactor;
        r = (-1.f/3.f * pixelVec.x  +  sqrt(3.f)/3.f * pixelVec.y) / sizeFactor;
    }
    else {
        q = (sqrt(3.f)/3.f * pixelVec.x - (1.f/3.f) * pixelVec.y) / sizeFactor;
        r = ((2.f/3.f) * pixelVec.y) / sizeFactor;
    }
    hex = hexRound(Vec(q, r));
    return hex;
}

bool gridHovered(Vec pixelVec, float sizeFactor, ORIENTATION shape) {																					///
    RoundAxialVec hex = pixelToHex(pixelVec, sizeFactor, shape);
    if (!hex.q && !hex.r)
        return true;
    else return false;
}

Vec hexToPixel(Vec axialVec, float sizeFactor, ORIENTATION shape) {																							///
    float x, y;
    if (shape == FLAT) { //!!!

    }
    else {
        x = (sqrt(3.f) * axialVec.x + sqrt(3.f)/2.f * axialVec.y) * sizeFactor;
        y = (3.f/2.f * axialVec.y) * sizeFactor;
    }
    return Vec(x, y);
}

int distance(CubeVec a, CubeVec b) {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

bool cellVisible(int q, int r, int arraySize) {
    bool visible = false;
    int radius = ((arraySize - 1) / 2) + 1;

    if ((q >= 0 && r >= 0) && (q < arraySize && r < arraySize)) {
        if (q > radius - 1) {
            if (r < arraySize - abs((radius - 1) - q))
                visible = true;
        }
        else if (q < radius - 1) {
            if (r >= arraySize - (arraySize - std::abs((radius - 1) - q)))
                visible = true;
        }
        else if (r < arraySize)
            visible = true;
    }
    return visible;
}

void drawHex(float x, float y, float sizeFactor, ORIENTATION shape, NVGcontext* ctx) {
    Vec points[6];
    float angle;
    if (shape == FLAT) {
        for (int i = 0; i < 6; i++) {
            angle = M_PI / 180 * (60 * i);
            points[i].x = x + sizeFactor * cos(angle);
            points[i].y = y + sizeFactor * sin(angle);
        }
    }
    else {
        for (int i = 0; i < 6; i++) {
            angle = M_PI / 180 * (60 * i - 30);
            points[i].x = x + sizeFactor * cos(angle);
            points[i].y = y + sizeFactor * sin(angle);
        }
    }
    nvgMoveTo(ctx, points[0].x, points[0].y);
    for (int i = 1; i < 6; i++)
        nvgLineTo(ctx, points[i].x, points[i].y);
}