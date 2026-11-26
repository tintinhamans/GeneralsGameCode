#pragma once
// Lowers client only effects step by step when this machine cannot keep up, and gives them back when it can.

#include "Common/AdaptiveQualityModel.h"
#include "Common/AdaptiveQualityPower.h"

struct AdaptiveQualityOverlay
{
	enum { LEVELS = AdaptiveQualityLevels::MAX_LEVELS };

	Int level;
	Int levelCount;				// levels in use, including level 0
	Int lastFrom;				// the last step, -1 when there was none this match
	Int lastTo;
	Int lastReason;				// AdaptiveQualityModel::Reason
	float lastAgoSec;
	float p50Ms;				// own frame cost over the last counted second
	float p95Ms;
	float cpuMs;
	float presentMs;
	float stepDownP50Ms;		// the median limit that steps down
	float stepUpP95Ms;			// the p95 limit that allows a step up
	float deadlineMs;
	Int deadlineKind;			// AdaptiveQualityTuning::DeadlineKind
	float refreshHz;
	float savingMs[LEVELS];		// frame cost saved by each level, learned on this machine or the default
	Bool savingLearned[LEVELS];
	float downInSec;			// seconds until a step down is allowed, -1 when no level is left to lower
	float upInSec;				// seconds until a step up is allowed, -1 at level 0
	Int catchUpFrames;			// logic frames the current level lets the game fall behind and make up in one drawn frame
	Int catchUpPerSec;			// extra logic frames run in the last second
	float catchUpMs;			// logic time those frames took in the last second
};

class AdaptiveQuality
{
public:
	static AdaptiveQuality& get();

	void frameBegin();
	void frameEnd();
	// own cost: game logic, client update and client step in QPC ticks
	void addOwn(long long ticks) { m_ownTicks += ticks; }
	// extra logic frames the catch-up loop ran, kept out of the frame cost so catching up never lowers the quality by itself
	void addCatchUp(long long ticks, Int frames) { m_catchUpTicks += ticks; m_catchUpFrames += frames; }
	// logic frames the game may fall behind before the lost time is dropped, and the most one drawn frame runs to make them up
	// it is the full quality value with dynamic details off
	Int catchUpFrames() const;
	// logic time after which no further catch-up frame starts in this drawn frame
	double catchUpBudgetMs() const;
	// time blocked in Present: counts only when the frame missed its deadline, since a met frame was waiting on vsync
	void addPresentWait(long long ticks) { m_presentTicks += ticks; }
	// W3DDisplay::draw: whether the device is windowed and whether fullscreen Present waits for the display
	void notePresentMode(Bool windowed, Bool intervalSynced) { m_windowed = windowed; m_intervalSynced = intervalSynced; }
	// this iteration waited on the other players, so it says nothing about the cost on this machine
	void noteNetworkWait() { m_networkWait = TRUE; }
	static long long now();

	// W3DDisplay::draw, once per frame outside frozen time: true when this frame is not rendered
	Bool shouldSkipRender();

	// the network overlay, false when there is no match to show
	Bool getOverlay(AdaptiveQualityOverlay& out) const;

private:
	AdaptiveQuality();

	void startMatch();
	void endMatch();
	void recordFrame(double ms, double deadlineMs);
	void checkDisplay();
	void closeSecond(long long t);
	void setLevel(Int from, Int level, const AdaptiveSecond& s);
	void showMessage(const UnicodeString& text);
	void checkPower();

	enum { BINS = 2000 };

	AdaptiveQualityModel m_model;
	double m_matchSeconds;
	float m_lastP50Ms;
	float m_lastP95Ms;
	Int m_deadlineKind;
	Int m_lastFrom;
	Int m_lastTo;
	Int m_lastReason;
	double m_lastStepAt;
	long long m_freq;
	long long m_ownTicks;
	long long m_presentTicks;
	long long m_catchUpTicks;
	Int m_catchUpFrames;
	Int m_lastCatchUpFrames;
	double m_lastCatchUpMs;
	long long m_secondStart;
	long long m_matchStart;
	Bool m_networkWait;
	Bool m_inMatch;
	Bool m_renderSkipPhase;
	Bool m_pairPending;
	double m_pairMs;
	double m_pairDeadlineMs;
	double m_cpuSumMs;
	double m_presentSumMs;
	unsigned m_splitFrames;
	double m_lastCpuMs;
	double m_lastPresentMs;
	double m_lastDeadlineMs;
	double m_refreshMs;
	Bool m_windowed;
	Bool m_intervalSynced;
	Bool m_presentSynced;
	unsigned m_hist[BINS];
	unsigned m_frames;
	unsigned m_over;
	unsigned m_waitFrames;
	unsigned m_iterations;
	double m_sumMs;
	Bool m_minimized;
	Bool m_paused;
	double m_lastMeanMs;
	Int m_stepDowns;
	Int m_stepUps;
	Bool m_powerChecked;
	AdaptiveQualityPower::State m_power;
	char m_planName[24];
};
