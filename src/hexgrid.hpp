#include "rack.hpp"

enum ORIENTATION {
    FLAT = 0,
    POINTY = 1
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

CubeVec axialToCube(RoundAxialVec v) {
    float x = v.q;
    float z = v.r;
    float y = -x-z;

    return CubeVec(x, y, z);
}

CubeVec axialToCube(Vec v) {
    float x = v.x;
    float z = v.y;
    float y = -x-z;

    return CubeVec(x, y, z);
}

RoundAxialVec hexRound(Vec v) {
    CubeVec cubeVec = (axialToCube(v));

    float rx = roundf(cubeVec.x), 		ry = roundf(cubeVec.y), 	rz = roundf(cubeVec.z);
    float dx = fabsf(rx - cubeVec.x),	dy = fabsf(ry - cubeVec.y),	dz = fabsf(rz - cubeVec.z);
    
    if 		(dx > dy && dx > dz)	rx = -ry-rz;
    else if (dy > dz)				ry = -rx-rz;
    else							rz = -rx-ry;

    return RoundAxialVec(rx, rz);
}

RoundAxialVec pixelToHex(Vec pixelVec, float sizeFactor, ORIENTATION shape, Vec origin) {
    RoundAxialVec hex;
    float q, r;
    pixelVec.x -= origin.x;
    pixelVec.y -= origin.y;
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

bool gridHovered(Vec pixelVec, float sizeFactor, ORIENTATION cellShape, Vec origin) {
    RoundAxialVec hex = pixelToHex(pixelVec, sizeFactor, cellShape, origin);
    if (!hex.q && !hex.r)
        return true;
    else return false;
}

Vec hexToPixel(RoundAxialVec hex, float sizeFactor, ORIENTATION shape, Vec origin) {
    float x, y;
    if (shape == FLAT) { //!!!

    }
    else {
        x = (sqrt(3.f) * hex.q + sqrt(3.f)/2.f * hex.r) * sizeFactor + origin.x;
        y = (3.f/2.f * hex.r) * sizeFactor + origin.y;
    }
    return Vec(x, y);
}

int distance(CubeVec a, CubeVec b) {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

bool cellVisible(int q, int r, int radius) {
    bool visible = false;

    if (q > 0 && q <= radius) {
        if (r <= radius - q && r >= -radius)
            visible = true;
    }
    else if (q < 0 && q >= -radius) {
        if (r >= -radius - q && r <= radius)
            visible = true;
    }
    else if (q == 0) {
        if (r >= -radius && r <= radius)
            visible = true;
    }

    return visible;
}

bool cellVisible(RoundAxialVec pos, int radius) {
    bool visible = false;

    if (pos.q > 0 && pos.q <= radius) {
        if (pos.r <= radius - pos.q && pos.r >= -radius)
            visible = true;
    }
    else if (pos.q < 0 && pos.q >= -radius) {
        if (pos.r >= -radius - pos.q && pos.r <= radius)
            visible = true;
    }
    else if (pos.q == 0) {
        if (pos.r >= -radius && pos.r <= radius)
            visible = true;
    }

    return visible;
}

void drawHex(Vec hexCenter, float sizeFactor, ORIENTATION shape, NVGcontext* ctx) {
    Vec points[6];
    float angle;
    if (shape == FLAT) {
        for (int i = 0; i < 6; i++) {
            angle = M_PI / 180.f * (60.f * i);
            points[i].x = hexCenter.x + sizeFactor * cos(angle);
            points[i].y = hexCenter.y + sizeFactor * sin(angle);
        }
    }
    else {
        for (int i = 0; i < 6; i++) {
            angle = M_PI / 180.f * (60.f * i - 30);
            points[i].x = hexCenter.x + sizeFactor * cos(angle);
            points[i].y = hexCenter.y + sizeFactor * sin(angle);
        }
    }
    nvgMoveTo(ctx, points[5].x, points[5].y);
    for (int i = 0; i < 6; i++)
        nvgLineTo(ctx, points[i].x, points[i].y);
}

/* void drawPartialHex(float x, float y, float sizeFactor, int numPoints, DIRECTION dir, ORIENTATION shape, NVGcontext* ctx) {    
    Vec points[6];
    bool drawnPoint[6] = { false };
    if (shape == POINTY) {
        switch (dir) {
            case N:
                drawnPoint[5] = drawnPoint[0] = drawnPoint[1] = true;
                break;
            case NE:
                if (numPoints == 4)
                    drawnPoint[5] = drawnPoint[0] = drawnPoint[1] = drawnPoint[2] = true;
                else
                    drawnPoint[0] = drawnPoint[1] = drawnPoint[2] = true;
                break;
            case E:
                drawnPoint[0] = drawnPoint[1] = drawnPoint[2] = drawnPoint[3] = true;
                break;
            case SE:
                if (numPoints == 3)
                    drawnPoint[1] = drawnPoint[2] = drawnPoint[3] = true;
                else
                    drawnPoint[1] = drawnPoint[2] = drawnPoint[3] = drawnPoint[4] = true;
                break;
            case S:
                drawnPoint[2] = drawnPoint[3] = drawnPoint[4] = true;
                break;
            case SW:
                if (numPoints == 4)
                    drawnPoint[2] = drawnPoint[3] = drawnPoint[4] = drawnPoint[5] = true;
                else
                    drawnPoint[3] = drawnPoint[4] = drawnPoint[5] = true;
                break;
            case W:
                drawnPoint[3] = drawnPoint[4] = drawnPoint[5] = drawnPoint[0] = true;
                break;
            case NW:
                if (numPoints == 3)
                    drawnPoint[4] = drawnPoint[5] = drawnPoint[0] = true;
                else
                    drawnPoint[4] = drawnPoint[5] = drawnPoint[0] = drawnPoint[1] = true;
                break;
        }
    }
    float angle;
    int j = 0;
    if (shape == FLAT) {
        for (int i = 0; i < 6; i++) {
            if (drawnPoint[i]) {
                angle = M_PI / 180 * (60 * i);
                points[j].x = x + sizeFactor * cos(angle);
                points[j].y = y + sizeFactor * sin(angle);
                j++;
            }
        }
    }
    else {
        for (int i = 0; i < 6; i++) {
            if (drawnPoint[i]) {
                angle = M_PI / 180 * (60 * i - 30);
                points[j].x = x + sizeFactor * cos(angle);
                points[j].y = y + sizeFactor * sin(angle);
                j++;
            }
        }
    }
    nvgMoveTo(ctx, points[0].x, points[0].y);
    for (int i = 1; i < numPoints; i++)
        nvgLineTo(ctx, points[i].x, points[i].y);
} */

Vec* hexPoints(Vec hexCenter, float sizeFactor, ORIENTATION shape, int startPoint, int numPoints, Vec* array) {
    // Returns the vertices of a hexagon, beginning from a given starting vertex and continuing clockwise through a given number of vertices
    //      5               5   0
    //  4       0       4           1
    //  3       1           3   2
    //      2
    float angle;
    if (shape == FLAT) {
        for (int i = 0; i < numPoints; i++) {
            angle = M_PI / 180.f * (60.f * ((startPoint + i) % 6));
            array[i].x = hexCenter.x + sizeFactor * cos(angle);
            array[i].y = hexCenter.y + sizeFactor * sin(angle);
        }
    }
    else {
        for (int i = 0; i < numPoints; i++) {
            angle = M_PI / 180.f * (60.f * ((startPoint + i) % 6) - 30);
            array[i].x = hexCenter.x + sizeFactor * cos(angle);
            array[i].y = hexCenter.y + sizeFactor * sin(angle);
        }
    }
    return array;
}

struct HexCell {
    RoundAxialVec pos;
    bool diagonalState = false;

    HexCell() {}
};

template < class CELL, class CURSOR, int NUM_CURSORS, int MAX_RADIUS, ORIENTATION CELL_SHAPE >
struct HexGrid {
    int usedRadius;
    const int arraySize = 2 * MAX_RADIUS + 1;
    ORIENTATION cellShape = CELL_SHAPE;
    CELL cellMap[2 * MAX_RADIUS + 1][2 * MAX_RADIUS + 1];
    CURSOR cursor[NUM_CURSORS];
    CubeVec mirrorCenters[6];

    HexGrid() {
        for (int q = 0; q < 2 * MAX_RADIUS + 1; q++) {
            for (int r = 0; r < 2 * MAX_RADIUS + 1; r++) {
                cellMap[q][r].pos = indexToAxial(q, r);
            }
        }
    }

    HexGrid(int usedRadius) {
        setRadius(usedRadius);
        HexGrid();
    }
    
    CELL getCell(int q, int r) {
        RoundAxialVec index = axialToIndex(q, r);
        return cellMap[index.q][index.r];
    }

    CELL getCell(RoundAxialVec hex) {
        hex = axialToIndex(hex);
        return cellMap[hex.q][hex.r];
    }

    void setCell(CELL c) {
        RoundAxialVec index = axialToIndex(c.pos);
        cellMap[index.q][index.r] = c;
    }

    void setCursor(int id, RoundAxialVec pos) {
        cursor[id].pos = pos;
    }

    RoundAxialVec axialToIndex(int q, int r) {
        return RoundAxialVec(q + MAX_RADIUS, r + MAX_RADIUS);
    }

    RoundAxialVec axialToIndex(RoundAxialVec hex) {
        return RoundAxialVec(hex.q + MAX_RADIUS, hex.r + MAX_RADIUS);
    }
    
    RoundAxialVec indexToAxial(int q, int r) {
        return RoundAxialVec(q + MAX_RADIUS, r + MAX_RADIUS);
    }

    RoundAxialVec indexToAxial(RoundAxialVec hex) {
        return RoundAxialVec(hex.q + MAX_RADIUS, hex.r + MAX_RADIUS);
    }

    void setRadius(int r) {
        usedRadius = r;
        updateMirrorCenters();
    }

    void updateMirrorCenters() {
        mirrorCenters[0] = CubeVec(	-usedRadius,			2 * usedRadius + 1,		-usedRadius - 1),			/// ( x,  y,  z)
        mirrorCenters[1] = CubeVec(	usedRadius + 1,			usedRadius, 			-(2 * usedRadius + 1)),		/// (-z, -x, -y)
        mirrorCenters[2] = CubeVec(	2 * usedRadius + 1,		-usedRadius - 1,		-usedRadius),				/// ( y,  z,  x)
        mirrorCenters[3] = CubeVec(	usedRadius,				-(2 * usedRadius + 1), 	usedRadius + 1),			/// (-x, -y, -z)
        mirrorCenters[4] = CubeVec(	-usedRadius - 1,		-usedRadius,			2 * usedRadius + 1),		/// ( z,  x,  y)
        mirrorCenters[5] = CubeVec(	-(2 * usedRadius + 1),	usedRadius + 1,			usedRadius);				/// (-y, -z, -x)
    }

    void wrapCell(CELL cell) {
        CubeVec cubePos = axialToCube(cell.pos);
        for (int i = 0; i < 6; i++) {
            if (distance(cubePos, mirrorCenters[i]) <= usedRadius) {						//If distance from mirror center i is less than distance to grid center
                cell.pos.q -= mirrorCenters[i].x;
                cell.pos.r -= mirrorCenters[i].z;
            }
        }
        setCell(cell);
    }
    
    void wrapCursor(int id) {
        CubeVec c = axialToCube(cursor[id].pos);
        for (int i = 0; i < 6; i++) {
            if (distance(c, mirrorCenters[i]) <= usedRadius) {						//If distance from mirror center i is less than distance to grid center
                cursor[id].pos.q -= mirrorCenters[i].x;
                cursor[id].pos.r -= mirrorCenters[i].z;
            }
        }
    }

    void moveCell(CELL cell, int direction) {
        // Direction is expressed as relative to the center of a clock, 0 through 11.
        // For flat-top hexagons, odd-numbered directions are oriented between neighboring cells and thus alternate: first clockwise, then counter
        // For pointy-top hexagons, it is the even-numbered directions which demand these alternating movements
        RoundAxialVec index = axialToIndex(cell.pos);
        if (cellShape == FLAT) {
            switch (direction) {
                case 0:
                    cell.r -= 1;
                    break;
                case 1:
                    if (!cell.diagonalState) {
                        cell.q += 1;
                        cell.r -= 1;
                    }
                    else
                        cell.r -= 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 2:
                    cell.q += 1;
                    cell.r -= 1;
                    break;
                case 3:
                    if (!cell.diagonalState)
                        cell.q += 1;
                    else {
                        cell.q += 1;
                        cell.r -= 1;
                    }
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 4:
                    cell.q += 1;
                    break;
                case 5:
                    if (!cell.diagonalState)
                        cell.r += 1;
                    else
                        cell.q += 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 6:
                    cell.r += 1;
                    break;
                case 7:
                    if (!cell.diagonalState) {
                        cell.q -= 1;
                        cell.r += 1;
                    }
                    else
                        cell.r += 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 8:
                    cell.q -= 1;
                    cell.r += 1;
                    break;
                case 9:
                    if (!cell.diagonalState)
                        cell.q -= 1;
                    else {
                        cell.q -= 1;
                        cell.r += 1;
                    }
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 10:
                    cell.q -= 1;
                    break;
                case 11:
                    if (!cell.diagonalState)
                        cell.r -= 1;
                    else
                        cell.q -= 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
            }
        }
        else {
            switch (direction) {
                case 0:
                    if (!cell.diagonalState) {
                        cell.q += 1;
                        cell.r -= 1;
                    }
                    else
                        cell.r -= 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 1:
                    cell.q += 1;
                    cell.r -= 1;
                    break;
                case 2:
                    if (!cell.diagonalState)
                        cell.q += 1;
                    else {
                        cell.q += 1;
                        cell.r -= 1;
                    }
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 3:
                    cell.q += 1;
                    break;
                case 4:
                    if (!cell.diagonalState)
                        cell.r += 1;
                    else
                        cell.q += 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 5:
                    cell.r += 1;
                    break;
                case 6:
                    if (!cell.diagonalState) {
                        cell.q -= 1;
                        cell.r += 1;
                    }
                    else
                        cell.r += 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 7:
                    cell.q -= 1;
                    cell.r += 1;
                    break;
                case 8:
                    if (!cell.diagonalState)
                        cell.q -= 1;
                    else {
                        cell.q -= 1;
                        cell.r += 1;
                    }
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 9:
                    cell.q -= 1;
                    break;
                case 10:
                    if (!cell.diagonalState)
                        cell.r -= 1;
                    else
                        cell.q -= 1;
                    cell.diagonalState = !cell.diagonalState;
                    break;
                case 11:
                    cell.r -= 1;
                    break;
            }
        }
        if (!cellVisible(cell.pos, usedRadius))
            wrapCell(cell);
        else 
            setCell(cell);
    }

    void moveCursor(int id, int direction) {
        // Direction is expressed as relative to the center of a clock, 0 through 11.
        // For flat-top hexagons, odd-numbered directions are oriented between neighboring cells and thus alternate: first clockwise, then counter
        // For pointy-top hexagons, it is the even-numbered directions which demand these alternating movements
        if (CELL_SHAPE == FLAT) {
            switch (direction) {
                case 0:
                    cursor[id].pos.r -= 1;
                    break;
                case 1:
                    if (!cursor[id].diagonalState) {
                        cursor[id].pos.q += 1;
                        cursor[id].pos.r -= 1;
                    }
                    else
                        cursor[id].pos.r -= 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 2:
                    cursor[id].pos.q += 1;
                    cursor[id].pos.r -= 1;
                    break;
                case 3:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.q += 1;
                    else {
                        cursor[id].pos.q += 1;
                        cursor[id].pos.r -= 1;
                    }
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 4:
                    cursor[id].pos.q += 1;
                    break;
                case 5:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.r += 1;
                    else
                        cursor[id].pos.q += 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 6:
                    cursor[id].pos.r += 1;
                    break;
                case 7:
                    if (!cursor[id].diagonalState) {
                        cursor[id].pos.q -= 1;
                        cursor[id].pos.r += 1;
                    }
                    else
                        cursor[id].pos.r += 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 8:
                    cursor[id].pos.q -= 1;
                    cursor[id].pos.r += 1;
                    break;
                case 9:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.q -= 1;
                    else {
                        cursor[id].pos.q -= 1;
                        cursor[id].pos.r += 1;
                    }
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 10:
                    cursor[id].pos.q -= 1;
                    break;
                case 11:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.r -= 1;
                    else
                        cursor[id].pos.q -= 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
            }
        }
        else {
            switch (direction) {
                case 0:
                    if (!cursor[id].diagonalState) {
                        cursor[id].pos.q += 1;
                        cursor[id].pos.r -= 1;
                    }
                    else
                        cursor[id].pos.r -= 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 1:
                    cursor[id].pos.q += 1;
                    cursor[id].pos.r -= 1;
                    break;
                case 2:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.q += 1;
                    else {
                        cursor[id].pos.q += 1;
                        cursor[id].pos.r -= 1;
                    }
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 3:
                    cursor[id].pos.q += 1;
                    break;
                case 4:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.r += 1;
                    else
                        cursor[id].pos.q += 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 5:
                    cursor[id].pos.r += 1;
                    break;
                case 6:
                    if (!cursor[id].diagonalState) {
                        cursor[id].pos.q -= 1;
                        cursor[id].pos.r += 1;
                    }
                    else
                        cursor[id].pos.r += 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 7:
                    cursor[id].pos.q -= 1;
                    cursor[id].pos.r += 1;
                    break;
                case 8:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.q -= 1;
                    else {
                        cursor[id].pos.q -= 1;
                        cursor[id].pos.r += 1;
                    }
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 9:
                    cursor[id].pos.q -= 1;
                    break;
                case 10:
                    if (!cursor[id].diagonalState)
                        cursor[id].pos.r -= 1;
                    else
                        cursor[id].pos.q -= 1;
                    cursor[id].diagonalState = !cursor[id].diagonalState;
                    break;
                case 11:
                    cursor[id].pos.r -= 1;
                    break;
            }
        }
        if (!cellVisible(cursor[id].pos.q, cursor[id].pos.r, usedRadius))
            wrapCursor(id);
    }

    void drawGrid(float cellSizeFactor, Vec gridOrigin, NVGcontext* ctx) {
        Vec hex;
        if (cellShape == FLAT) {
            // TODO                
        }
        else {
            for (int q = -usedRadius; q <= usedRadius; q++) {
                for (int r = -usedRadius; r <= usedRadius; r++) {
                    if (cellVisible(q, r, usedRadius)) {
                        hex = hexToPixel(RoundAxialVec(q, r), cellSizeFactor, POINTY, gridOrigin);
                        drawHex(hex, cellSizeFactor, POINTY, ctx);
                    }
                }
            }
        }
    }

    void drawGridOutline(float cellSizeFactor, Vec gridOrigin, NVGcontext* ctx) {
        int numPoints = 18 + (2 * (usedRadius - 1)) * 6;    // 3 for each corner hex, 2 for each edge hex
        Vec borderPoint[numPoints];

        int numHexes = 6 * usedRadius;
        RoundAxialVec borderHex[numHexes];

        int q = -usedRadius, r = 0;
        for (int i = 0; i < 6; i++) {                                   // For each edge
            for (int j = 0; j < usedRadius; j++) {                      // For each hex on a given edge
                borderHex[i * usedRadius + j] = RoundAxialVec(q, r);
                switch (i) {
                    case 0:
                        q++;
                        r--;
                        break;
                    case 1:
                        q++;
                        break;
                    case 2:
                        r++;
                        break;
                    case 3:
                        q--;
                        r++;
                        break;
                    case 4:
                        q--;
                        break;
                    case 5:
                        r--;
                        break;
                }
            }
        }

        if (cellShape == FLAT) {

        }
        else {
            int n = 0;
            int curHexPoints;
            for (int i = 0; i < 6; i++) {                   // For each edge
                for (int j = 0; j < usedRadius; j++) {      // For each hex on a given edge
                    Vec points[4];
                    Vec* p;
                    if (j == 0) {   // Corner hex
                        curHexPoints = 3;
                        p = hexPoints(hexToPixel(borderHex[usedRadius * i], cellSizeFactor, cellShape, gridOrigin), cellSizeFactor, cellShape, (i + 2) % 6, curHexPoints, points);
                    }
                    else {
                        curHexPoints = 2;
                        p = hexPoints(hexToPixel(borderHex[usedRadius * i + j], cellSizeFactor, cellShape, gridOrigin), cellSizeFactor, cellShape, (i + 3) % 6, curHexPoints, points);  
                    }
                    for (int k = 0; k < curHexPoints; k++) {
                        borderPoint[n] = p[k];
                        n++;
                    }
                }
            }
        }
        nvgMoveTo(ctx, borderPoint[numPoints - 1].x, borderPoint[numPoints - 1].y);
        for (int i = 0; i < numPoints; i++)
            nvgLineTo(ctx, borderPoint[i].x, borderPoint[i].y);
    }
};