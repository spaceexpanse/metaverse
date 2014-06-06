// ==============================================================
//   ORBITER VISUALISATION PROJECT (OVP)
//   Copyright (C) 2006-2014 Martin Schweiger
//   Dual licensed under GPL v3 and LGPL v3
// ==============================================================

// ==============================================================
// tilemanager2.cpp
// Rendering of planetary surfaces using texture tiles at
// variable resolutions (new version).
// ==============================================================

#include "Tilemgr2.h"
#include "Texture.h"
#include "D3D9Config.h"
#include "D3D9Effect.h"

#include <stack>

// =======================================================================
// Externals

static int maxres = 14;    // max tree resolution depth    
static int patch_res = 32; // patch node grid dimensions
static int elev_stride = patch_res*8+3;
static TEXCRDRANGE2 fullrange = {0,1,0,1};

int SURF_MAX_PATCHLEVEL2 = 18; // move this somewhere else


// =======================================================================
// Class Tile

Tile::Tile (TileManager2Base *_mgr, int _lvl, int _ilat, int _ilng)
: mgr(_mgr), lvl(_lvl), ilat(_ilat), ilng(_ilng)
{
	mesh = 0;
	tex = 0;
	mean_elev = 0.0;
	texrange = fullrange;
	owntex = true;
	lngnbr_lvl = latnbr_lvl = dianbr_lvl = _lvl;
	state = Invalid;
	cnt = Centre();
}

// -----------------------------------------------------------------------

Tile::~Tile ()
{
	state = Invalid;
	mgr = NULL;
	if (mesh) delete mesh;
	if (tex && owntex) tex->Release();
}

// -----------------------------------------------------------------------

bool Tile::PreDelete ()
{
	switch (state) {
	case Loading:
		return false;                // locked
	case InQueue:
		mgr->loader->Unqueue (this); // remove from load queue
		// fall through
	default:
		return true;
	}
}

// -----------------------------------------------------------------------

bool Tile::GetParentSubTexRange (TEXCRDRANGE2 *subrange)
{
	Tile *parent = getParent();
	if (!parent) return false; // haven't got a parent

	if (!(ilat&1)) { // left column
		subrange->tvmin = parent->texrange.tvmin;
		subrange->tvmax = (parent->texrange.tvmin+parent->texrange.tvmax)*0.5f;
	} else {         // right column
		subrange->tvmin = (parent->texrange.tvmin+parent->texrange.tvmax)*0.5f;
		subrange->tvmax = parent->texrange.tvmax;
	}
	if (!(ilng&1)) { // bottom row
		subrange->tumin = parent->texrange.tumin;
		subrange->tumax = (parent->texrange.tumin+parent->texrange.tumax)*0.5f;
	} else {         // top row
		subrange->tumin = (parent->texrange.tumin+parent->texrange.tumax)*0.5f;
		subrange->tumax = parent->texrange.tumax;
	}
	return true;
}

// -----------------------------------------------------------------------
// Check if tile bounding box intersects the viewing frustum
// given the transformation matrix transform

bool Tile::InView (const MATRIX4 &transform)
{
	//return true;

	if (!lvl) return true; // no good check for this yet

	if (!mesh) return true; // DEBUG : TEMPORARY

	bool bx1, bx2, by1, by2, bz1, bbvis;
	int v;
	bx1 = bx2 = by1 = by2 = bz1 = bbvis = false;
	double hx, hy, hz;
	for (v = 0; v < 8; v++) {
		VECTOR4 vt = mul (mesh->Box[v], transform);
		hx = vt.x/vt.w, hy = vt.y/vt.w, hz = vt.z/vt.w;
		if (hz <= 1.0) hx = -hx, hy = -hy;
		if (hz >  0.0) bz1 = true;
		if (hx > -1.0) bx1 = true;
		if (hx <  1.0) bx2 = true;
		if (hy > -1.0) by1 = true;
		if (hy <  1.0) by2 = true;
		if (bbvis = bx1 && bx2 && by1 && by2 && bz1) break;
	}
	return bbvis;
}

// -----------------------------------------------------------------------
// returns the direction of the tile centre from the planet centre in local
// planet coordinates

VECTOR3 Tile::Centre () const
{
	int nlat = 1 << lvl;
	int nlng = 2 << lvl;
	double cntlat = PI05 - PI * ((double)ilat+0.5)/(double)nlat, slat = sin(cntlat), clat = cos(cntlat);
	double cntlng = PI2  * ((double)ilng+0.5)/(double)nlng + PI, slng = sin(cntlng), clng = cos(cntlng);
	return _V(clat*clng,  slat,  clat*slng);
}


// -----------------------------------------------------------------------

void Tile::Extents (double *latmin, double *latmax, double *lngmin, double *lngmax) const
{
	int nlat = 1 << lvl;
	int nlng = 2 << lvl;
	*latmin = PI * (0.5 - (double)(ilat+1)/(double)nlat);
	*latmax = PI * (0.5 - (double)ilat/(double)nlat);
	*lngmin = PI2 * (double)(ilng-nlng/2)/(double)nlng;
	*lngmax = PI2 * (double)(ilng-nlng/2+1)/(double)nlng;
}

// -----------------------------------------------------------------------

VBMESH *Tile::CreateMesh_quadpatch (int grdlat, int grdlng, INT16 *elev, double globelev,
	const TEXCRDRANGE2 *range, bool shift_origin, VECTOR3 *shift)
{
	const float TEX2_MULTIPLIER = 4.0f; // was: 16.0f
	const float c1 = 1.0f, c2 = 0.0f;   // -1.0f/512.0f; // assumes 256x256 texture patches
	int i, j, n, nofs0, nofs1;
	int nlng = 2 << lvl;
	int nlat = 1 << lvl;
	bool north = (ilat < nlat/2);

	double lat, slat, clat, lng, slng, clng, eradius, dx, dy;
	double minlat = PI * (double)(nlat/2-ilat-1)/(double)nlat;
	double maxlat = PI * (double)(nlat/2-ilat)/(double)nlat;
	double minlng = 0;
	double maxlng = PI2/(double)nlng;
	double radius = mgr->obj_size;
	VECTOR3 pos, tpos, nml;
	if (!range) range = &fullrange;
	float turange = range->tumax-range->tumin;
	float tvrange = range->tvmax-range->tvmin;

	int nvtx = (grdlat+1)*(grdlng+1);         // patch mesh node grid
	int nvtxbuf = nvtx + grdlat+1 + grdlng+1; // add buffer for storage of edges (for elevation matching)
	VERTEX_2TEX *vtx = new VERTEX_2TEX[nvtxbuf];

	// create transformation for bounding box
	// we define the local coordinates for the patch so that the x-axis points
	// from (minlng,minlat) corner to (maxlng,minlat) corner (origin is halfway between)
	// y-axis points from local origin to middle between (minlng,maxlat) and (maxlng,maxlat)
	// bounding box is created in this system and then transformed back to planet coords.
	double clat0 = cos(minlat), slat0 = sin(minlat);
	double clng0 = cos(minlng), slng0 = sin(minlng);
	double clat1 = cos(maxlat), slat1 = sin(maxlat);
	double clng1 = cos(maxlng), slng1 = sin(maxlng);
	VECTOR3 ex = {clat0*clng1 - clat0*clng0, 0, clat0*slng1 - clat0*slng0}; normalise (ex);
	VECTOR3 ey = {0.5*(clng0+clng1)*(clat1-clat0), slat1-slat0, 0.5*(slng0+slng1)*(clat1-clat0)}; normalise (ey);
	VECTOR3 ez = crossp (ey, ex);
	MATRIX3 R = {ex.x, ex.y, ex.z,  ey.x, ey.y, ey.z,  ez.x, ez.y, ez.z};
	VECTOR3 pref = {radius*clat0*0.5*(clng1+clng0), radius*slat0, radius*clat0*0.5*(slng1+slng0)}; // origin
	VECTOR3 tpmin, tpmax; 

	// patch translation vector
	if (shift_origin) {
		dx = (north ? clat0:clat1)*radius;
		dy = (north ? slat0:slat1)*radius;
	} else {
		dx = dy = 0.0;
	}
	if (shift) {
		shift->x = dx;
		shift->y = dy;
		shift->z = 0.0;
	}

	// create the vertices
	for (i = n = 0; i <= grdlat; i++) {
		lat = minlat + (maxlat-minlat) * (double)i/(double)grdlat;
		slat = sin(lat), clat = cos(lat);
		for (j = 0; j <= grdlng; j++) {
			lng = minlng + (maxlng-minlng) * (double)j/(double)grdlng;
			slng = sin(lng), clng = cos(lng);

			eradius = radius + globelev; // radius including node elevation
			if (elev) eradius += (double)elev[(i+1)*elev_stride + j+1];
			nml = _V(clat*clng, slat, clat*slng);
			pos = nml*eradius;
			tpos = mul (R, pos-pref);
			if (!n) {
				tpmin = tpos;
				tpmax = tpos;
			} else {
				if      (tpos.x < tpmin.x) tpmin.x = tpos.x;
			    else if (tpos.x > tpmax.x) tpmax.x = tpos.x;
				if      (tpos.y < tpmin.y) tpmin.y = tpos.y;
				else if (tpos.y > tpmax.y) tpmax.y = tpos.y;
				if      (tpos.z < tpmin.z) tpmin.z = tpos.z;
				else if (tpos.z > tpmax.z) tpmax.z = tpos.z;
			}
			vtx[n].x = D3DVAL(pos.x - dx); vtx[n].nx = D3DVAL(nml.x);
			vtx[n].y = D3DVAL(pos.y - dy); vtx[n].ny = D3DVAL(nml.y);
			vtx[n].z = D3DVAL(pos.z);      vtx[n].nz = D3DVAL(nml.z);

			vtx[n].tu0 = D3DVAL((c1*j)/grdlng+c2); // overlap to avoid seams
			vtx[n].tv0 = D3DVAL(grdlat-i)/D3DVAL(grdlat);
			vtx[n].tu1 = vtx[n].tu0 * TEX2_MULTIPLIER;
			vtx[n].tv1 = vtx[n].tv0 * TEX2_MULTIPLIER;

			// map texture coordinates to subrange
			vtx[n].tu0 = vtx[n].tu0*turange + range->tumin;
			vtx[n].tv0 = vtx[n].tv0*tvrange + range->tvmin;
			n++;
		}
	}

	// create the face indices
	int nidx = 2*grdlat*grdlng * 3;
	WORD *idx = new WORD[nidx];

	if (elev) { // do adaptive orientation of cell diagonal
		INT16 *elev1, *elev2, *elev1n, *elev2n, err1, err2;
		for (i = n = nofs0 = 0; i < grdlat; i++) {
			nofs1 = nofs0+grdlng+1;
			for (j = 0; j < grdlng; j++) {
				elev1  = elev + elev_stride+2+j+elev_stride*i;
				elev2  = elev1 + elev_stride-1;
				elev1n = elev1 - elev_stride+1;
				elev2n = elev2 + elev_stride-1;
				err1 = abs(*elev1 * 2 - *elev2 - *elev1n) + abs(*elev2 * 2 - *elev1 - *elev2n);
				elev1  = elev + elev_stride+1+j+elev_stride*i;
				elev2  = elev1 + elev_stride+1;
				elev1n = elev1 - elev_stride-1;
				elev2n = elev2 + elev_stride+1;
				err2 = abs(*elev1 * 2 - *elev2 - *elev1n) + abs(*elev2 * 2 - *elev1 - *elev2n);
				if (err1 < err2) {
					idx[n++] = nofs0+j;
					idx[n++] = nofs1+j;
					idx[n++] = nofs0+j+1;
					idx[n++] = nofs0+j+1;
					idx[n++] = nofs1+j;
					idx[n++] = nofs1+j+1;
				} else {
					idx[n++] = nofs0+j;
					idx[n++] = nofs1+j+1;
					idx[n++] = nofs0+j+1;
					idx[n++] = nofs1+j+1;
					idx[n++] = nofs0+j;
					idx[n++] = nofs1+j;
				}
			}
			nofs0 = nofs1;
		}
	} else { // no elevation => no adaptation of cell diagonals necessary
		for (i = n = nofs0 = 0; i < grdlat; i++) {
			nofs1 = nofs0+grdlng+1;
			for (j = 0; j < grdlng; j++) {
				idx[n++] = nofs0+j;
				idx[n++] = nofs1+j;
				idx[n++] = nofs0+j+1;
				idx[n++] = nofs1+j+1;
				idx[n++] = nofs0+j+1;
				idx[n++] = nofs1+j;
			}
			nofs0 = nofs1;
		}
	}

	// regenerate normals for terrain
	if (elev) {
		const double shade_exaggerate = 1.0; // 1 = normal, <1 = more dramatic landscape shadows
		double dy, dz, dydz, nz_x, ny_x, nx1, ny1, nz1;
		int en;
		dy = radius * PI/(nlat*grdlat);  // y-distance between vertices
		ny_x = shade_exaggerate*dy;
		for (i = n = 0; i <= grdlat; i++) {
			lat = minlat + (maxlat-minlat) * (double)i/(double)grdlat;
			slat = sin(lat), clat = cos(lat);
			dz = radius * PI2*cos(lat) / (nlng*grdlng); // z-distance between vertices on unit sphere
			dydz = dy*dz;
			nz_x = shade_exaggerate*dz;
			for (j = 0; j <= grdlng; j++) {
				lng = minlng + (maxlng-minlng) * (double)j/(double)grdlng;
				slng = sin(lng), clng = cos(lng);
				en = (i+1)*elev_stride + (j+1);

				// This version avoids the normalisation of the 4 intermediate face normals
				// It's faster and doesn't seem to make much difference
				VECTOR3 nml = {2.0*dydz, dz*(elev[en-elev_stride]-elev[en+elev_stride]), dy*(elev[en-1]-elev[en+1])};
				normalise (nml);
				// rotate into place
				nx1 = nml.x*clat - nml.y*slat;
				ny1 = nml.x*slat + nml.y*clat;
				nz1 = nml.z;
				vtx[n].nx = (float)(nx1*clng - nz1*slng);
				vtx[n].ny = (float)(ny1);
				vtx[n].nz = (float)(nx1*slng + nz1*clng);
				n++;
			}
		}
	}

	// store the adaptable edges in the separate vertex area
	for (i = 0, n = nvtx; i <= grdlat; i++) // store left or right edge
		vtx[n++] = vtx[i*(grdlng+1) + ((ilng&1) ? grdlng:0)];
	for (i = 0; i <= grdlng; i++) // store top or bottom edge
		vtx[n++] = vtx[i + ((ilat&1) ? 0 : (grdlng+1)*grdlat)];

	// create the mesh
	VBMESH *mesh = new VBMESH(mgr);
	mesh->vtx = vtx;
	mesh->nv  = nvtx;
	mesh->idx = idx;
	mesh->nf  = nidx/3;

	// set bounding box for visibility calculations
	pref.x -= dx;
	pref.y -= dy;
	
	mesh->Box[0] = _V(tmul (R, _V(tpmin.x, tpmin.y, tpmin.z)) + pref);
	mesh->Box[1] = _V(tmul (R, _V(tpmax.x, tpmin.y, tpmin.z)) + pref);
	mesh->Box[2] = _V(tmul (R, _V(tpmin.x, tpmax.y, tpmin.z)) + pref);
	mesh->Box[3] = _V(tmul (R, _V(tpmax.x, tpmax.y, tpmin.z)) + pref);
	mesh->Box[4] = _V(tmul (R, _V(tpmin.x, tpmin.y, tpmax.z)) + pref);
	mesh->Box[5] = _V(tmul (R, _V(tpmax.x, tpmin.y, tpmax.z)) + pref);
	mesh->Box[6] = _V(tmul (R, _V(tpmin.x, tpmax.y, tpmax.z)) + pref);
	mesh->Box[7] = _V(tmul (R, _V(tpmax.x, tpmax.y, tpmax.z)) + pref);

	mesh->MapVertices(TileManager2Base::pDev);

	return mesh;
}

// -----------------------------------------------------------------------

VBMESH *Tile::CreateMesh_hemisphere (int grd, INT16 *elev, double globelev)
{
	const int texres = 512;
	double radius = mgr->obj_size + globelev;
	double eradius, slat, clat, slng, clng;
	VECTOR3 pos, nml;

	// Allocate memory for the vertices and indices
	int         nVtx = grd*(grd+1)+2;
	int         nIdx = 6*grd*grd;
	VERTEX_2TEX *Vtx = new VERTEX_2TEX[nVtx];
	WORD*       Idx = new WORD[nIdx];

	// Counters
    WORD x, y, nvtx = 0, nidx = 0;
	VERTEX_2TEX *vtx = Vtx;
	WORD *idx = Idx;

	// Angle deltas for constructing the sphere's vertices
    double fDAng   = PI / grd;
    double lat = fDAng;
	DWORD x1 = grd;
	DWORD x2 = x1+1;
	FLOAT du = 0.5f/(FLOAT)texres;
	FLOAT a  = (1.0f-2.0f*du)/(FLOAT)x1;

    // Make the middle of the sphere
    for (y = 0; y < grd; y++) {
		slat = sin(lat), clat = cos(lat);
		float tv = (float)(lat/PI);

        for (x = 0; x < x2; x++) {
            double lng = x*fDAng - PI;  // subtract Pi to wrap at +-180�
			if (ilng) lng += PI;
			slng = sin(lng), clng = cos(lng);
			eradius = radius; // TODO: elevation
			nml = _V(slat*clng, clat, slat*slng);
			pos = nml*eradius;
			vtx->x = D3DVAL(pos.x);  vtx->nx = D3DVAL(nml.x);
			vtx->y = D3DVAL(pos.y);  vtx->ny = D3DVAL(nml.y);
			vtx->z = D3DVAL(pos.z);  vtx->nz = D3DVAL(nml.z);
			FLOAT tu = a*(FLOAT)x + du;
			vtx->tu0 = vtx->tu1 = tu;
			vtx->tv0 = vtx->tv1 = tv;
			vtx++;
			nvtx++;
        }
        lat += fDAng;
    }

    for (y = 0; y < grd-1; y++) {
        for (x = 0; x < x1; x++) {
            *idx++ = (WORD)( (y+0)*x2 + (x+0) );
            *idx++ = (WORD)( (y+0)*x2 + (x+1) );
            *idx++ = (WORD)( (y+1)*x2 + (x+0) );
            *idx++ = (WORD)( (y+0)*x2 + (x+1) );
            *idx++ = (WORD)( (y+1)*x2 + (x+1) );
            *idx++ = (WORD)( (y+1)*x2 + (x+0) ); 
			nidx += 6;
        }
    }
    // Make top and bottom
	WORD wNorthVtx = nvtx;
	eradius = radius;
	nml = _V(0,1,0);
	pos = nml*eradius;
	vtx->x = D3DVAL(pos.x);  vtx->nx = D3DVAL(nml.x);
	vtx->y = D3DVAL(pos.y);  vtx->ny = D3DVAL(nml.y);
	vtx->z = D3DVAL(pos.z);  vtx->nz = D3DVAL(nml.z);
	vtx->tu0 = vtx->tu1 = 0.5f;
	vtx->tv0 = vtx->tv1 = 0.0f;
	vtx++;
    nvtx++;
	WORD wSouthVtx = nvtx;

	
	eradius = radius;
	nml = _V(0,-1,0);
	pos = nml*eradius;
	vtx->x = D3DVAL(pos.x);  vtx->nx = D3DVAL(nml.x);
	vtx->y = D3DVAL(pos.y);  vtx->ny = D3DVAL(nml.y);
	vtx->z = D3DVAL(pos.z);  vtx->nz = D3DVAL(nml.z);
	vtx->tu0 = vtx->tu1 = 0.5f;
	vtx->tv0 = vtx->tv1 = 1.0f;
	vtx++;
    nvtx++;

    for (x = 0; x < x1; x++) {
		WORD p1 = wSouthVtx;
		WORD p2 = (WORD)( (y)*x2 + (x+0) );
		WORD p3 = (WORD)( (y)*x2 + (x+1) );

        *idx++ = p1;
        *idx++ = p3;
        *idx++ = p2;
		nidx += 3;
    }

    for (x = 0; x < x1; x++) {
		WORD p1 = wNorthVtx;
		WORD p2 = (WORD)( (0)*x2 + (x+0) );
		WORD p3 = (WORD)( (0)*x2 + (x+1) );

        *idx++ = p1;
        *idx++ = p3;
        *idx++ = p2;
		nidx += 3;
    }

	// create the mesh
	VBMESH *mesh = new VBMESH(mgr);
	mesh->vtx = Vtx;
	mesh->nv  = nVtx;
	mesh->idx = Idx;
	mesh->nf  = nIdx/3;
	mesh->MapVertices (TileManager2Base::pDev); // TODO
	return mesh;
}

// =======================================================================
// =======================================================================

const oapi::D3D9Client *TileLoader::gc = 0;
volatile bool TileLoader::bRunThread = true;
int TileLoader::nqueue = 0;
int TileLoader::queue_in = 0;
int TileLoader::queue_out = 0;
HANDLE TileLoader::hLoadMutex = 0;
struct TileLoader::QUEUEDESC TileLoader::queue[MAXQUEUE2] = {0};

TileLoader::TileLoader (const oapi::D3D9Client *gclient)
{
	gc = gclient;
	bRunThread = true;
	nqueue = queue_in = queue_out = 0;
	load_frequency = Config->PlanetLoadFrequency;
	DWORD id;
	hLoadMutex = CreateMutex (0, FALSE, NULL);
	hLoadThread = CreateThread (NULL, 32768, Load_ThreadProc, this, 0, &id);
}

// -----------------------------------------------------------------------

TileLoader::~TileLoader ()
{
	bRunThread = false;
	if (WaitForSingleObject (hLoadThread, 1000) == WAIT_TIMEOUT) {
		TerminateThread (hLoadThread, 0);
	}
	CloseHandle (hLoadThread);
	CloseHandle (hLoadMutex);
}

// -----------------------------------------------------------------------

bool TileLoader::LoadTileAsync (Tile *tile)
{
	// Note: the queue should probably be sorted according to tile level

	int i, j;

	// Check if request is already present
	for (i = 0; i < nqueue; i++) {
		j = (i+queue_out) % MAXQUEUE2;
		if (queue[j].tile == tile)
			return false;
	}

	if (nqueue == MAXQUEUE2) { // queue full
		int maxlvl, maxlvl_idx = 0;
		for (i = 1, maxlvl = queue[0].tile->lvl; i < nqueue; i++)
			if (queue[i].tile->lvl > maxlvl)
				maxlvl = queue[i].tile->lvl, maxlvl_idx = i;
		if (maxlvl > tile->lvl) { // replace the highest level queued tile (load from lowest to highest)
			queue[maxlvl_idx].tile->state = Tile::Invalid;
			queue[maxlvl_idx].tile = tile;
			tile->state = Tile::InQueue;
		} else {
			tile->state = Tile::Invalid;
		}
		return false;
	}

	// add tile to load queue
	QUEUEDESC *qd = queue+queue_in;
	qd->tile = tile;
	tile->state = Tile::InQueue;
	nqueue++;
	queue_in = (queue_in+1) % MAXQUEUE2;
	return true;
}

// -----------------------------------------------------------------------

void TileLoader::Unqueue (TileManager2Base *mgr)
{
	bool locked = false; // whether a mutex semaphore was taken

	bool found;
	do {
		int i, j;
		found = false;
		for (i = 0; i < nqueue; ++i) {
			j = (queue_out+i) % MAXQUEUE2;
			if (queue[j].tile->mgr == mgr) {
				if (!locked) {
					WaitForMutex();
					locked = true;
				}
				if (i) {
					queue[j].tile = queue[queue_out].tile;
				}
				queue_out = (queue_out+1) % MAXQUEUE2;
				nqueue--;
				found = true;
				break;
			}
		}
	} while (found);

	if (locked) {
		ReleaseMutex(); // ...if mutex was taken
	}
}

// -----------------------------------------------------------------------

bool TileLoader::Unqueue (Tile *tile)
{
	if (tile->state != Tile::InQueue) return false;

	int i, j;
	for (i = 0; i < nqueue; i++) {
		j = (queue_out+i) % MAXQUEUE2;
		if (queue[j].tile == tile) {
			WaitForMutex ();
			if (i) {
				queue[j].tile = queue[queue_out].tile;
			}
			queue_out = (queue_out+1) % MAXQUEUE2;
			nqueue--;
			ReleaseMutex ();
			return true;
		}
	}
	return false;
}

// -----------------------------------------------------------------------

DWORD WINAPI TileLoader::Load_ThreadProc (void *data)
{
	TileLoader *loader = (TileLoader*)data;
	DWORD idle = 1000/loader->load_frequency;
	Tile *tile;
	bool load;

	while (bRunThread) {
		Sleep (idle);
		WaitForMutex ();
		if (load = (nqueue > 0)) {
			tile = queue[queue_out].tile;
			_ASSERT(TILE_STATE_OK(tile));
			if (tile->state == Tile::InQueue) {
				tile->state = Tile::Loading; // lock tile and its ancestor tree
			} else {
				load = false;
			}
			queue_out = (queue_out+1) % MAXQUEUE2; // remove from queue
			nqueue--;
		}
//		ReleaseMutex ();

		if (load) {
			tile->Load(); // load/create the tile

//			WaitForMutex ();
			tile->state = Tile::Inactive; // unlock tile
//			ReleaseMutex ();
		}
		ReleaseMutex ();
	}
	return 0;
}

// =======================================================================
// =======================================================================

class oapi::D3D9Client *TileManager2Base::gc = NULL; 
LPDIRECT3DDEVICE9 TileManager2Base::pDev = NULL;
TileManager2Base::configPrm TileManager2Base::cprm = {
	1,                  // elevInterpol
	false,				// bSpecular
	false,				// bLights
	false,              // bCloudShadow
	0.5                 // lightfac
};
TileLoader *TileManager2Base::loader = NULL;
double TileManager2Base::resolutionBias = 4.0;
bool TileManager2Base::bTileLoadThread = false;


// -----------------------------------------------------------------------

TileManager2Base::TileManager2Base (const vPlanet *vplanet, int _maxres)
: vp(vplanet)
{
	// set persistent parameters
	prm.maxlvl = max (1, _maxres-4);

	obj = vp->Object();
	obj_size = oapiGetSize (obj);
	oapiGetObjectName (obj, cbody_name, 256);

	for (int i=0;i<NPOOLS;i++) VtxPoolSize[i]=IdxPoolSize[i]=0;
}

// -----------------------------------------------------------------------

TileManager2Base::~TileManager2Base ()
{
	if (loader) {
		loader->Unqueue(this);
	}

	LogAlw("Deleting TileManagerBase 0x%X ...",this);

	for (int i=0;i<NPOOLS;i++) {
		while (!VtxPool[i].empty()) {
			VtxPool[i].top()->Release();
			VtxPool[i].pop();
		}
		while (!IdxPool[i].empty()) {
			IdxPool[i].top()->Release();
			IdxPool[i].pop();
		}
	}
}

// -----------------------------------------------------------------------

void TileManager2Base::GlobalInit (class oapi::D3D9Client *gclient)
{
	LogAlw("Starting to initialize Surface.fx a shading technique...");

	gc = gclient;
	pDev = gc->GetDevice();

	resolutionBias = 4.0 + *(double*)gclient->GetConfigParam (CFGPRM_RESOLUTIONBIAS);
	cprm.bSpecular = *(bool*)gclient->GetConfigParam (CFGPRM_SURFACEREFLECT);
	cprm.bLights = *(bool*)gclient->GetConfigParam (CFGPRM_SURFACELIGHTS);
	cprm.bCloudShadow = *(bool*)gclient->GetConfigParam (CFGPRM_CLOUDSHADOWS);
	cprm.lightfac = *(double*)gclient->GetConfigParam (CFGPRM_SURFACELIGHTBRT);
	cprm.elevMode = *(int*)gclient->GetConfigParam (CFGPRM_ELEVATIONMODE);
	bTileLoadThread = true; // TODO: g_pOrbiter->Cfg()->CfgPRenderPrm.bLoadOnThread;

	loader = new TileLoader (gc);
}

// -----------------------------------------------------------------------

void TileManager2Base::GlobalExit ()
{
	delete loader;
}

// -----------------------------------------------------------------------

void TileManager2Base::SetRenderPrm (MATRIX4 &dwmat, double prerot, bool use_zbuf, const vPlanet::RenderPrm &rprm)
{
	const double minalt = 0.002;
	VECTOR3 obj_pos, cam_pos;

	prm.rprm = &rprm;

	// set up the parameter structure
	oapiGetRotationMatrix(obj, &prm.grot); // planet's rotation matrix
	oapiGetGlobalPos (obj, &obj_pos);      // planet position
	oapiCameraGlobalPos (&cam_pos);        // camera position

	if (prerot) {
		double srot = sin(prerot), crot = cos(prerot);
		MATRIX4 RM4 = { crot,0,-srot,0,  0,1,0,0,  srot,0,crot,0,  0,0,0,1 };
		prm.dwmat = mul(RM4,dwmat);
		prm.grot = mul (prm.grot, _M(crot,0,srot, 0,1,0, -srot,0,crot));
	} else {
		prm.dwmat = dwmat;
	}
	prm.dwmat_tmp = prm.dwmat;
	prm.cpos = obj_pos-cam_pos;
	prm.cdir = tmul (prm.grot, -prm.cpos); // camera's direction in planet frame
	double cdist = length(prm.cdir);
	prm.cdist = cdist / obj_size;          // camera's distance in units of planet radius
	normalise (prm.cdir);
	prm.sdir = tmul (prm.grot, -obj_pos);  // sun's direction in planet frame
	normalise (prm.sdir);
	prm.viewap = acos (1.0/(max (prm.cdist, 1.0+minalt)));
	prm.scale = 1.0;
	prm.fog = rprm.bFog;
	prm.tint = rprm.bTint;
	//if (prm.tint)
	//	prm.atm_tint = rprm.rgbTint;
}

// -----------------------------------------------------------------------

MATRIX4 TileManager2Base::WorldMatrix (int ilng, int nlng, int ilat, int nlat)
{
	if (nlat < 2) {  // render full sphere
		return prm.dwmat;
	}

	double lat, lng = PI2 * (double)ilng/(double)nlng + PI; // add pi so texture wraps at +-180�
	double slng = sin(lng), clng = cos(lng);
	MATRIX4 lrot = {clng,0,slng,0,  0,1.0,0,0,  -slng,0,clng,0,  0,0,0,1.0};

	if (nlat <= 8) {  // rotate patch into place
		return mul(lrot,prm.dwmat);
	} else {          // shift, scale and rotate
		bool north = (ilat < nlat/2);
		if (north) lat = PI * (double)(nlat/2-ilat-1)/(double)nlat;
		else       lat = PI * (double)(nlat/2-ilat)/(double)nlat;
		double s = obj_size;
		double dx = s*cos(lng)*cos(lat); // the offsets between sphere centre and tile corner
		double dy = s*sin(lat);
		double dz = s*sin(lng)*cos(lat);
		prm.dwmat_tmp.m41 = (dx*prm.grot.m11 + dy*prm.grot.m12 + dz*prm.grot.m13 + prm.cpos.x) * (float)prm.scale;
		prm.dwmat_tmp.m42 = (dx*prm.grot.m21 + dy*prm.grot.m22 + dz*prm.grot.m23 + prm.cpos.y) * (float)prm.scale;
		prm.dwmat_tmp.m43 = (dx*prm.grot.m31 + dy*prm.grot.m32 + dz*prm.grot.m33 + prm.cpos.z) * (float)prm.scale;
		return mul(lrot,prm.dwmat_tmp);
	}
}



DWORD TileManager2Base::RecycleVertexBuffer(DWORD nv, LPDIRECT3DVERTEXBUFFER9 *pVB)
{
	int pool = -1;
	D3DVERTEXBUFFER_DESC desc;

	if (*pVB) {
		(*pVB)->GetDesc(&desc);
		desc.Size /= sizeof(VERTEX_2TEX);
		for (int i=0;i<NPOOLS;i++) if (VtxPoolSize[i]==desc.Size) { pool = i; break; } 
		if (pool>=0) {
			VtxPool[pool].push(*pVB);
			*pVB = NULL;
			if (nv==0) return 0; // Store buffer, do not allocate new one.
		}
		else LogErr("Pool Doesn't exists");	
	}

	
	pool = -1;
	
	// Find a pool 
	for (int i=0;i<NPOOLS;i++) if (VtxPoolSize[i]==nv) { pool = i; break; }
	
	// Create a new pool size
	if (pool==-1) {
		for (int i=0;i<NPOOLS;i++) if (VtxPoolSize[i]==0) { VtxPoolSize[i] = nv; pool = i; break; }	
		if (pool<0) { 
			LogErr("Failed to Crerate a Pool (size=%u)", nv);
			*pVB = NULL; 
			return 0; 
		}
	}

	if (VtxPool[pool].empty()) {
		HR(pDev->CreateVertexBuffer(nv*sizeof(VERTEX_2TEX), D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, pVB, NULL));
		return VtxPoolSize[pool];
	}
	else {
		*pVB = VtxPool[pool].top();
		VtxPool[pool].pop();
		return VtxPoolSize[pool];
	}
}



DWORD TileManager2Base::RecycleIndexBuffer(DWORD nf, LPDIRECT3DINDEXBUFFER9 *pIB)
{
	int pool = -1;
	D3DINDEXBUFFER_DESC desc;

	if (*pIB) {
		(*pIB)->GetDesc(&desc);
		desc.Size /= (sizeof(WORD)*3);
		for (int i=0;i<NPOOLS;i++) if (IdxPoolSize[i]==desc.Size) { pool = i; break; } 
		if (pool>=0) {
			IdxPool[pool].push(*pIB);
			*pIB = NULL;
			if (nf==0) return 0; // Store buffer, do not allocate new one.
		}
		else LogErr("Pool Doesn't exists");	
	}

	
	pool = -1;
	
	// Find a pool 
	for (int i=0;i<NPOOLS;i++) if (IdxPoolSize[i]==nf) { pool = i; break; }
	
	// Create a new pool size
	if (pool==-1) {
		for (int i=0;i<NPOOLS;i++) if (IdxPoolSize[i]==0) { IdxPoolSize[i] = nf; pool = i; break; }	
		if (pool<0) { 
			LogErr("Failed to Crerate a Pool (size=%u)", nf);
			*pIB = NULL; 
			return 0; 
		}
	}

	if (IdxPool[pool].empty()) {
		HR(pDev->CreateIndexBuffer(nf*sizeof(WORD)*3, D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, pIB, NULL));
		return IdxPoolSize[pool];
	}
	else {
		*pIB = IdxPool[pool].top();
		IdxPool[pool].pop();
		return IdxPoolSize[pool];
	}
}
