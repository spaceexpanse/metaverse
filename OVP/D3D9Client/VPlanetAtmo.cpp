// ==============================================================
// VPlanet.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under LGPL v3
// Copyright (C) 2006-2022 Martin Schweiger
//				 2010-2022 Jarmo Nikkanen
// ==============================================================

#define D3D_OVERLOADS

#include <map>
#include <sstream>
#include <unordered_map>

#include "D3D9Client.h"
#include "D3D9Config.h"
#include "VPlanet.h"
#include "AtmoControls.h"
#include "VectorHelpers.h"
#include "IProcess.h"

using namespace oapi;

const float invalid_val = -1e12;

// ===========================================================================================
//

PlanetShader::PlanetShader(LPDIRECT3DDEVICE9 pDev, const char* file, const char* vs, const char* ps, const char* name, const char* options)
	: ShaderClass(pDev, file, vs, ps, name, options)
{
	bLocals = false;
	bMicrotex = false;
	bShdMap = false;
	bDevtools = false;
	bAtmosphere = true;
	bWater = false;
	bRipples = false;
	bCloudShd = false;
	bNightlights = false;

	if (string(options).find("_LOCALLIGHTS") != string::npos) bLocals = true;
	if (string(options).find("_MICROTEX") != string::npos) bMicrotex = true;
	if (string(options).find("_SHDMAP") != string::npos) bShdMap = true;
	if (string(options).find("_DEVTOOLS") != string::npos) bDevtools = true;
	if (string(options).find("_NO_ATMOSPHERE") != string::npos) bAtmosphere = false;
	if (string(options).find("_WATER") != string::npos) bWater = true;
	if (string(options).find("_RIPPLES") != string::npos) bRipples = true;
	if (string(options).find("_CLOUDSHD") != string::npos) bCloudShd = true;
	if (string(options).find("_NIGHTLIGHTS") != string::npos) bNightlights = true;

	tCloud = GetPSHandle("tCloud");
	tCloud2 = GetPSHandle("tCloud2");
	tMask = GetPSHandle("tMask");
	tDiff = GetPSHandle("tDiff");
	tShadowMap = GetPSHandle("tShadowMap");
	PrmVS = GetVSHandle("Prm");
	Prm = GetPSHandle("Prm");
	FlowVS = GetVSHandle("FlowVS");
	Flow = GetPSHandle("Flow");
	Lights = GetPSHandle("Lights");
	Spotlight = GetPSHandle("Spotlight");
};

PlanetShader::~PlanetShader()
{

}






PlanetShader* vPlanet::pRender[8] = {};


// ===========================================================================================
//
void vPlanet::GlobalInitAtmosphere(oapi::D3D9Client* gc)
{
	pDev = gc->GetDevice();

	pIP = new ImageProcessing(pDev, "Modules/D3D9Client/Scatter.hlsl", "SunColor");

	pIP->CompileShader("SkyView");
	pIP->CompileShader("LandView");
	pIP->CompileShader("RingView");
	pIP->CompileShader("RenderSun");
	pIP->CompileShader("AmbientSky");
	pIP->CompileShader("LandViewAtten");

	if (!pIP->IsOK()) {
		oapiWriteLog("InitializeScatteringEx() FAILED");
		return;
	}

	// Render Sunglare texture -----------------------------
	//
	D3DXCreateTexture(pDev, 256, 256, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &pSunTex);

	LPDIRECT3DSURFACE9 pTgt;

	pIP->Activate("RenderSun");
	pSunTex->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(false)) LogErr("pIP Execute Failed (RenderSun)");
	SAFE_RELEASE(pTgt);


	// Get Texture size constants form shader file
	//
	Wc = pIP->FindDefine("Wc");
	Nc = pIP->FindDefine("Nc");
	Qc = pIP->FindDefine("Qc");

	bool bRiples = *(bool*)gc->GetConfigParam(CFGPRM_SURFACERIPPLE);
	bool bShadows = *(bool*)gc->GetConfigParam(CFGPRM_CLOUDSHADOWS);
	bool bClouds = *(bool*)gc->GetConfigParam(CFGPRM_CLOUDS);
	bool bNightLights = *(bool*)gc->GetConfigParam(CFGPRM_SURFACELIGHTS);
	bool bWater = *(bool*)gc->GetConfigParam(CFGPRM_SURFACEREFLECT);
	bool bLocals = *(bool*)gc->GetConfigParam(CFGPRM_LOCALLIGHT);
	bool bAtmosphere = *(bool*)gc->GetConfigParam(CFGPRM_ATMHAZE);

	// ---------------------------------------------------
	// Compile planet shader with different configurations
	// ---------------------------------------------------

	string blend = "";
	string flags = "";
	pRender[PLT_GIANT] = new PlanetShader(pDev, "Modules/D3D9Client/NewPlanet.hlsl", "GiantVS", "GiantPS", "Giant", flags.c_str());
	pRender[PLT_G_CLOUDS] = new PlanetShader(pDev, "Modules/D3D9Client/NewPlanet.hlsl", "CloudVS", "GiantCloudPS", "GiantCloud", flags.c_str());

	if (Config->CloudMicro) flags += "_CLOUDMICRO ";
	if (Config->bCloudNormals) flags += "_CLOUDNORMALS ";

	pRender[PLT_CLOUDS] = new PlanetShader(pDev, "Modules/D3D9Client/NewPlanet.hlsl", "CloudVS", "CloudPS", "Clouds", flags.c_str());



	flags = "";
	if (Config->BlendMode == 0) blend = "_SOFT ";
	if (Config->BlendMode == 1) blend = "_MED ";
	if (Config->BlendMode == 2) blend = "_HARD ";

	if (bLocals) flags += "_LOCALLIGHTS ";
	if (Config->MicroMode) flags += "_MICROTEX ";
	if (Config->ShadowMapMode) flags += "_SHDMAP ";
	if (Config->EnableMeshDbg) flags += "_DEVTOOLS ";

	pRender[PLT_MARS] = new PlanetShader(pDev, "Modules/D3D9Client/NewPlanet.hlsl", "TerrainVS", "TerrainPS", "Mars", (flags + blend).c_str());


	flags += "_NO_ATMOSPHERE ";

	pRender[PLT_MOON] = new PlanetShader(pDev, "Modules/D3D9Client/NewPlanet.hlsl", "TerrainVS", "TerrainPS", "Moon", (flags + blend).c_str());


	flags = "";
	blend = "";
	if (bWater) flags += "_WATER ";
	if (bWater && bRiples) flags += "_RIPPLES ";
	if (bShadows) flags += "_CLOUDSHD ";
	if (bNightLights) flags += "_NIGHTLIGHTS ";
	if (bLocals) flags += "_LOCALLIGHTS ";

	if (Config->ShadowMapMode) flags += "_SHDMAP ";
	if (Config->EnableMeshDbg) flags += "_DEVTOOLS ";

	pRender[PLT_EARTH] = new PlanetShader(pDev, "Modules/D3D9Client/NewPlanet.hlsl", "TerrainVS", "TerrainPS", "Earth", (flags + blend).c_str());
}


// ===========================================================================================
//
FVECTOR2 vPlanet::Gauss7(float alt, float cos_dir, float R0, float R1, FVECTOR2 iH0)
{
	float dist = RayLength(cos_dir, R0 + alt, R1);
	return Gauss7(cos_dir, R0 + alt, dist, iH0);
}


// ===========================================================================================
//
FVECTOR2 vPlanet::Gauss7(float cos_dir, float r0, float dist, FVECTOR2 ih0)
{
	static const float n[] = { -0.949107, -0.741531, -0.405845, 0.0, 0.405845,0.741531, 0.949107 };
	static const float w[] = { 0.129485, 0.279705, 0.381830, 0.417959,  0.381830, 0.279705, 0.129485 };

	float x = 2.0 * r0 * cos_dir;
	float r2 = r0 * r0;
	float a[7];

	// Compute altitudes of sample points
	for (int i = 0; i < 7; i++) {
		float d0 = dist * (n[i] + 1.0f) * 0.5f;
		a[i] = sqrt(r2 + d0 * d0 - d0 * x) - cp.PlanetRad;
	}

	float ray = 0.0f; float mie = 0.0f;
	for (int i = 0; i < 7; i++) {
		ray += exp(-clamp(a[i] * ih0.x, -20.0f, 20.0f)) * w[i];
		mie += exp(-clamp(a[i] * ih0.y, -20.0f, 20.0f)) * w[i];
	}
	return FVECTOR2(ray, mie) * 0.5f * dist;
}


// ===========================================================================================
//
FVECTOR2 vPlanet::Gauss4(float cos_dir, float r0, float dist, FVECTOR2 ih0)
{	
	static const float n[] = { 0.06943f, 0.33001f, 0.66999f, 0.93057f };
	static const float w[] = { 0.34786f, 0.65215f, 0.65215f, 0.34786f };

	float x = 2.0 * r0 * cos_dir;
	float r2 = r0 * r0;
	float a[4];

	// Compute altitudes of sample points
	for (int i = 0; i < 4; i++) {
		float d0 = dist * n[i];
		a[i] = sqrt(r2 + d0 * d0 - d0 * x) - cp.PlanetRad;
	}

	float ray = 0.0f; float mie = 0.0f;
	for (int i = 0; i < 4; i++) {
		ray += exp(-clamp(a[i] * ih0.x, -20.0f, 20.0f)) * w[i];
		mie += exp(-clamp(a[i] * ih0.y, -20.0f, 20.0f)) * w[i];
	}
	return FVECTOR2(ray, mie) * 0.5f * dist;
}


// ===========================================================================================
//
float vPlanet::SunAltitude()
{
	float d = dot(cp.toCam, cp.toSun);
	float q = cp.CamRad * d;

	// Ray's closest approach to planet
	float alt = sqrt(cp.CamRad2 - q * q) - cp.PlanetRad;
	if (d < 0) return alt;
	return cp.AtmoAlt;
}


// ===========================================================================================
//
float vPlanet::RayLength(float cos_dir, float r0, float r1)
{
	float y = r0 * cos_dir;
	float z2 = r0 * r0 - y * y;
	return sqrt(r1 * r1 - z2) + y;
}


// ===========================================================================================
//
float vPlanet::RayLength(float cos_dir, float r0)
{
	return RayLength(cos_dir, r0, cp.AtmoRad);
}


// ===========================================================================================
// Rayleigh phase function
//
float vPlanet::RayPhase(float cw)
{
	return 0.25f * (4.0f + cw * cw) / (1.0f + cp.RayPh * cw);
}


// ===========================================================================================
// Henyey-Greenstein Phase function
//
float vPlanet::MiePhase(float cw)
{
	float cw2 = cw * cw;
	return cp.HG.x * (1.0f + cw2) * pow(abs(cp.HG.y - cp.HG.z * cw2 * cw), -1.5f) + cp.HG.w;
}


// ===========================================================================================
// Compute optical transparency of atmosphere. color in .rgb and (ray+mie) optical depth in .a
//
FVECTOR4 vPlanet::ComputeCameraView(float angle, float rad, float distance)
{
	FVECTOR2 od = Gauss7(angle, rad, distance, cp.iH); // Ray/Mie Optical depth
	FVECTOR2 rm = od * cp.rmO;
	FVECTOR3 clr = cp.RayWave * rm.x + cp.MieWave * rm.y;
	return FVECTOR4(exp(-clr), od.x + od.y);
}


// ===========================================================================================
//
FVECTOR4 vPlanet::ComputeCameraView(FVECTOR3 vPos, FVECTOR3 vNrm, FVECTOR3 vRay, float r, float t_factor)
{
	float d = 0.0f;
	float a = dot(vNrm, vRay);
	if (!CameraInAtmosphere()) d = RayLength(a, r);
	else d = abs(dot(vPos - cp.CamPos, vRay));
	return ComputeCameraView(a, r, d);
}


// ===========================================================================================
//
FVECTOR4 vPlanet::ComputeCameraView(FVECTOR3 vPos)
{
	FVECTOR3 vRP = vPos - cp.CamPos;
	float d = length(vRP);
	float r = length(vPos);
	FVECTOR3 vRay = vRP / d;
	float a = dot(vPos / r, vRay);
	if (!CameraInAtmosphere()) d = RayLength(a, r); // Recompute distance to atm exit
	return ComputeCameraView(a, r, d);
}


// ===========================================================================================
// Amount of light inscattering along the ray
//
void vPlanet::IntegrateSegment(FVECTOR3 vOrig, float len, FVECTOR4* ral, FVECTOR4* mie)
{
	static const int NSEG = 6;
	static const float iNSEG = 1.0f / float(NSEG);

	if (ral) *ral = 0.0f;
	if (mie) *mie = 0.0f;

	FVECTOR3 vRay = normalize(cp.CamPos - vOrig); // From vOrig to Cam

	for (int i = 0; i < NSEG; i++)
	{
		float dst = len * (iNSEG * (float(i) + 0.5f));
		FVECTOR3 pos = vOrig + vRay * dst;
		FVECTOR3 n = normalize(pos);
		float rad = dot(n, pos);
		float alt = rad - cp.PlanetRad;
		float ang = dot(n, vRay);

		FVECTOR3 x = SunLightColor(-dot(n, cp.toSun), alt) * ComputeCameraView(ang, rad, len - dst).rgb;

		if (ral) {
			float f = exp(-alt * cp.iH.x) * iNSEG;
			ral->rgb += x * f; ral->a += f;
		}
		if (mie) {
			float f = exp(-alt * cp.iH.y) * iNSEG;
			mie->rgb += x * f; mie->a += f;
		}
	}

	if (ral) ral->rgb *= cp.RayWave * cp.cSun * cp.rmI.x * len;	// Multiply with wavelength and inscatter factors
	if (mie) mie->rgb *= cp.MieWave * cp.cSun * cp.rmI.y * len;
}

// ===========================================================================================
// Sunlight color for pixel in atmosphere
//
FVECTOR3 vPlanet::SunLightColor(float angle, float alt)
{
	float r = alt + cp.PlanetRad;
	float d = RayLength(angle, r);
	FVECTOR2 rm = Gauss7(angle, r, d, cp.iH) * cp.rmO;
	FVECTOR3 clr = cp.RayWave * rm.x + cp.MieWave * rm.y;
	return exp(-clr);
}

// ===========================================================================================
//
FVECTOR3 vPlanet::SunLightColor(FVECTOR3 geo_pos_ecl)
{
	FVECTOR2 rm = 0.0f;
	float r2 = dot(geo_pos_ecl, geo_pos_ecl);
	float r = sqrt(r2);
	float a = r - cp.PlanetRad;
	float d = dot(geo_pos_ecl, cp.toSun) / r;

	if (a > cp.AtmoRad && d > 0) return FVECTOR3(1, 1, 1); // Ray doesn't intersect atmosphere

	float q = r * d;
	float alt = sqrt(r2 - q * q) - cp.PlanetRad; // Ray's closest approach to a planet's center

	if (alt < 0.0f && d < 0) return FVECTOR3(0, 0, 0); // Ray is shadowed by a planet
	if (alt > cp.AtmoRad && a > cp.AtmoRad) return FVECTOR3(1, 1, 1); // Ray doesn't intersect atmosphere

	if (r > cp.AtmoRad) // Ray passes through atmosphere from space to space
	{
		rm = Gauss7(alt, 0.0f, cp.PlanetRad, cp.AtmoRad, cp.iH) * 2.0f;
	}
	else // Sample point 'pos' lies with-in atmosphere
	{
		rm = Gauss7(a, d, cp.PlanetRad, cp.AtmoRad, cp.iH);
	}

	rm *= cp.rmO;
	return exp(-(cp.RayWave * rm.x + cp.MieWave * rm.y));
}


// ===========================================================================================
//
ObjAtmParams vPlanet::GetObjectAtmoParams(VECTOR3 obj_gpos)
{
	ObjAtmParams op;
	
	FVECTOR3 vPos = (obj_gpos - gpos);
	FVECTOR3 vRP = vPos - cp.CamPos;	// Camera relative position
	float d = length(vRP);				// Distance to camera
	float r = length(vPos);				// Radius
	FVECTOR3 vRay = vRP / d;			// Unit viewing ray from cameta to obj_gpos
	float a = dot(vPos / r, vRay);		// cosine of (Normal/vRay) angle 
	float s = dot(vRay, cp.toSun);

	if (!CameraInAtmosphere()) d = RayLength(a, r); // Recompute distance to atm exit

	// Incatter Color
	FVECTOR4 rl, mi;
	FVECTOR3 vOrig = vPos - vRay * d;
	IntegrateSegment(vOrig, d, &rl, &mi);	// Rayleigh and Mie inscatter

	
	// Color of Sunlight
	FVECTOR2 rm = Gauss7(a, r, d, cp.iH) * cp.rmO;
	FVECTOR3 clr = exp(-(cp.RayWave * rm.x + cp.MieWave * rm.y));
	
	op.Sun = clr;
	op.Transmission = ComputeCameraView(a, r, d).rgb;
	op.Incatter = (rl.rgb * RayPhase(-s) + mi.rgb * MiePhase(-s));

	return op;
}


// ===========================================================================================
//
vPlanet::SHDPrm vPlanet::ComputeShadow(FVECTOR3 vRay)
{
	// Compute Planet's shadow entry and exit points
	vPlanet::SHDPrm sp;

	// Camera radius in "shadow" frame.
	double A = dot(TestPrm.Up, TestPrm.toCam * TestPrm.CamRad);
	sp.cr = abs(A);

	// Projection of viewing ray on 'shadow' axes
	double u = dot(vRay, TestPrm.Up);
	double t = dot(vRay, TestPrm.ZeroAz);
	double z = dot(vRay, TestPrm.toSun);

	// Cosine 'a'
	double a = u / sqrt(u * u + t * t);

	double k2 = A * A * a * a;
	double h2 = A * A - k2;
	double w2 = cp.PlanetRad2 - h2;
	double w = sqrt(w2);
	double k = sqrt(k2) * sign(a);
	double v2 = 0;
	double m = TestPrm.CamRad2 - cp.PlanetRad2;

	sp.w2 = w2;
	sp.se = k - w;
	sp.sx = sp.se + 2.0f * w;
	
	// Project distances 'es' and 'xs' back to 3D space
	double f = 1.0f / sqrt(max(2.5e-5, 1.0 - z * z));
	sp.se *= f;
	sp.sx *= f;
	
	// Compute atmosphere entry and exit points 
	//
	a = -dot(TestPrm.toCam, vRay);
	k2 = TestPrm.CamRad2 * a * a;
	h2 = TestPrm.CamRad2 - k2;
	v2 = cp.AtmoRad2 - h2;
	w = sqrt(v2);
	k = sqrt(k2) * sign(a);

	
	sp.hd = m > 0.0 ? sqrt(m) : 0.0;
	sp.ae = (k - w);
	sp.ax = sp.ae + 2.0f * w;
	sp.ca = TestPrm.CamRad * a;

	// If the ray doesn't intersect atmosphere then set both distances to zero
	if (v2 < 0) sp.ae = sp.ax = invalid_val;

	// If the ray doesn't intersect shadow then set both distances to atmo exit
	if (w2 < 0) {
		sp.se = invalid_val;
		sp.sx = invalid_val;
	}
	else {
		FVECTOR3 vEn = TestPrm.CamPos + vRay * sp.se;
		FVECTOR3 vEx = TestPrm.CamPos + vRay * sp.sx;
		if (dot(vEn, TestPrm.toSun) > 0) sp.se = invalid_val;
		if (dot(vEx, TestPrm.toSun) > 0) sp.sx = invalid_val;
	}

	return sp;
}


// ===========================================================================================
//
void vPlanet::TestComputations(Sketchpad* pSkp)
{
	//return;

	static int status = 0;
	float size = 0.02f;
	VECTOR3 cpos, rpos;
	VESSEL* pV = oapiGetFocusInterface();
	OBJHANDLE hV = pV->GetHandle();
	OBJHANDLE hR = pV->GetGravityRef();
	if (hR != hObj) return;

	oapiGetGlobalPos(hR, &rpos);
	oapiCameraGlobalPos(&cpos);
	cpos -= rpos;

	FVECTOR3 vCam(cpos);

	static FVECTOR3 vRef = 0;
	static FVECTOR3 vPos = 0;
	static FVECTOR3 vRay = 0;
	static float beta = 0.0f;

	if (length(GetScene()->vPickRay) > 0.8f) {
		vRef = cpos;
		beta = dot(unit(vRef), GetScene()->vPickRay);
		TestPrm.CamPos = cp.CamPos;
		TestPrm.toSun = cp.toSun;
		TestPrm.toCam = unit(vRef);
		TestPrm.CamRad = length(vRef);
		TestPrm.CamRad2 = TestPrm.CamRad * TestPrm.CamRad;
		TestPrm.ZeroAz = unit(cross(TestPrm.toCam, TestPrm.toSun));
		TestPrm.SunAz = unit(cross(TestPrm.toCam, TestPrm.ZeroAz));
		TestPrm.Up = unit(cross(TestPrm.ZeroAz, TestPrm.toSun));
		TestPrm.CosAlpha = min(1.0f, cp.PlanetRad / TestPrm.CamRad);
		TestPrm.SinAlpha = sqrt(1.0f - TestPrm.CosAlpha * TestPrm.CosAlpha);
	}


	// Trace picking ray --------------------------------------------
	//
	float Ref2 = dot(vRef, vRef);
	float Ref = sqrt(Ref2);
	float ds = Ref * beta;
	float he2 = Ref2 - ds * ds;

	if (length(GetScene()->vPickRay) > 0.8f)
	{
		if (he2 < cp.PlanetRad2 && beta < 0) {	// Surface contact
			float s = sqrt(cp.PlanetRad2 - he2);
			vPos = vRef + GetScene()->vPickRay * (-ds - s);
			status = 1;
		}
		else {
			if (!CameraInAtmosphere()) {	// Horizon ring contact
				float Alpha = acos(TestPrm.CosAlpha);
				float Beta = acos(-beta);
				float Gamma = PI - Alpha - Beta;
				float re = Ref * sin(Beta) / sin(Gamma);
				float di = sqrt(re * re + Ref2 - 2.0f * re * Ref * TestPrm.CosAlpha);
				vPos = vRef + GetScene()->vPickRay * di;
				status = 2;
			}
			else {	// Sky Dome contact
				float ew = sqrt(cp.AtmoRad2 - he2) - ds;
				vPos = vRef + GetScene()->vPickRay * ew;
				status = 3;
			}
		}
		GetScene()->vPickRay = 0;
		vRay = normalize(vPos - vRef); // From vRef to vPos
	}

	float cd = length(vRef - vPos);

	SHDPrm sp = ComputeShadow(vRay);

	FVECTOR4 rd = ComputeCameraView(vPos);

	D3D9DebugLog("Optical Depth=%f   RGB(%f, %f, %f)", rd.a, rd.r, rd.g, rd.b);

	float sl = dot(cp.toSun, TestPrm.toCam);
	float sa = dot(vRay, TestPrm.toCam);
	float rl = RayLength(-sa, TestPrm.CamRad);
	float rf = sqrt(cp.AtmoRad2 - cp.PlanetRad2);
	float ph = dot(vRay, cp.toSun);

	FVECTOR3 cl = SunLightColor(sl, cp.CamAlt);
	FVECTOR4 ral, mie;
	IntegrateSegment(vPos, rl, &ral, &mie);
	
	FVECTOR3 is = (ral.rgb + mie.rgb);// *MiePhase(-ph));

	D3D9DebugLog("Sunlight RGB(%f, %f, %f)", cl.x, cl.y, cl.z);
	D3D9DebugLog("InScatter RGB(%f, %f, %f)", is.x, is.y, is.z);
	D3D9DebugLog("RayLength = %f vs %f, sa = %f, sl = %f", rl, rf, sa, sl);

	double u = dot(vPos, TestPrm.Up);
	double t = dot(vPos, TestPrm.ZeroAz);
	bool bTgt = ((u * u + t * t) < cp.PlanetRad2 && dot(vPos, TestPrm.toSun) < 0);
	bool bSrc = (sp.cr < cp.PlanetRad&& dot(vRef, TestPrm.toSun) < 0);

	float s0 = 0, s1 = 0, e0 = 0, e1 = 0, mp = 0, lf = 0;

	if (status == 1) {
		s0 = sp.ae > 0 ? sp.ae : 0;
		e0 = sp.se > 0 ? min(cd, sp.se) : cd;
	}
	else {
		if (bSrc) {		// Camera In Shadow

			s0 = max(sp.sx, sp.ae);
			lf = max(0, sp.ca) / max(1.0f, abs(sp.hd)); // Lerp Factor
			mp = lerp((sp.ax + s0) * 0.5f, sp.hd, saturate(lf));

			e0 = mp;
			s1 = max(sp.sx, mp);
			e1 = sp.ax;
		}
		else { // Camera In Lit

			s0 = max(0, sp.ae);
			lf = max(0, sp.ca) / max(1.0f, abs(sp.hd)); // Lerp Factor
			mp = lerp((sp.ax + s0) * 0.5f, sp.hd, saturate(lf));

			bool bA = (sp.se > sp.ax || sp.se < 0);
			bool bB = (sp.sx > sp.ax || sp.sx < 0);

			if (bA && bA) sp.se = sp.sx = mp;
			else {
				if (bA) sp.se = mp;
				if (bB) sp.sx = sp.ax;
			}
			//if (sp.se > sp.ax || sp.se < 0) sp.se = mp;
			//if (sp.sx > sp.ax || sp.sx < 0) sp.sx = sp.ax;

			e0 = sp.se;
			s1 = sp.sx;
			e1 = sp.ax;
		}
	}


	int sz = 250;

	// Origin Point
	pSkp->QuickPen(0xFF00FF00);
	pSkp->QuickBrush(0xFF00FF00);
	pSkp->SetWorldBillboard(vRef - vCam, size);
	pSkp->Ellipse(-sz, -sz, sz, sz);

	// Test Point 'contact point'
	sz = 170;
	pSkp->QuickPen(0xFF0000FF);
	pSkp->QuickBrush(0xFF0000FF);
	pSkp->SetWorldBillboard(vRef - vCam + vRay * cd, size);
	pSkp->Ellipse(-sz, -sz, sz, sz);

	sz = 250;
	// Shadow Crossing
	if (sp.se != invalid_val) {
		pSkp->QuickPen(0xFFDD00DD);
		pSkp->QuickBrush(0xFFDD00DD);
		pSkp->SetWorldBillboard(vRef - vCam + vRay * sp.se, size);
		pSkp->Ellipse(-sz, -sz, sz, sz);
	}
	if (sp.sx != invalid_val) {
		pSkp->QuickPen(0xFFFF88FF);
		pSkp->QuickBrush(0xFFFF88FF);
		pSkp->SetWorldBillboard(vRef - vCam + vRay * sp.sx, size);
		pSkp->Ellipse(-sz, -sz, sz, sz);
	}
	sz = 170;
	if (sp.ae != invalid_val) {
		// Atmosphere entry points
		pSkp->QuickPen(0xFF008080);
		pSkp->QuickBrush(0xFF008080);
		pSkp->SetWorldBillboard(vRef - vCam + vRay * sp.ae, size);
		pSkp->Ellipse(-sz, -sz, sz, sz);
	}
	if (sp.ax != invalid_val) {
		pSkp->QuickPen(0xFF00F0F0);
		pSkp->QuickBrush(0xFF00F0F0);
		pSkp->SetWorldBillboard(vRef - vCam + vRay * sp.ax, size);
		pSkp->Ellipse(-sz, -sz, sz, sz);
	}
	sz = 120;
	if (sp.hd != invalid_val) {
		pSkp->QuickPen(0xFFFFFFFF);
		pSkp->QuickBrush(0xFFFFFFFF);
		pSkp->SetWorldBillboard(vRef - vCam + vRay * mp, size);
		pSkp->Ellipse(-sz, -sz, sz, sz);
	}


	if (bSrc) D3D9DebugLog("Camera in Shadow");
	if (bTgt) D3D9DebugLog("Target in Shadow");


	if (status == 1) D3D9DebugLog("SURFACE");
	if (status == 2) D3D9DebugLog("HORIZON");
	if (status == 3) D3D9DebugLog("SKYDOME");

	D3D9DebugLog("Shadow First=%f, Second=%f", sp.se, sp.sx);
	D3D9DebugLog("Atmosp First=%f, Second=%f", sp.ae, sp.ax);
	D3D9DebugLog("Hd=%f, Ca=%f", sp.hd, sp.ca);
	D3D9DebugLog("CameraInSpace=%f", cp.CamSpace);

	D3DXMATRIX mI; D3DXMatrixIdentity(&mI);
	VECTOR3 V0, V1;
	IVECTOR2 pt0, pt1;

	pSkp->SetViewMode(Sketchpad::ORTHO);
	pSkp->SetWorldTransform();

	FVECTOR3 vR = vRef - vCam;

	D3D9DebugLog("s0=%f, e0=%f, s1=%f, e1=%f, mp=%f, lf=%f", s0, e0, s1, e1, mp, lf);

	if (e0 > s0) {
		D3D9DebugLog("Primary Integral");
		V0 = (vR + vRay * s0)._V();
		V1 = V0 + vRay._V() * (e0 - s0);
		scn->WorldToScreenSpace(V0, &pt0);
		scn->WorldToScreenSpace(V1, &pt1);
		pSkp->QuickPen(0xFF90FF90);
		pSkp->Line(pt0.x, pt0.y, pt1.x, pt1.y);
	}

	if (e1 > s1) {
		D3D9DebugLog("Secondary Integral");
		V0 = (vR + vRay * s1)._V();
		V1 = V0 + vRay._V() * (e1 - s1);
		scn->WorldToScreenSpace(V0, &pt0);
		scn->WorldToScreenSpace(V1, &pt1);
		pSkp->QuickPen(0xFF9090FF);
		pSkp->Line(pt0.x, pt0.y, pt1.x, pt1.y);
	}
	pSkp->SetViewMode(Sketchpad::USER);
}


// ===========================================================================================
//
void vPlanet::UpdateScatter()
{
	if (scn->GetRenderPass() != RENDERPASS_MAINSCENE) return;
	if (surfmgr2 == NULL) return;
	if (mesh) return;

	if (HasAtmosphere())
	{
		if (!pSunColor) D3DXCreateTexture(pDev, 4*Qc, Qc/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pSunColor);
		if (!pRaySkyView) D3DXCreateTexture(pDev, Qc * 2, Qc, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pRaySkyView);
		if (!pMieSkyView) D3DXCreateTexture(pDev, Qc * 2, Qc, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pMieSkyView);
		if (!pLandViewRay) D3DXCreateTexture(pDev, Wc * Nc, Wc, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pLandViewRay);
		if (!pLandViewMie) D3DXCreateTexture(pDev, Wc * Nc, Wc, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pLandViewMie);
		if (!pLandViewAtn) D3DXCreateTexture(pDev, Wc * Nc, Wc, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pLandViewAtn);
		if (!pAmbientSky) D3DXCreateTexture(pDev, Qc, Qc, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &pAmbientSky);
	}

	FVECTOR3 SunDir = SunDirection();
	FVECTOR3 cam = -PosFromCamera();

	OBJHANDLE hPlanet = GetObject();

	const ScatterParams* atmo = GetAtmoParams();

	DWORD dAmbient = *(DWORD*)gc->GetConfigParam(CFGPRM_AMBIENTLEVEL);
	float fAmbient = float(dAmbient) * 0.0039f;

	double pr = GetSize() + minelev;		// Planet Min Radius
	double cr = CamDist();		// Camera distance from a planet center
	double ca = cr - pr;		// Camera altitude

	float mdh = 6000.0f;		// Minumum distance to horizon
	float scr = 4.0e-6;
	float scm = 1.6e-6;

	float g = float(atmo->mphase);
	float hrz = sqrt(max(0, cr * cr - pr * pr));
	float qw = float(pr / cr);

	LPDIRECT3DSURFACE9 pTgt, pTgt2;

	// ---------------------------------------------------------------------
	// Initialize camera centric tangent frame for normal mapped water
	//
	MATRIX3 mRot;
	oapiGetRotationMatrix(hPlanet, &mRot);
	VECTOR3 vNrm = mul(mRot, ReferencePoint());
	VECTOR3 vRot = unit(mul(mRot, _V(0, 1, 0)));
	VECTOR3 vTan = unit(crossp(vRot, vNrm));
	VECTOR3 vBiT = unit(crossp(vTan, vNrm));

	memcpy(&cp.mVP, scn->GetProjectionViewMatrix(), sizeof(FMATRIX4));

	cp.vPolarAxis = vRot;
	cp.vTangent = vTan;		// RefFrame for surface micro-tex and water
	cp.vBiTangent = vBiT;	// RefFrame for surface micro-tex and water
	cp.cSun = FVECTOR3(1, 1, 1) * 2.0f;
	cp.PlanetRad = float(pr);
	cp.PlanetRad2 = float(pr * pr);
	cp.MinAlt = float(minelev);
	cp.MaxAlt = float(maxelev);		// Max Elevation above sea-level
	cp.iAltRng = 1.0f / (cp.MaxAlt - cp.MinAlt);
	cp.CamAlt = float(ca);
	cp.CamElev = float(scn->GetCameraElevation());
	cp.CamRad = float(cr);
	cp.CamRad2 = float(cr * cr);
	cp.CamPos = cam;
	cp.toCam = unit(cam);
	cp.toSun = unit(SunDir);						// Sun-aligned Atmo Scatter RefFrame
	cp.ZeroAz = unit(cross(cp.toCam, cp.toSun));	// Sun-aligned Atmo Scatter RefFrame
	cp.SunAz = unit(cross(cp.toCam, cp.ZeroAz));	// Sun-aligned Atmo Scatter RefFrame
	cp.Up = unit(cross(cp.ZeroAz, cp.toSun));
	cp.HrzDst = sqrt(max(mdh, cp.CamRad2 - cp.PlanetRad2));
	cp.Time = fmod(oapiGetSimTime(), 3600.0f);
	cp.TrGamma = 1.0f / float(atmo->tgamma);
	cp.TrExpo = float(atmo->trb);
	cp.Ambient = fAmbient;
	cp.CosAlpha = min(1.0f, cp.PlanetRad / cp.CamRad);
	cp.SinAlpha = sqrt(1.0f - cp.CosAlpha * cp.CosAlpha);
	float A = dot(cp.toCam, cp.Up) * cp.CamRad;
	cp.Cr2 = A * A;
	float g2 = cp.CamRad2 - cp.PlanetRad2;
	cp.ShdDst = g2 > 0 ? sqrt(g2) : 0.0f;


	if (HasAtmosphere() == false) return;

	// Skip the rest if no atmosphere exists
	// ------------------------------------------------------------------------------------------------------------

	cp.AtmoAlt = atmo->visalt;
	cp.AtmoRad = atmo->visalt + cp.PlanetRad;
	cp.AtmoRad2 = cp.AtmoRad * cp.AtmoRad;
	cp.CloudAlt = float(prm.cloudalt);
	cp.CamSpace = sqrt(saturate(cp.CamAlt / atmo->visalt));
	cp.AngMin = -sqrt(max(1.0f, cp.CamRad2 - cp.PlanetRad2)) / cp.CamRad;
	cp.AngRng = 1.0f - cp.AngMin;
	cp.iAngRng = 1.0f / cp.AngRng;
	cp.AngCtr = sqrt(max(0.0f, 1.0f - qw * qw));
	cp.RayWave = unit(pow(FVECTOR3(atmo->red, atmo->green, atmo->blue), -atmo->rpow));
	cp.MieWave = unit(pow(FVECTOR3(atmo->red, atmo->green, atmo->blue), -atmo->mpow));

	cp.rmO.x = atmo->ray * scr;
	cp.rmO.y = atmo->mie * scm;
	cp.rmI.x = atmo->ray * atmo->rayin * scr;
	cp.rmI.y = atmo->mie * atmo->miein * scm;

	cp.iH.x = 1.0f / float(atmo->rheight * 1000.0);
	cp.iH.y = 1.0f / float(atmo->mheight * 1000.0);
	cp.RayPh = atmo->rphase;
	cp.Expo = atmo->aux3;
	cp.HG = FVECTOR4(1.5f * (1.0f - g * g) / (2.0f + g * g), 1.0f + g * g, 2.0f * g, float(atmo->mphaseb));

	cp.Clouds = float(atmo->aux2);
	cp.TW_Multi = float(atmo->tw_bri);
	cp.TW_Dst = float(atmo->tw_dst);

	sFlow Flow;
	Flow.bCamLit = !((cp.Cr2 < cp.PlanetRad2) && (dot(cp.toCam, cp.toSun) < 0));
	Flow.bCamInSpace = !CameraInAtmosphere();


	//
	// ----------------------------------------------------------------------------
	//
	pIP->Activate("SunColor");
	pIP->SetStruct("Const", &cp, sizeof(ConstParams));

	pSunColor->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (SunColor)");
	SAFE_RELEASE(pTgt);


	//
	// ----------------------------------------------------------------------------
	//
	if (CameraInAtmosphere()) pIP->Activate("SkyView");
	else pIP->Activate("RingView");

	Flow.bRay = true;
	pIP->SetStruct("Flo", &Flow, sizeof(sFlow));
	pIP->SetStruct("Const", &cp, sizeof(ConstParams));
	pIP->SetTextureNative("tSun", pSunColor, IPF_CLAMP | IPF_LINEAR);

	pRaySkyView->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (SkyView)");
	SAFE_RELEASE(pTgt);

	Flow.bRay = false;
	pIP->SetStruct("Flo", &Flow, sizeof(sFlow));
	pIP->SetStruct("Const", &cp, sizeof(ConstParams));
	pMieSkyView->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (SkyView)");
	SAFE_RELEASE(pTgt);


	//
	// ----------------------------------------------------------------------------
	//
	pIP->Activate("AmbientSky");
	pIP->SetStruct("Const", &cp, sizeof(ConstParams));
	pIP->SetTextureNative("tSkyRayColor", pRaySkyView, IPF_CLAMP | IPF_LINEAR);
	pIP->SetTextureNative("tSkyMieColor", pMieSkyView, IPF_CLAMP | IPF_LINEAR);

	pAmbientSky->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (AmbientSky)");
	SAFE_RELEASE(pTgt);


	//
	// ----------------------------------------------------------------------------
	//
	pIP->Activate("LandViewAtten");
	pIP->SetStruct("Const", &cp, sizeof(ConstParams));

	pLandViewAtn->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (AmbientSky)");
	SAFE_RELEASE(pTgt);


	//
	// ----------------------------------------------------------------------------
	//
	pIP->Activate("LandView");
	Flow.bRay = true;

	pIP->SetStruct("Const", &cp, sizeof(ConstParams));
	pIP->SetStruct("Flo", &Flow, sizeof(sFlow));
	pIP->SetTextureNative("tSun", pSunColor, IPF_CLAMP | IPF_LINEAR);
	pLandViewRay->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (SkyView)");
	SAFE_RELEASE(pTgt);


	Flow.bRay = false;
	pIP->SetStruct("Const", &cp, sizeof(ConstParams));
	pIP->SetStruct("Flo", &Flow, sizeof(sFlow));
	pLandViewMie->GetSurfaceLevel(0, &pTgt);
	pIP->SetOutputNative(0, pTgt);
	if (!pIP->Execute(true)) LogErr("pIP Execute Failed (SkyView)");
	SAFE_RELEASE(pTgt);
}

// ==============================================================

PlanetShader* vPlanet::GetShader(int id)
{
	if (id == PLT_CONFIG) return pRender[GetShaderID()];
	return pRender[id];
}

// ==============================================================

int vPlanet::GetShaderID()
{
	if (strcmp(ShaderName, "Moon") == 0) return PLT_MOON;
	if (strcmp(ShaderName, "Earth") == 0) return PLT_EARTH;
	if (strcmp(ShaderName, "Mars") == 0) return PLT_MARS;
	if (strcmp(ShaderName, "Giant") == 0) return PLT_GIANT;
	if (strcmp(ShaderName, "Auto") == 0)
	{
		bool has_atmosphere = HasAtmosphere();
		if (has_atmosphere) {
			bool has_ripples = HasRipples();
			bool render_shadows = CloudMgr2() != NULL;
			if (has_ripples || render_shadows) return PLT_EARTH;
			if (size > 6e6) return PLT_GIANT;
			return PLT_MARS;
		}
	}
	return PLT_MOON;
}

// ==============================================================

LPDIRECT3DTEXTURE9 vPlanet::GetScatterTable(int i)
{
	switch (i)
	{
	case SUN_COLOR: return pSunColor;
	case RAY_COLOR: return pRaySkyView;
	case MIE_COLOR: return pMieSkyView;
	case RAY_LAND: return pLandViewRay;
	case MIE_LAND: return pLandViewMie;
	case ATN_LAND: return pLandViewAtn;
	case SUN_GLARE: return pSunTex;
	case SKY_AMBIENT: return pAmbientSky;
	default: return NULL;
	}
	return NULL;
}

// ==============================================================

ScatterParams* vPlanet::GetAtmoParams(int mode)
{
	if (!prm.bAtm || prm.atm_hzalt == 0.0) {
		atm_mode = 1;
		return &SPrm;	// Return surface setup if a planet doesn't have atmosphere
	}

	double lorb = SPrm.orbalt;
	double horb = SPrm.orbalt * 10.0;
	double ca = CamDist() - size;
	double alt = saturate(ca / lorb);
	double halt = saturate((ca - lorb) / horb);

	if (mode == 0) {
		if (ca < abs(ca - lorb)) mode = 1;
		else {
			if (abs(ca - lorb) < abs(ca - horb)) mode = 2;
			else mode = 3;
		}
	}

	atm_mode = mode;

	if (mode == 1) return &SPrm;		// Surface configuration
	if (mode == 2) return &OPrm;		// Orbital configuration
	if (mode == 3) return &HPrm;		// High Orbital configuration

	if (alt < 0.9999)
	{
		// ----------------------------------------------------
		CPrm.miein = lerp(SPrm.miein, OPrm.miein, alt);
		CPrm.aux2 = lerp(SPrm.aux2, OPrm.aux2, alt);
		CPrm.aux3 = lerp(SPrm.aux3, OPrm.aux3, alt);
		CPrm.mheight = lerp(SPrm.mheight, OPrm.mheight, alt);
		CPrm.rheight = lerp(SPrm.rheight, OPrm.rheight, alt);
		CPrm.trb = lerp(SPrm.trb, OPrm.trb, alt);
		CPrm.mie = lerp(SPrm.mie, OPrm.mie, alt);
		CPrm.mphase = lerp(SPrm.mphase, OPrm.mphase, alt);
		CPrm.rphase = lerp(SPrm.rphase, OPrm.rphase, alt);
		CPrm.mpow = lerp(SPrm.mpow, OPrm.mpow, alt);
		CPrm.rayin = lerp(SPrm.rayin, OPrm.rayin, alt);
		CPrm.ray = lerp(SPrm.ray, OPrm.ray, alt);
		CPrm.rpow = lerp(SPrm.rpow, OPrm.rpow, alt);
		// ----------------------------------------------------
		CPrm.tgamma = lerp(SPrm.tgamma, OPrm.tgamma, alt);
		CPrm.mphaseb = lerp(SPrm.mphaseb, OPrm.mphaseb, alt);
		CPrm.hazei = lerp(SPrm.hazei, OPrm.hazei, alt);
		// ----------------------------------------------------
		CPrm.tw_bri = lerp(SPrm.tw_bri, OPrm.tw_bri, alt);
		CPrm.green = lerp(SPrm.green, OPrm.green, alt);
		CPrm.tw_dst = lerp(SPrm.tw_dst, OPrm.tw_dst, alt);
	}
	else {
		alt = 1.0 - halt;
		// ----------------------------------------------------
		CPrm.miein = lerp(HPrm.miein, OPrm.miein, alt);
		CPrm.aux2 = lerp(HPrm.aux2, OPrm.aux2, alt);
		CPrm.aux3 = lerp(HPrm.aux3, OPrm.aux3, alt);
		CPrm.mheight = lerp(HPrm.mheight, OPrm.mheight, alt);
		CPrm.rheight = lerp(HPrm.rheight, OPrm.rheight, alt);
		CPrm.trb = lerp(HPrm.trb, OPrm.trb, alt);
		CPrm.mie = lerp(HPrm.mie, OPrm.mie, alt);
		CPrm.mphase = lerp(HPrm.mphase, OPrm.mphase, alt);
		CPrm.rphase = lerp(HPrm.rphase, OPrm.rphase, alt);
		CPrm.mpow = lerp(HPrm.mpow, OPrm.mpow, alt);
		CPrm.rayin = lerp(HPrm.rayin, OPrm.rayin, alt);
		CPrm.ray = lerp(HPrm.ray, OPrm.ray, alt);
		CPrm.rpow = lerp(HPrm.rpow, OPrm.rpow, alt);
		// ----------------------------------------------------
		CPrm.tgamma = lerp(HPrm.tgamma, OPrm.tgamma, alt);
		CPrm.mphaseb = lerp(HPrm.mphaseb, OPrm.mphaseb, alt);
		CPrm.hazei = lerp(HPrm.hazei, OPrm.hazei, alt);
		// ----------------------------------------------------
		CPrm.tw_bri = lerp(HPrm.tw_bri, OPrm.tw_bri, alt);
		CPrm.green = lerp(HPrm.green, OPrm.green, alt);
		CPrm.tw_dst = lerp(HPrm.tw_dst, OPrm.tw_dst, alt);
	}

	CPrm.red = SPrm.red;
	CPrm.blue = SPrm.blue;
	CPrm.orbalt = SPrm.orbalt;
	CPrm.visalt = GetHorizonAlt();

	return &CPrm;
}

// ==============================================================

bool vPlanet::LoadAtmoConfig()
{
	char name[32];
	char path[256];

	oapiGetObjectName(hObj, name, 32);
	sprintf_s(path, "GC/%s.atm.cfg", name);

	FILEHANDLE hFile = oapiOpenFile(path, FILE_IN_ZEROONFAIL, CONFIG);
	if (!hFile) {
		LogErr("Rendering Configuration File Is Missing for [%s]", name);
		return false;
	}

	LogAlw("Loading Atmospheric Configuration file [%s] Handle=%s", path, _PTR(hFile));

	if (oapiReadItem_string(hFile, "Shader", ShaderName) == false) strcpy_s(ShaderName, 32, "Auto");

	LoadStruct(hFile, &SPrm, 0);
	LoadStruct(hFile, &OPrm, 1);
	LoadStruct(hFile, &HPrm, 2);

	oapiCloseFile(hFile, FILE_IN_ZEROONFAIL);

	if (!oapiPlanetHasAtmosphere(hObj)) return false;

	return true;

}

// ==============================================================

char* vPlanet::Label(const char* x)
{
	static char lbl[32];
	if (iConfig == 0) sprintf_s(lbl, 32, "Srf_%s", x);
	if (iConfig == 1) sprintf_s(lbl, 32, "Low_%s", x);
	if (iConfig == 2) sprintf_s(lbl, 32, "Hig_%s", x);
	return lbl;
}

// ==============================================================

void vPlanet::SaveStruct(FILEHANDLE hFile, ScatterParams* prm, int iCnf)
{
	iConfig = iCnf;
	oapiWriteItem_float(hFile, Label("RPwr"), prm->rpow);
	oapiWriteItem_float(hFile, Label("MPwr"), prm->mpow);
	// -----------------------------------------------------------------
	oapiWriteItem_float(hFile, Label("Expo"), prm->trb);
	oapiWriteItem_float(hFile, Label("TGamma"), prm->tgamma);
	// -----------------------------------------------------------------
	oapiWriteItem_float(hFile, Label("TWDst"), prm->tw_dst);
	oapiWriteItem_float(hFile, Label("Green"), prm->green);
	oapiWriteItem_float(hFile, Label("TWBri"), prm->tw_bri);
	// -----------------------------------------------------------------
	oapiWriteItem_float(hFile, Label("RayO"), prm->ray);
	oapiWriteItem_float(hFile, Label("RayI"), prm->rayin);
	oapiWriteItem_float(hFile, Label("RayP"), prm->rphase);
	oapiWriteItem_float(hFile, Label("RayH"), prm->rheight);
	// -----------------------------------------------------------------
	oapiWriteItem_float(hFile, Label("MieO"), prm->mie);
	oapiWriteItem_float(hFile, Label("MieP"), prm->mphase);
	oapiWriteItem_float(hFile, Label("MieI"), prm->miein);
	oapiWriteItem_float(hFile, Label("MieH"), prm->mheight);
	// -----------------------------------------------------------------
	oapiWriteItem_float(hFile, Label("Aux2"), prm->aux2);
	oapiWriteItem_float(hFile, Label("Aux3"), prm->aux3);
	oapiWriteItem_float(hFile, Label("Aux4"), prm->mphaseb);
	oapiWriteItem_float(hFile, Label("Aux5"), prm->hazei);
}

// ==============================================================

void vPlanet::LoadStruct(FILEHANDLE hFile, ScatterParams* prm, int iCnf)
{
	iConfig = iCnf;
	oapiReadItem_float(hFile, "OrbitAlt", prm->orbalt);
	oapiReadItem_float(hFile, "AtmoVisualAlt", prm->visalt);
	oapiReadItem_float(hFile, "Red", prm->red);
	oapiReadItem_float(hFile, "Blue", prm->blue);
	oapiReadItem_float(hFile, Label("RPwr"), prm->rpow);
	oapiReadItem_float(hFile, Label("MPwr"), prm->mpow);
	// -----------------------------------------------------------------
	oapiReadItem_float(hFile, Label("Expo"), prm->trb);
	oapiReadItem_float(hFile, Label("TGamma"), prm->tgamma);
	// -----------------------------------------------------------------
	oapiReadItem_float(hFile, Label("TWDst"), prm->tw_dst);
	oapiReadItem_float(hFile, Label("Green"), prm->green);
	oapiReadItem_float(hFile, Label("TWBri"), prm->tw_bri);
	// -----------------------------------------------------------------
	oapiReadItem_float(hFile, Label("RayO"), prm->ray);
	oapiReadItem_float(hFile, Label("RayI"), prm->rayin);
	oapiReadItem_float(hFile, Label("RayP"), prm->rphase);
	oapiReadItem_float(hFile, Label("RayH"), prm->rheight);
	// -----------------------------------------------------------------
	oapiReadItem_float(hFile, Label("MieO"), prm->mie);
	oapiReadItem_float(hFile, Label("MieP"), prm->mphase);
	oapiReadItem_float(hFile, Label("MieI"), prm->miein);
	oapiReadItem_float(hFile, Label("MieH"), prm->mheight);
	// -----------------------------------------------------------------
	oapiReadItem_float(hFile, Label("Aux2"), prm->aux2);
	oapiReadItem_float(hFile, Label("Aux3"), prm->aux3);
	oapiReadItem_float(hFile, Label("Aux4"), prm->mphaseb);
	oapiReadItem_float(hFile, Label("Aux5"), prm->hazei);
}

// ==============================================================

void vPlanet::SaveAtmoConfig()
{
	char name[64];
	char path[256];

	oapiGetObjectName(hObj, name, 64);
	sprintf_s(path, "GC/%s.atm.cfg", name);

	FILEHANDLE hFile = oapiOpenFile(path, FILE_OUT, CONFIG);

	oapiWriteItem_string(hFile, ";", "Shader(s) = [Earth, Mars, Moon, Giant, Auto]");
	oapiWriteItem_string(hFile, "Shader", ShaderName);
	oapiWriteItem_float(hFile, "OrbitAlt", SPrm.orbalt);
	oapiWriteItem_float(hFile, "AtmoVisualAlt", SPrm.visalt);
	oapiWriteItem_float(hFile, "Red", SPrm.red);
	oapiWriteItem_float(hFile, "Blue", SPrm.blue);

	SaveStruct(hFile, &SPrm, 0);
	SaveStruct(hFile, &OPrm, 1);
	SaveStruct(hFile, &HPrm, 2);

	oapiCloseFile(hFile, FILE_OUT);
}

