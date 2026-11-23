#pragma once
// What each adaptive quality level takes away. Client only, so user options keep their values.
// the levels are a list and level 0 is always full quality.

namespace AdaptiveQualityLevels
{
	enum { MAX_LEVELS = 16 };	// level 0 plus up to 15 step-down levels

	enum ParticleLod
	{
		PARTICLE_LOD_FULL = 0,
		PARTICLE_LOD_MEDIUM = 1,
		PARTICLE_LOD_LOW = 2
	};

	static const int kMinParticleCap = 500;

	// everything a level can take away, a level states its whole state and levels do not add up
	struct Level
	{
		bool heatEffects = true;
		bool shadowVolumes = true;
		bool shadowDecals = true;
		bool lightPulses = true;
		bool renderSkip = false;		// every 2nd frame is not rendered
		int particleLod = PARTICLE_LOD_FULL;
		int particleCapPercent = 100;
		int catchUpFrames = 2;		// logic frames the game may fall behind and one drawn frame may run to make them up, 1 = no catch-up
		double defaultSavingMs = 0.0;	// frame cost the level is assumed to save until it is measured on this machine
		char name[32] = "";
	};

	struct Config
	{
		int count;		// levels including level 0
		Level levels[MAX_LEVELS];

		// logic catch-up takes the place of the old render skip level and grows at the first level below full quality
		Config() : count(4)
		{
			Level& particles = levels[1];
			particles.heatEffects = false;
			particles.particleLod = PARTICLE_LOD_MEDIUM;
			particles.particleCapPercent = 75;
			particles.catchUpFrames = 3;
			particles.defaultSavingMs = 1.0;
			copyName(particles, "particles");

			Level& shadows = levels[2];
			shadows = particles;
			shadows.shadowVolumes = false;
			shadows.defaultSavingMs = 0.5;
			copyName(shadows, "shadow volumes");

			Level& minimum = levels[3];
			minimum = shadows;
			minimum.shadowDecals = false;
			minimum.lightPulses = false;
			minimum.particleLod = PARTICLE_LOD_LOW;
			minimum.particleCapPercent = 50;
			minimum.defaultSavingMs = 2.0;
			copyName(minimum, "minimum");
		}

		static void copyName(Level& level, const char* name)
		{
			int i = 0;
			for (; name[i] != '\0' && i < (int)sizeof(level.name) - 1; ++i)
			{
				level.name[i] = name[i];
			}
			level.name[i] = '\0';
		}
	};

	inline Config& config()
	{
		static Config c;
		return c;
	}

	inline int levelCount() { return config().count; }

	inline const Level& level(int l)
	{
		const int last = config().count - 1;
		return config().levels[l < 0 ? 0 : (l > last ? last : l)];
	}

	inline bool heatEffectsOff(int l) { return !level(l).heatEffects; }
	inline bool shadowVolumesOff(int l) { return !level(l).shadowVolumes; }
	inline bool rendersSkipped(int l) { return level(l).renderSkip; }
	inline bool shadowDecalsOff(int l) { return !level(l).shadowDecals; }
	inline bool lightPulsesOff(int l) { return !level(l).lightPulses; }
	inline int catchUpFrames(int l) { return level(l).catchUpFrames; }

	// Never above the user cap, never below kMinParticleCap unless the user cap is lower
	inline int particleCap(int userCap, int l)
	{
		const int scaled = userCap * level(l).particleCapPercent / 100;
		const int floor = userCap < kMinParticleCap ? userCap : kMinParticleCap;
		return scaled > floor ? scaled : floor;
	}
}
