/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DModelDraw.h /////////////////////////////////////////////////////////////////////////
// Author: Colin Day, November 2001
// Desc:   Default client update module
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/ModelState.h"
#include "Common/DrawModule.h"
#ifdef BRUTAL_TIMING_HACK // hack for collecting model timing info.  jba.
class RenderObjClass {
public:
	enum AnimMode
	{
		ANIM_MODE_MANUAL		= 0,
		ANIM_MODE_LOOP,
		ANIM_MODE_ONCE,
		ANIM_MODE_LOOP_PINGPONG,
		ANIM_MODE_LOOP_BACKWARDS,	//make sure only backwards playing animations after this one
		ANIM_MODE_ONCE_BACKWARDS,

		ANIM_MODE_COUNT
	};
};

#else
#include "WW3D2/rendobj.h"
#endif
#include "Common/SparseMatchFinder.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/HeadlightShader.h"
#include "GameClient/PlanarMirrorShader.h"
#include "W3DLaserDraw.h"
#include "Common/STLTypedefs.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;
class RenderObjClass;
class Shadow;
class TerrainTracksRenderObjClass;
class HAnimClass;
enum GameLODLevel CPP_11(: Int);
//-------------------------------------------------------------------------------------------------
/** The default client update module */
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
typedef UnsignedInt64 TransitionSig;

const TransitionSig NO_TRANSITION = 0;

inline TransitionSig buildTransitionSig(NameKeyType src, NameKeyType dst)
{
	return (((UnsignedInt64)src) << 32) | ((UnsignedInt64)dst);
}

inline NameKeyType recoverSrcState(TransitionSig sig)
{
	return (NameKeyType)((sig >> 32) & 0xffffffff);
}

inline NameKeyType recoverDstState(TransitionSig sig)
{
	return (NameKeyType)((sig) & 0xffffffff);
}

//-------------------------------------------------------------------------------------------------
// please do not define RETAIN_ANIM_HANDLES. It prevents us from ever releasing animations.
// old code should be deleting by mid-Jan 2003. (srj)
#define NO_RETAIN_ANIM_HANDLES

//-------------------------------------------------------------------------------------------------
class W3DAnimationInfo
{
private:
	AsciiString						m_name;
#ifdef RETAIN_ANIM_HANDLES
	mutable HAnimClass*		m_handle;
#endif
	Real									m_distanceCovered;		// if nonzero, the distance covered by a single loop of the anim
	mutable Real					m_naturalDurationInMsec;
	Bool									m_isIdleAnim;					// if true, we pick another animation at random when this one completes

public:
	W3DAnimationInfo(const AsciiString& name, Bool isIdle, Real distanceCovered);
	W3DAnimationInfo(const W3DAnimationInfo &r);
	W3DAnimationInfo &operator=(const W3DAnimationInfo& r);

	~W3DAnimationInfo();

	HAnimClass* getAnimHandle() const;
	const AsciiString& getName() const { return m_name; }
	Bool isIdleAnim() const { return m_isIdleAnim; }
	Real getDistanceCovered() const { return m_distanceCovered; }
	Real getNaturalDurationInMsec() const { return m_naturalDurationInMsec; }

};
typedef std::vector<W3DAnimationInfo>	W3DAnimationVector;

//-------------------------------------------------------------------------------------------------
struct ParticleSysBoneInfo
{
	AsciiString boneName;
	const ParticleSystemTemplate* particleSystemTemplate;
};

typedef std::vector<ParticleSysBoneInfo> ParticleSysBoneInfoVector;

//-------------------------------------------------------------------------------------------------
struct FXEventInfo
{
	AsciiString boneName;
	const FXList* fx;
	UnsignedInt frame;
};

typedef std::vector<FXEventInfo> FXEventInfoVector;

//-------------------------------------------------------------------------------------------------
struct PristineBoneInfo
{
	Matrix3D mtx;
	Int boneIndex;
};
//typedef std::hash_map< NameKeyType, PristineBoneInfo, rts::hash<NameKeyType>, rts::equal_to<NameKeyType> > PristineBoneInfoMap;
typedef std::map< NameKeyType, PristineBoneInfo, std::less<NameKeyType>/**/> PristineBoneInfoMap;

//-------------------------------------------------------------------------------------------------

struct ModelConditionInfo
{
	struct HideShowSubObjInfo
	{
		AsciiString subObjName;
		Bool hide;
	};

	struct TurretInfo
	{
		// read from INI
		NameKeyType		m_turretAngleNameKey;
		NameKeyType		m_turretPitchNameKey;
		Real					m_turretArtAngle;
		Real					m_turretArtPitch;
		// calculated at runtime
		Int						m_turretAngleBone;
		Int						m_turretPitchBone;

		void clear()
		{
			m_turretAngleNameKey = NAMEKEY_INVALID;
			m_turretPitchNameKey = NAMEKEY_INVALID;
			m_turretArtAngle = 0;
			m_turretArtPitch = 0;
			m_turretAngleBone = 0;
			m_turretPitchBone = 0;
		}
	};

	struct WeaponBarrelInfo
	{
		Int							m_recoilBone;									///< the W3D bone for this barrel (zero == no bone)
		Int							m_fxBone;											///< the FX bone for this barrel (zero == no bone)
		Int							m_muzzleFlashBone;						///< the muzzle-flash subobj bone for this barrel (zero == none)
		Matrix3D				m_projectileOffsetMtx;				///< where the projectile fires from
#if defined(RTS_DEBUG) || defined(DEBUG_CRASHING)
		AsciiString			m_muzzleFlashBoneName;
#endif

		WeaponBarrelInfo()
		{
			clear();
		}

		void clear()
		{
			m_recoilBone = 0;
			m_fxBone = 0;
			m_muzzleFlashBone = 0;
			m_projectileOffsetMtx.Make_Identity();
#if defined(RTS_DEBUG) || defined(DEBUG_CRASHING)
			m_muzzleFlashBoneName.clear();
#endif
		}

		void setMuzzleFlashHidden(RenderObjClass *fullObject, Bool hide) const;
	};
	typedef std::vector<WeaponBarrelInfo>	WeaponBarrelInfoVec;

#if defined(RTS_DEBUG) || defined(DEBUG_CRASHING)
	AsciiString												m_description;
#endif
	std::vector<ModelConditionFlags>	m_conditionsYesVec;
	AsciiString												m_modelName;
	std::vector<HideShowSubObjInfo>		m_hideShowVec;
	mutable std::vector<AsciiString>	m_publicBones;
	AsciiString												m_weaponFireFXBoneName[WEAPONSLOT_COUNT];
#if RTS_ZEROHOUR
	const ParticleSystemTemplate* m_weaponFireParticleSys[WEAPONSLOT_COUNT];
#endif
	AsciiString												m_weaponRecoilBoneName[WEAPONSLOT_COUNT];
	AsciiString												m_weaponMuzzleFlashName[WEAPONSLOT_COUNT];
	AsciiString												m_weaponProjectileLaunchBoneName[WEAPONSLOT_COUNT];
	AsciiString												m_weaponProjectileHideShowName[WEAPONSLOT_COUNT];
	W3DAnimationVector								m_animations;
	NameKeyType												m_transitionKey;
	NameKeyType												m_allowToFinishKey;
	Int																m_flags;
	Int																m_iniReadFlags;	// not read from ini, but used for helping with default states
	RenderObjClass::AnimMode					m_mode;
	ParticleSysBoneInfoVector					m_particleSysBones;			///< Bone names and attached particle systems.
	TransitionSig											m_transitionSig;
	Real															m_animMinSpeedFactor; //Min speed factor (randomized each time it's played)
	Real															m_animMaxSpeedFactor; //Max speed factor (randomized each time it's played)

	FXEventInfoVector									m_fxEvents;			///<frames, Bone names and attached FXLists

	mutable PristineBoneInfoMap				m_pristineBones;
	mutable TurretInfo								m_turrets[MAX_TURRETS];
	mutable WeaponBarrelInfoVec				m_weaponBarrelInfoVec[WEAPONSLOT_COUNT];
	mutable Bool											m_hasRecoilBonesOrMuzzleFlashes[WEAPONSLOT_COUNT];
	mutable Byte											m_validStuff;

	// Animation Blending (needs WalkerDraw)
	UnsignedInt m_animBlendTime;


	enum
	{
		PRISTINE_BONES_VALID		= 0x0001,
		TURRETS_VALID						= 0x0002,
		HAS_PROJECTILE_BONES		= 0x0004,
		BARRELS_VALID						= 0x0008,
		PUBLIC_BONES_VALID			= 0x0010
	};

	ModelConditionInfo()
	{
		clear();
	}

	ModelConditionInfo(const ModelConditionInfo&) = default;
	ModelConditionInfo& operator=(const ModelConditionInfo&) = default;
	ModelConditionInfo(ModelConditionInfo&&) noexcept = default;
	ModelConditionInfo& operator=(ModelConditionInfo&&) = default;

	void clear();
	void loadAnimations() const;
	void preloadAssets( TimeOfDay timeOfDay, Real scale );			///< preload any assets for time of day

	Int getConditionsYesCount() const { DEBUG_ASSERTCRASH(!m_conditionsYesVec.empty(), ("empty m_conditionsYesVec.size(), see srj")); return m_conditionsYesVec.size(); }
	const ModelConditionFlags& getNthConditionsYes(Int i) const { return m_conditionsYesVec[i]; }
#if defined(RTS_DEBUG)
	inline AsciiString getDescription() const { return m_description; }
#endif

	const Matrix3D* findPristineBone(NameKeyType boneName, Int* boneIndex) const;
	Bool findPristineBonePos(NameKeyType boneName, Coord3D& pos) const;
	void addPublicBone(const AsciiString& boneName) const;
	Bool matchesMode(Bool night, Bool snowy) const;

	void validateStuff(RenderObjClass* robj, Real scale, const std::vector<AsciiString>& extraPublicBones) const;

private:
	void validateWeaponBarrelInfo() const;
	void validateTurretInfo() const;
	void validateCachedBones(RenderObjClass* robj, Real scale) const;
};
typedef std::vector<ModelConditionInfo> ModelConditionVector;
static_assert(std::is_nothrow_move_constructible<ModelConditionInfo>::value, "ModelConditionVector would copy its states on growth");

//-------------------------------------------------------------------------------------------------
//typedef std::hash_map< TransitionSig, ModelConditionInfo, std::hash<TransitionSig>, std::equal_to<TransitionSig> > TransitionMap;
typedef std::map< TransitionSig, ModelConditionInfo, std::less<TransitionSig>/**/> TransitionMap;

//-------------------------------------------------------------------------------------------------
// this is more efficient and also helps solve a projectile-launch-offset problem for double-upgraded
// technicals. it is not within risk, however, and WILL NOT WORK if the attach-to-module is animated
// in any meaningful way, so be careful. (srj)
#define CACHE_ATTACH_BONE

//-------------------------------------------------------------------------------------------------
class W3DModelDrawModuleData : public ModuleData
{
public:

	mutable ModelConditionVector			m_conditionStates;
	mutable SparseMatchFinder< ModelConditionInfo, ModelConditionFlags >
																		m_conditionStateMap;
	mutable TransitionMap							m_transitionMap;
	std::vector<AsciiString>					m_extraPublicBones;
	AsciiString												m_trackFile;						///< if present, leaves tracks using this texture
	AsciiString												m_attachToDrawableBone;
#ifdef CACHE_ATTACH_BONE
	mutable Vector3										m_attachToDrawableBoneOffset;
#endif
	Int																m_defaultState;
	Int																m_lastRealConditionStateIndex;	///< index of last DefaultConditionState/ConditionState, used by AutoConditionState
	Int																m_projectileBoneFeedbackEnabledSlots;	///< Hide and show the launch bone geometries according to clip status adjustments.
	Real															m_initialRecoil;
	Real															m_maxRecoil;
	Real															m_recoilDamping;
	Real															m_recoilSettle;
	StaticGameLODLevel								m_minLODRequired;				///< minumum game LOD level necessary to use this module.
	ModelConditionFlags								m_ignoreConditionStates;
	Bool															m_okToChangeModelColor;
	Bool															m_animationsRequirePower;///< Should UnderPowered disable type pause animations in this draw module?
#ifdef CACHE_ATTACH_BONE
	mutable Bool											m_attachToDrawableBoneOffsetValid;
#endif
	mutable Byte											m_validated;

  Bool                              m_particlesAttachedToAnimatedBones;

  Bool                              m_receivesDynamicLights; ///< just like it sounds... it sets a property of Drawable, actually

  Bool                              m_ignoreAnimScaling; ///< ignore external scaling of animation speed, e.g. for PreAttack.

#if RTS_ZEROHOUR
	Coord3D m_modelOffset;
	Real m_modelYaw;
	Real m_modelPitch;
	Real m_modelRoll;
#endif
	Bool															m_ignoreRotation;  ///< ignore all rotations for this draw module 

	Bool															m_showForOwnerOnly;  ///< show this model only to the owning player 

	Bool															m_keepRecoilAcrossStates;  ///< Don't reset recoil bones when switching states

	HeadlightShaderTuning							m_headlightTuning;  ///< HeadlightShader and the Headlight keys, unset where the model takes GameData.ini's

	DisruptionShaderInfo							m_disruption;  ///< DisruptionShader and its settings, for translucent meshes that bend the scene behind them

	Bool															m_flameShader;  ///< shade the translucent meshes as fire
	Bool															m_electricShader;  ///< shade them as electricity, unless FlameShader or CryoShader is on
	Bool															m_cryoShader;  ///< shade them as ice, over either
	FlameShaderTuning									m_flameTuning;
	BeamShaderTuning									m_beamTuning;  ///< the electric and cryo settings

	Bool															m_planarMirror;  ///< some meshes reflect the scene as level mirrors, over any other mesh shader
	std::vector<AsciiString>					m_planarMirrorMeshes;  ///< the mirror meshes by name, every mesh where empty
	PlanarMirrorShaderTuning					m_planarMirrorTuning;  ///< the look keys, unset where the model takes GameData.ini's

	// Bool															m_disableMoveEffectsOverWater;  ///< disable track marks and tread/wheel anims over water

	W3DModelDrawModuleData();
	virtual ~W3DModelDrawModuleData() override;
	void validateStuffForTimeAndWeather(const Drawable* draw, Bool night, Bool snowy) const;
	static void buildFieldParse(MultiIniFieldParse& p);
 	AsciiString getBestModelNameForWB(const ModelConditionFlags& c) const;
	const ModelConditionInfo* findBestInfo(const ModelConditionFlags& c) const;
	void preloadAssets( TimeOfDay timeOfDay, Real scale ) const;
#ifdef CACHE_ATTACH_BONE
	const Vector3* getAttachToDrawableBoneOffset(const Drawable* draw) const;
#endif

	// ugh, hack
	virtual const W3DModelDrawModuleData* getAsW3DModelDrawModuleData() const override { return this; }
	virtual StaticGameLODLevel getMinimumRequiredGameLOD() const override { return m_minLODRequired;}

private:
	static void parseConditionState( INI* ini, void *instance, void * /*store*/, const void* /*userData*/ );

public:
 	virtual void crc( Xfer *xfer ) override;
 	virtual void xfer( Xfer *xfer ) override;
 	virtual void loadPostProcess() override;
};

//-------------------------------------------------------------------------------------------------
class W3DModelDraw : public DrawModule, public ObjectDrawInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DModelDraw, "W3DModelDraw" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( W3DModelDraw, W3DModelDrawModuleData )

public:

	W3DModelDraw( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

  /// preloading assets
	virtual void preloadAssets( TimeOfDay timeOfDay ) override;

	/// the draw method
	virtual void doDrawModule(const Matrix3D* transformMtx) override;
	virtual void setShadowsEnabled(Bool enable) override;
	virtual void releaseShadows() override;	///< frees all shadow resources used by this module - used by Options screen.
	virtual void allocateShadows() override; ///< create shadow resources if not already present. Used by Options screen.

#if defined(RTS_DEBUG)
	virtual void getRenderCost(RenderCost & rc) const override;  ///< estimates the render cost of this draw module
	void getRenderCostRecursive(RenderCost & rc,RenderObjClass * robj) const;
#endif

	virtual void setFullyObscuredByShroud(Bool fullyObscured) override;
	virtual void setTerrainDecal(TerrainDecalType type) override;
	// TheSuperHackers @feature Selection ring, kept in its own slot so it does not evict the
	// horde or chem suit decal while a unit is selected.
	virtual void setSelectionDecal(Bool enable, Real radius, Color color, Bool footprint = FALSE, Real dottedRadius = 0.0f, Color dottedColor = 0) override;

	virtual Bool isVisible() const override;
	virtual void reactToTransformChange(const Matrix3D* oldMtx, const Coord3D* oldPos, Real oldAngle) override;
	virtual void reactToTeleport();
	virtual void reactToGeometryChange() override { }

	// this method must ONLY be called from the client, NEVER From the logic, not even indirectly.
	virtual Bool clientOnly_getRenderObjInfo(Coord3D* pos, Real* boundingSphereRadius, Matrix3D* transform) const override;
	virtual Bool clientOnly_getRenderObjBoundBox(OBBoxClass * boundbox) const override;
	virtual Bool clientOnly_getRenderObjBoneTransform(const AsciiString & boneName,Matrix3D * set_tm) const override;
#if defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	virtual Int clientOnly_getSubObjectNames(AsciiString* names, Int maxNames) const override;
	virtual Bool clientOnly_getModelNameAndCenter(AsciiString* name, Coord3D* center) const override;
#endif
	virtual Int getPristineBonePositionsForConditionState(const ModelConditionFlags& condition, const char* boneNamePrefix, Int startIndex, Coord3D* positions, Matrix3D* transforms, Int maxBones) const override;
	virtual Int getCurrentBonePositions(const char* boneNamePrefix, Int startIndex, Coord3D* positions, Matrix3D* transforms, Int maxBones) const override;
	virtual Bool getCurrentWorldspaceClientBonePositions(const char* boneName, Matrix3D& transform) const override;
	virtual Bool getProjectileLaunchOffset(const ModelConditionFlags& condition, WeaponSlotType wslot, Int specificBarrelToUse, Matrix3D* launchPos, WhichTurretType tur, Coord3D* turretRotPos, Coord3D* turretPitchPos = nullptr
#if RTS_ZEROHOUR
		, Matrix3D* turretAxes = nullptr
#endif
	) const override;
	virtual void updateProjectileClipStatus( UnsignedInt shotsRemaining, UnsignedInt maxShots, WeaponSlotType slot ) override; ///< This will do the show/hide work if ProjectileBoneFeedbackEnabled is set.
	virtual void updateDrawModuleSupplyStatus( Int maxSupply, Int currentSupply ) override; ///< This will do visual feedback on Supplies carried
	virtual void notifyDrawModuleDependencyCleared() override {}///< if you were waiting for something before you drew, it's ready now

	virtual void setHidden(Bool h) override;
	virtual void replaceModelConditionState(const ModelConditionFlags& c) override;
	virtual void replaceIndicatorColor(Color color) override;
	virtual void refreshIndicatorColor() override;
	virtual Bool handleWeaponFireFX(WeaponSlotType wslot, Int specificBarrelToUse, const FXList* fxl, Real weaponSpeed, const Coord3D* victimPos, Real damageRadius
#if RTS_ZEROHOUR
		, Bool emitFireParticles
#endif
	) override;
	virtual Bool handleWeaponPreAttackFX(WeaponSlotType wslot, Int specificBarrelToUse, const FXList* fxl, Real weaponSpeed, const Coord3D* victimPos, Real damageRadius);
	virtual Int getBarrelCount(WeaponSlotType wslot) const override;
	virtual void setSelectable(Bool selectable) override; // Change the selectability of the model.

	/**
		This call says, "I want the current animation (if any) to take n frames to complete a single cycle".
		If it's a looping anim, each loop will take n frames. someday, we may want to add the option to insert
		"pad" frames at the start and/or end, but for now, we always just "stretch" the animation to fit.
		Note that you must call this AFTER setting the condition codes.
	*/
	virtual void setAnimationLoopDuration(UnsignedInt numFrames) override;
	virtual bool isIgnoreAnimLoopDuration() const { return getW3DModelDrawModuleData()->m_ignoreAnimScaling; }

	/**
		similar to the above, but assumes that the current state is a "ONCE",
		and is smart about transition states... if there is a transition state
		"in between", it is included in the completion time.
	*/
	virtual void setAnimationCompletionTime(UnsignedInt numFrames) override;

	/**
	  This call is used to pause or resume an animation.
	*/
	virtual void setPauseAnimation(Bool pauseAnim) override;

	//Kris: Manually set a drawable's current animation to specific frame.
	virtual void setAnimationFrame( int frame ) override;

	virtual void updateSubObjects() override;
	virtual void showSubObject( const AsciiString& name, Bool show ) override;

#ifdef ALLOW_ANIM_INQUIRIES
// srj sez: not sure if this is a good idea, for net sync reasons...
	virtual Real getAnimationScrubScalar() const;
#endif

	virtual ObjectDrawInterface* getObjectDrawInterface() override { return this; }
	virtual const ObjectDrawInterface* getObjectDrawInterface() const override { return this; }

	///@todo: I had to make this public because W3DDevice needs access for casting shadows -MW
	RenderObjClass *getRenderObject() { return m_renderObject; }
	const RenderObjClass *getRenderObject() const { return m_renderObject; }
	virtual Bool updateBonesForClientParticleSystems() override;///< this will reposition particle systems on the fly ML

	virtual void handleFXEvents();  // Check frame times and trigger FX events at correct positions

	virtual void onDrawableBoundToObject() override;
	virtual void setTerrainDecalSize(Real x, Real y) override;
	virtual void setTerrainDecalOpacity(Real o) override;

protected:

	virtual void onRenderObjRecreated(){};

	const ModelConditionInfo* getCurState() const { return m_curState; }

	void setModelState(const ModelConditionInfo* newState);
	const ModelConditionInfo* findBestInfo(const ModelConditionFlags& c) const;
	void handleClientTurretPositioning();
	void computeDrawnTurretAngles(Real* angles, Real* pitches);
	void handleClientRecoil();
#if RTS_ZEROHOUR && (defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE))
	virtual Bool getDebugModelAlignment(ModelAlignment& alignment) const override;
	virtual void setDebugModelAlignment(const ModelAlignment* alignment) override;
#endif
	void recalcBonesForClientParticleSystems();
#if RTS_ZEROHOUR
	void updateFrozenBoneParticleSystems();
	Bool m_useModelTransformForParticles;
	mutable Bool m_hasAlignedAttachment;
#endif
	void stopClientParticleSystems();
#if RTS_ZEROHOUR
	void updateWeaponFireParticleSystems();
	void stopWeaponFireParticleSystems();
#endif
	const ParticleSystemTemplate* findMoveEffectTemplate(const AsciiString& name) const;	///< snow drawables take "<name>Snow" when it exists
	void doHideShowSubObjs(const std::vector<ModelConditionInfo::HideShowSubObjInfo>* vec);
	virtual void adjustTransformMtx(Matrix3D& mtx) const;
#if RTS_ZEROHOUR
	ModelAlignment getModelAlignment() const;
	Bool hasModelAlignment() const;
	void applyModelAlignment(Matrix3D& mtx, Real offsetScale = 1.0f) const;
	Bool getBoneAlignment(const ModelConditionFlags& condition, Matrix3D& mtx, Int remainingAttachments = -1, Vector3* unalignedAttachment = nullptr) const;
	Bool getDrawableBoneAttachment(const ModelConditionFlags& condition, Matrix3D& attachment, Bool& aligned, Int remainingAttachments) const;
	virtual Bool getPristineBoneAttachment(const ModelConditionFlags& condition, const char* boneName, Matrix3D& attachment, Bool& aligned, Int remainingAttachments) const override;
	void updateRenderObjectTransform(const Matrix3D& transform, Bool adjustForState);
	void updateRenderObjectTransform(Bool adjustForState);
#endif

	Real getCurAnimDistanceCovered() const;
	Bool setCurAnimDurationInMsec(Real duration);


	Bool getFullyObscuredByShroud() const { return m_fullyObscuredByShroud; }

private:

	struct WeaponRecoilInfo
	{
		enum RecoilState
		{
			IDLE, RECOIL_START, RECOIL, SETTLE
		};

		RecoilState			m_state;											///< what state this gun is in
		Real						m_shift;											///< how far the gun barrel has recoiled
		Real						m_recoilRate;									///< how fast the gun barrel is recoiling

		WeaponRecoilInfo()
		{
			clear();
		}

		void clear()
		{
			m_state = IDLE;
			m_shift = 0.0f;
			m_recoilRate = 0.0f;
		}
	};


	struct ParticleSysTrackerType
	{
		ParticleSystemID id;
		Int				boneIndex;
#if RTS_ZEROHOUR
		Matrix3D frozenBoneTransform;
		Bool followsAnimatedBone;
#endif
	};

	struct AnimInfoHelper
	{
		Int		frameNum;
		Int		mode;
		float	numFrames;
		float	fraction;
	};
	AnimInfoHelper m_prevAnimHelper;
	AnimInfoHelper getCurrentAnimHelper() const;


	typedef std::vector<WeaponRecoilInfo>	WeaponRecoilInfoVec;
	typedef std::vector<ParticleSysTrackerType>	ParticleSystemIDVec;
	//typedef std::vector<ParticleSystemID>	ParticleSystemIDVec;


	const ModelConditionInfo*			m_curState;
	const ModelConditionInfo*			m_nextState;
	UnsignedInt										m_nextStateAnimLoopDuration;
	Int														m_hexColor;
	Int														m_whichAnimInCurState;						///< the index of the currently playing anim in cur state (if any)
	WeaponRecoilInfoVec						m_weaponRecoilInfoVec[WEAPONSLOT_COUNT];
	Bool													m_needRecalcBoneParticleSystems;
#if RTS_ZEROHOUR && (defined(RTS_DEBUG) || defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE))
	ModelAlignment m_debugModelAlignment;
	Bool m_hasDebugModelAlignment;
#endif
	Bool													m_fullyObscuredByShroud;
	Bool													m_shadowEnabled;	///< cached state of shadow.  Used to determine if shadows should be enabled via options screen.
	RenderObjClass*								m_renderObject;										///< W3D Render object for this drawable
	Shadow*												m_shadow;													///< Updates/Renders shadows of this object
	Shadow*												m_terrainDecal;
	// TheSuperHackers @feature Selection ring decal, independent of m_terrainDecal.
	Shadow*												m_selectionDecal;
	// TheSuperHackers @fix The wanted state is remembered separately so the ring survives
	// render object rebuilds - a damage state or upgrade swaps the model while the object
	// stays selected.
	Bool													m_selectionDecalWanted;
	Real													m_selectionDecalRadius;
	Color													m_selectionDecalColor;
	Bool													m_selectionDecalFootprint;
	Real													m_selectionDecalDottedRadius;
	Color													m_selectionDecalDottedColor;
	// TheSuperHackers @feature Display decal from the template, in its own slot so it coexists
	// with the object's shadow.
	Shadow*												m_objectDecal;
	TerrainTracksRenderObjClass*	m_trackRenderObject;							///< This is rendered under object
	Bool													m_lastTrackWasBackwards;					///< travel direction of the last laid tread edge, to detect fwd<->rev flips
	ParticleSystemIDVec						m_particleSystemIDs;							///< The ID numbers of the particle systems currently running.
#if RTS_ZEROHOUR
	struct WeaponFireParticleTracker
	{
		ParticleSystemID id;
		AsciiString boneName;
		Int boneIndex;
	};
	std::vector<WeaponFireParticleTracker> m_weaponFireParticleSystems;
#endif
	std::vector<ModelConditionInfo::HideShowSubObjInfo>		m_subObjectVec;
	Bool													m_hideHeadlights;

	// A HEADLIGHT mesh the headlight shader draws for, with its beam in the mesh's own space.
	struct Headlight
	{
		Int subObject;
		Vector3 start;
		Vector3 end;
		Real radius;
	};
	std::vector<Headlight>				m_headlights;
	RenderObjClass*								m_headlightSource;								///< the render object m_headlights was found in, never dereferenced

	Bool													m_pauseAnimation;
	Int														m_animationMode;
	Bool													m_isFirstDrawModule;
	Real													m_drawnTurretAngle[MAX_TURRETS][2];	///< turret angle one logic frame back, and at m_drawnTurretFrame
	Real													m_drawnTurretPitch[MAX_TURRETS][2];	///< turret pitch one logic frame back, and at m_drawnTurretFrame
	UnsignedInt										m_drawnTurretFrame;
	Bool													m_drawnTurretValid;

	void adjustAnimation(const ModelConditionInfo* prevState, Real prevAnimFraction);
	Real getCurrentAnimFraction() const;
	void applyCorrectModelStateAnimation();
	const ModelConditionInfo* findTransitionForSig(TransitionSig sig) const;
	void rebuildWeaponRecoilInfo(const ModelConditionInfo* state, bool clear = TRUE);
	void doHideShowProjectileObjects( UnsignedInt showCount, UnsignedInt maxCount, WeaponSlotType slot );///< Means effectively, show m of n.
	void nukeCurrentRender(Matrix3D* xform);
	void rebuildRenderObject(); ///< recreate the render object, picking up the current house color
	void createObjectDecal();
	void updateObjectDecalVisibility();
	void doStartOrStopParticleSys();
	void adjustAnimSpeedToMovementSpeed();
	static void hideAllMuzzleFlashes(const ModelConditionInfo* state, RenderObjClass* renderObject);
	void hideAllHeadlights(Bool hide);
	void addHeadlight(Int subObject, RenderObjClass* mesh);
	void submitHeadlights();
#if defined(RTS_DEBUG)	//art wants to see buildings without flags as a test.
	void hideGarrisonFlags(Bool hide);
#endif
};
