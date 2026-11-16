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

// TheSuperHackers @feature W3DView background job system.
//
// Small fixed-size thread pool scoped to the W3DView tool. Long-running
// operations (asset directory scans, .w3d parsing, animation-report
// matrix fill) submit jobs that run on worker threads; results that touch
// MFC controls or the WW3D asset manager are marshalled back to the UI
// thread via SubmitToMain, which posts to a hidden message-only window.
//
// Built on the existing WWLib threading primitives (ThreadClass,
// FastCriticalSectionClass) - no std::thread / std::mutex.

#pragma once

#include <functional>

namespace W3DViewJobs
{
	// Initialize the worker pool. Safe to call once. worker_count == 0 means
	// auto-detect (hardware_concurrency - 1, clamped to [1, 8]).
	void Init(int worker_count = 0);

	// Stop workers, drain pending main-thread jobs, destroy the hidden window.
	// Safe to call without a prior Init (no-op).
	void Shutdown();

	// True between successful Init and Shutdown.
	bool Is_Initialized();

	// Push a job onto the worker queue. If the system isn't initialized,
	// the job runs synchronously on the calling thread (so callers don't
	// need to special-case startup/shutdown).
	void Submit(std::function<void()> job);

	// Marshal a job back to the MFC main thread. Returns immediately; the
	// job runs the next time the main thread pumps messages. If called
	// from the main thread before Init or after Shutdown, the job runs
	// inline. Workers may safely call this from any thread.
	void Submit_To_Main(std::function<void()> job);
}
