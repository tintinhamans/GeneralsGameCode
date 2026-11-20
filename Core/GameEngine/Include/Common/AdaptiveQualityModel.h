#pragma once
// Decides when the adaptive quality steps down or up. Pure logic: one call per counted wall clock second, no engine types.

#include <algorithm>
#include "AdaptiveQualityLevels.h"

namespace AdaptiveQualityTuning
{
	static const int kLearnSeconds = 3;				// seconds averaged before and after a step to learn its saving

	// the catch-up tuning
	struct Values
	{
		double budgetMs = 1000.0 / 60.0;		// frame budget: 60 fps
		double overFrameMs = 15.0;			// a frame missed its deadline when it took longer than this, or than the period of a lower fps cap
		double stepDownOverFraction = 0.10;	// step down when this share of frames is over ...
		int stepDownOverSeconds = 3;			// ... for this many counted seconds in a row
		double stepDownP50Ms = 13.3;			// or when the median frame cost is above this ...
		int stepDownP50Seconds = 2;			// ... for this many counted seconds in a row
		double cooldownSeconds = 3.0;		// wait after any step
		double stepUpOverFraction = 0.02;	// step up needs fewer than this share of frames over ...
		double stepUpP95Ms = 12.5;			// ... and p95 plus the saving of the level below this ...
		double stepUpSeconds = 15.0;			// ... for this many counted seconds
		double stepUpMaxSeconds = 120.0;		// the bounce guard doubles the wait up to this
		double bounceSeconds = 60.0;			// a step down this soon after a step up is a bounce
		double stepUpAfterUpSeconds = 30.0;	// counted seconds measured at the new level before another step up
		double stepUpUnlearnedP95Ms = 10.0;	// p95 limit for a step up when the saving of the level was never learned
		double maxSavingMs = 20.0;
		double minUsefulSavingMs = 0.3;		// a level that saved less than this is skipped on the way down
		double warmupSeconds = 10.0;			// seconds at the start of a match that do not count
		double networkWaitIgnoreFraction = 0.5;	// a second with more frames than this waiting on the network does not count
		double syncedMissRefreshes = 1.5;	// presented in sync with the display, a real miss takes two refreshes
		int catchUpFrames = 3;				// logic frames run per drawn frame at most when the game is behind, 1 = no catch-up; network games and skirmish
		double catchUpBudgetMs = 8.0;		// logic frames past the second one start only while this drawn frame's logic took less than this
	};

	inline Values& values()
	{
		static Values v;
		return v;
	}

	// the target period of a frame: the fps cap but never chasing more than 60 fps, and never faster than a synced display; fpsCap 0 is uncapped
	inline double frameDeadlineMs(int fpsCap, double refreshMs, bool presentSynced)
	{
		const double period = fpsCap > 0 ? 1000.0 / fpsCap : 0.0;
		double deadline = period > values().overFrameMs ? period : values().overFrameMs;
		if (presentSynced && refreshMs * values().syncedMissRefreshes > deadline)
		{
			deadline = refreshMs * values().syncedMissRefreshes;
		}
		return deadline;
	}

	enum DeadlineKind { DEADLINE_FLOOR, DEADLINE_CAP, DEADLINE_VSYNC };

	// which of the three limits frameDeadlineMs picked
	inline DeadlineKind frameDeadlineKind(int fpsCap, double refreshMs, bool presentSynced)
	{
		const double period = fpsCap > 0 ? 1000.0 / fpsCap : 0.0;
		const double deadline = period > values().overFrameMs ? period : values().overFrameMs;
		if (presentSynced && refreshMs * values().syncedMissRefreshes > deadline)
		{
			return DEADLINE_VSYNC;
		}
		return period > values().overFrameMs ? DEADLINE_CAP : DEADLINE_FLOOR;
	}

	// frame cost: the CPU side, plus the Present wait when the frame missed its deadline (GPU bound, not vsync)
	inline double frameCostMs(double cpuMs, double presentMs, double deadlineMs)
	{
		return cpuMs + presentMs > deadlineMs ? cpuMs + presentMs : cpuMs;
	}
}

struct AdaptiveSecond
{
	unsigned frames;
	unsigned over;		// frames that missed their deadline
	float p50Ms;
	float p95Ms;
	float meanMs;
};

class AdaptiveQualityModel
{
public:
	enum Decision { DECISION_NONE, DECISION_DOWN, DECISION_UP };
	enum Reason { REASON_NONE, REASON_OVER, REASON_P50, REASON_CALM };

	AdaptiveQualityModel() { reset(); }

	void reset()
	{
		m_level = 0;
		m_overStreak = 0;
		m_p50Streak = 0;
		m_calmSeconds = 0;
		m_cooldownUntil = -1e9;
		m_stepUpWait = AdaptiveQualityTuning::values().stepUpSeconds;
		m_lastStepUpAt = -1e9;
		m_lastStepWasUp = false;
		m_recentCount = 0;
		m_learnLeft = 0;
		m_learnLevel = 0;
		m_learnBefore = 0.0;
		m_learnSum = 0.0;
		m_reason = REASON_NONE;
		for (int i = 0; i < AdaptiveQualityLevels::MAX_LEVELS; ++i)
		{
			m_delta[i] = AdaptiveQualityLevels::config().levels[i].defaultSavingMs;
			m_deltaLearned[i] = false;
		}
	}

	// now: seconds on any steady clock
	Decision onSecond(const AdaptiveSecond& s, double now)
	{
		using namespace AdaptiveQualityTuning;

		pushRecent(s.meanMs);
		if (m_learnLeft > 0)
		{
			m_learnSum += s.meanMs;
			if (--m_learnLeft == 0)
			{
				const double after = m_learnSum / kLearnSeconds;
				m_delta[m_learnLevel] = std::min(std::max(m_learnBefore - after, 0.0), values().maxSavingMs);
				m_deltaLearned[m_learnLevel] = true;
			}
		}
		if (m_stepUpWait > values().stepUpSeconds && m_lastStepUpAt > -1e8 && now - m_lastStepUpAt > values().bounceSeconds)
		{
			m_stepUpWait = values().stepUpSeconds;	// the step up held
		}

		const double over = s.frames ? (double)s.over / (double)s.frames : 0.0;
		m_overStreak = over >= values().stepDownOverFraction ? m_overStreak + 1 : 0;
		m_p50Streak = s.p50Ms > values().stepDownP50Ms ? m_p50Streak + 1 : 0;
		// a step waits until the saving of the last one was measured, a median far above the budget does not wait;
		// it goes to the next level not learned to be useless, a useless current level never blocks it
		const bool savingKnown = m_learnLeft == 0;
		const int target = nextUsefulLevel();
		const bool wantDown = target > m_level && ((m_overStreak >= values().stepDownOverSeconds && savingKnown) || (m_p50Streak >= values().stepDownP50Seconds && (savingKnown || s.p50Ms > values().budgetMs)));
		// the saving of the level being restored, or a stricter limit while it was never learned
		const bool headroom = m_deltaLearned[m_level] ? s.p95Ms + delta() < values().stepUpP95Ms : s.p95Ms < values().stepUpUnlearnedP95Ms;
		const bool calm = over < values().stepUpOverFraction && headroom;
		m_calmSeconds = (calm && !wantDown && m_level > 0) ? m_calmSeconds + 1 : 0;

		if (now < m_cooldownUntil)
		{
			return DECISION_NONE;
		}

		if (wantDown)
		{
			m_reason = (m_overStreak >= values().stepDownOverSeconds && savingKnown) ? REASON_OVER : REASON_P50;
			m_learnBefore = recentMean();
			// the saving of skipped levels is learned as part of the target
			m_level = target;
			m_learnLevel = m_level;
			m_learnLeft = kLearnSeconds;
			m_learnSum = 0.0;
			if (now - m_lastStepUpAt <= values().bounceSeconds)
			{
				m_stepUpWait = std::min(m_stepUpWait * 2.0, values().stepUpMaxSeconds);
			}
			m_lastStepWasUp = false;
			afterStep(now);
			return DECISION_DOWN;
		}

		const double required = m_lastStepWasUp ? std::max(m_stepUpWait, values().stepUpAfterUpSeconds) : m_stepUpWait;
		if (m_level > 0 && m_calmSeconds >= required)
		{
			m_reason = REASON_CALM;
			--m_level;
			m_lastStepUpAt = now;
			m_learnLeft = 0;
			m_lastStepWasUp = true;
			afterStep(now);
			return DECISION_UP;
		}
		return DECISION_NONE;
	}

	int level() const { return m_level; }
	Reason reason() const { return m_reason; }
	double stepUpWait() const { return m_stepUpWait; }
	// saving of the current level, learned or the default
	double delta() const { return m_delta[m_level]; }
	double delta(int level) const { return m_delta[level]; }
	bool deltaLearned(int level) const { return m_deltaLearned[level]; }
	int calmSeconds() const { return m_calmSeconds; }
	// match seconds before the next step down or up is allowed by the cooldown
	double cooldownLeft(double now) const { return m_cooldownUntil > now ? m_cooldownUntil - now : 0.0; }
	// counted calm seconds a step up needs, and how many it has
	double stepUpRequired() const { return m_lastStepWasUp ? std::max(m_stepUpWait, AdaptiveQualityTuning::values().stepUpAfterUpSeconds) : m_stepUpWait; }
	// whether a step down still has a level that is not learned to be useless
	bool canStepDown() const { return nextUsefulLevel() > m_level; }

private:
	// the level a step down goes to, or the current level when every level below was learned to be useless
	int nextUsefulLevel() const
	{
		for (int l = m_level + 1; l < AdaptiveQualityLevels::levelCount(); ++l)
		{
			if (!m_deltaLearned[l] || m_delta[l] >= AdaptiveQualityTuning::values().minUsefulSavingMs)
			{
				return l;
			}
		}
		return m_level;
	}
	void afterStep(double now)
	{
		m_cooldownUntil = now + AdaptiveQualityTuning::values().cooldownSeconds;
		m_overStreak = 0;
		m_p50Streak = 0;
		m_calmSeconds = 0;
	}
	void pushRecent(float mean)
	{
		if (m_recentCount < AdaptiveQualityTuning::kLearnSeconds)
		{
			m_recent[m_recentCount++] = mean;
			return;
		}
		for (int i = 1; i < AdaptiveQualityTuning::kLearnSeconds; ++i)
		{
			m_recent[i - 1] = m_recent[i];
		}
		m_recent[AdaptiveQualityTuning::kLearnSeconds - 1] = mean;
	}
	double recentMean() const
	{
		double sum = 0.0;
		for (int i = 0; i < m_recentCount; ++i)
		{
			sum += m_recent[i];
		}
		return m_recentCount ? sum / m_recentCount : 0.0;
	}

	int m_level;
	int m_overStreak;
	int m_p50Streak;
	int m_calmSeconds;
	double m_cooldownUntil;
	double m_stepUpWait;
	double m_lastStepUpAt;
	bool m_lastStepWasUp;
	float m_recent[AdaptiveQualityTuning::kLearnSeconds];
	int m_recentCount;
	int m_learnLeft;
	int m_learnLevel;
	double m_learnBefore;
	double m_learnSum;
	Reason m_reason;
	double m_delta[AdaptiveQualityLevels::MAX_LEVELS];
	bool m_deltaLearned[AdaptiveQualityLevels::MAX_LEVELS];
};
