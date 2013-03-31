// =================================================================================================================================
// The MIT Lisence:
//
// Copyright (C) 2012 Jarmo Nikkanen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation 
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software 
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// =================================================================================================================================

#ifndef __D3D9EFFECT_H
#define __D3D9EFFECT_H

#include "D3D9Client.h"
#include <d3d9.h> 
#include <d3dx9.h>

using namespace oapi;

class D3D9Effect {

	DWORD d3d9id;

public:
	static void D3D9TechInit(D3D9Client *gc, LPDIRECT3DDEVICE9 pDev, const char *folder);
	static void GlobalExit();
	static void ShutDown();

	D3D9Effect();
	~D3D9Effect();

	static void UpdateEffectCamera(OBJHANDLE hPlanet);
	static void InitLegacyAtmosphere(OBJHANDLE hPlanet, float GlobalAmbient);
	static void SetViewProjMatrix(LPD3DXMATRIX pVP);

	static void RenderBoundingBox(const LPD3DXMATRIX pW, const LPD3DXMATRIX pGT, const D3DXVECTOR4 *bmin, const D3DXVECTOR4 *bmax, const LPD3DXVECTOR4 color);
	static void RenderBoundingSphere(const LPD3DXMATRIX pW, const LPD3DXMATRIX pGT, const D3DXVECTOR4 *bs, const LPD3DXVECTOR4 color);
	static void RenderBillboard(const LPD3DXMATRIX pW, LPD3D9CLIENTSURFACE pTex);
	static void RenderExhaust(const LPD3DXMATRIX pW, VECTOR3 &cdir, EXHAUSTSPEC *es, LPD3D9CLIENTSURFACE def);
	static void RenderSpot(float intens, const LPD3DXCOLOR color, const LPD3DXMATRIX pW, LPD3D9CLIENTSURFACE pTex);
	static void RenderArrow(OBJHANDLE hObj, const VECTOR3 *ofs, float size, const D3DXCOLOR *pColor);
	static void Render2DPanel(const MESHGROUP *mg, const LPD3D9CLIENTSURFACE pTex, const LPD3DXMATRIX pW, float alpha, float scale);
	static void RenderReEntry(const LPD3D9CLIENTSURFACE pTex, const LPD3DXVECTOR3 vPosA, const LPD3DXVECTOR3 vPosB, const LPD3DXVECTOR3 vDir, float alpha_a, float alpha_b, float size);
	static void RenderArrow(OBJHANDLE hObj, const VECTOR3 *ofs, const VECTOR3 *dir, const VECTOR3 *rot, float size, const D3DXCOLOR *pColor);  
	static void RenderAxisVector(LPD3DXMATRIX pW, const LPD3DXCOLOR pColor, float len);
	static bool ComputeLighting(LPDIRECT3DCUBETEXTURE9 pEnv, LPDIRECT3DCUBETEXTURE9 pLight, LPDIRECT3DCUBETEXTURE9 pTgt);
	
	static LPDIRECT3DDEVICE9 pDev;
	static LPDIRECT3DVERTEXBUFFER9 pVB;
	static LPDIRECT3DTEXTURE9 pNoise;
	static SURFHANDLE hNoise;
	
	// Rendering Technique related parameters
	static ID3DXEffect	*FX;
	static D3D9Client   *gc;
	static D3D9Mesh     *hArrow;

	static D3D9MatExt	defmat;
	static D3D9MatExt	night_mat;
	static D3D9MatExt	emissive_mat;
	
	// Techniques ----------------------------------------------------
	static D3DXHANDLE	eVesselTech;
	static D3DXHANDLE	eBuildingTech;
	static D3DXHANDLE	eSimple;
	static D3DXHANDLE	eBBTech;
	static D3DXHANDLE	eBSTech;
	static D3DXHANDLE   eExhaust;
	static D3DXHANDLE   eSpotTech;
	static D3DXHANDLE   ePanelTech;
	static D3DXHANDLE   ePanelTechB;
	static D3DXHANDLE	eBaseTile;
	static D3DXHANDLE	eRingTech;
	static D3DXHANDLE	eRingTech2;
	static D3DXHANDLE	eShadowTech;
	static D3DXHANDLE	eBaseShadowTech;
	static D3DXHANDLE	eBeaconArrayTech;
	static D3DXHANDLE	eArrowTech;
	static D3DXHANDLE	eAxisTech;
	static D3DXHANDLE	eLightCubeTech;

	static D3DXHANDLE	eVCHudTech;
	static D3DXHANDLE	eVCMFDTech;
	static D3DXHANDLE	eVCTech;

	static D3DXHANDLE	ePlanetTile;
	static D3DXHANDLE	eCloudTech;
	static D3DXHANDLE	eCloudShadow;
	static D3DXHANDLE	eSkyDomeTech;

	static D3DXHANDLE	eDiffuseTech;
	static D3DXHANDLE	eEmissiveTech;
	static D3DXHANDLE	eHazeTech;

	// Transformation Matrices ----------------------------------------
	static D3DXHANDLE	eVP;	// Combined View & Projection Matrix
	static D3DXHANDLE	eW;		// World Matrix
	static D3DXHANDLE	eWI;	// World inverse Matrix
	static D3DXHANDLE	eGT;	// MeshGroup transformation matrix
	static D3DXHANDLE	eGTI;

	// Lighting related parameters ------------------------------------
	static D3DXHANDLE   eMtrl;
	static D3DXHANDLE	eMat;
	static D3DXHANDLE	eWater;
	static D3DXHANDLE	eSun;
	static D3DXHANDLE	eLights;
	static D3DXHANDLE	eLightCount;

	// Auxilliary params ----------------------------------------------
	static D3DXHANDLE   eModAlpha;
	static D3DXHANDLE	eFullyLit;
	static D3DXHANDLE	eBrighten;
	static D3DXHANDLE	eUseSpec;
	static D3DXHANDLE	eUseEmis;
	static D3DXHANDLE	eUseRefl;
	static D3DXHANDLE	eDebugHL;
	static D3DXHANDLE	eEnvMapEnable;
	static D3DXHANDLE	eUseDisl;
	static D3DXHANDLE	eMix;
	static D3DXHANDLE   eColor;
	static D3DXHANDLE   eFogColor;
	static D3DXHANDLE   eTexOff;
	static D3DXHANDLE	eSpecularMode;
	static D3DXHANDLE	eHazeMode;
	static D3DXHANDLE	eNormalMap;
	static D3DXHANDLE	eNormalType;
	static D3DXHANDLE	eTextured;
	static D3DXHANDLE	eClamp;
	static D3DXHANDLE	eNight;
	static D3DXHANDLE   eTime;
	static D3DXHANDLE	eExposure;
	static D3DXHANDLE	eCameraPos;	
	static D3DXHANDLE	eReflCtrl;
	static D3DXHANDLE   eDistScale;
	static D3DXHANDLE   eRadius;
	static D3DXHANDLE	eFogDensity;
	static D3DXHANDLE	ePointScale;
	static D3DXHANDLE	eAtmColor;
	static D3DXHANDLE	eCamOff;
	static D3DXHANDLE	eProxySize;
	static D3DXHANDLE	eMtrlAlpha;

	static D3DXHANDLE	eAttennuate;
	static D3DXHANDLE	eInScatter;

	// Textures --------------------------------------------------------
	static D3DXHANDLE	eTex0;
	static D3DXHANDLE	eTex1;
	static D3DXHANDLE	eTex3;
	static D3DXHANDLE	eSpecMap;
	static D3DXHANDLE	eEmisMap;
	static D3DXHANDLE	eEnvMap;
	static D3DXHANDLE	eDislMap;
	static D3DXHANDLE	eReflMap;
	static D3DXHANDLE	eEnvLight;

	// Legacy Atmosphere -----------------------------------------------

	static D3DXHANDLE	eGlobalAmb;	 
	static D3DXHANDLE	eSunAppRad;	 
	static D3DXHANDLE	eAmbient0;	 
	static D3DXHANDLE	eDispersion;	  
};

#endif // !__D3D9EFFECT_H