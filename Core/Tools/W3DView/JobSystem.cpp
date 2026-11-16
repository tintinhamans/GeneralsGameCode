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

#include "StdAfx.h"
#include "JobSystem.h"

#include "WWLib/thread.h"
#include "WWLib/mutex.h"

#include <deque>
#include <vector>
#include <thread>

#include <windows.h>

namespace W3DViewJobs
{

namespace
{
	// ------------------------------------------------------------------
	// Worker queue + workers
	// ------------------------------------------------------------------

	FastCriticalSectionClass g_QueueCS;
	std::deque<std::function<void()> > g_Queue;
	HANDLE g_WakeEvent = nullptr; // auto-reset; pulsed when a job is pushed
	bool g_Initialized = false;
	DWORD g_MainThreadID = 0;

	class WorkerThread : public ThreadClass
	{
	public:
		WorkerThread(const char* name) : ThreadClass(name, nullptr) {}

	protected:
		virtual void Thread_Function() override
		{
			while (running) {
				std::function<void()> job;
				bool got_job = false;
				{
					FastCriticalSectionClass::LockClass lock(g_QueueCS);
					if (!g_Queue.empty()) {
						job = std::move(g_Queue.front());
						g_Queue.pop_front();
						got_job = true;
					}
				}

				if (got_job) {
					try {
						job();
					} catch (...) {
						// Swallow worker exceptions; one bad job must not kill the pool.
					}
				} else {
					// No work - park on the event. 50 ms timeout keeps Stop() responsive.
					if (g_WakeEvent) {
						WaitForSingleObject(g_WakeEvent, 50);
					} else {
						ThreadClass::Sleep_Ms(10);
					}
				}
			}
		}
	};

	std::vector<WorkerThread*> g_Workers;

	// ------------------------------------------------------------------
	// Main-thread marshalling via a hidden message-only window
	// ------------------------------------------------------------------

	const UINT WM_W3DVIEW_RUN_MAIN_JOB = WM_USER + 0x4D31;
	const char* const MAIN_WND_CLASS = "W3DViewJobSystemMainWnd";

	HWND g_MainWnd = nullptr;
	FastCriticalSectionClass g_MainQueueCS;
	std::deque<std::function<void()> > g_MainQueue;

	void Drain_Main_Queue()
	{
		// Run jobs in batches; each pass takes whatever's queued so jobs
		// submitted while we drain get picked up on the next WM message.
		for (;;) {
			std::function<void()> job;
			{
				FastCriticalSectionClass::LockClass lock(g_MainQueueCS);
				if (g_MainQueue.empty()) return;
				job = std::move(g_MainQueue.front());
				g_MainQueue.pop_front();
			}
			try {
				job();
			} catch (...) {
				// Same policy as workers - never let a bad job take down the UI.
			}
		}
	}

	LRESULT CALLBACK Main_Wnd_Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_W3DVIEW_RUN_MAIN_JOB) {
			Drain_Main_Queue();
			return 0;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	void Create_Main_Window()
	{
		WNDCLASSEXA wc = { sizeof(wc) };
		wc.lpfnWndProc = Main_Wnd_Proc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = MAIN_WND_CLASS;
		// RegisterClassEx may fail if already registered; that's fine.
		RegisterClassExA(&wc);

		g_MainWnd = CreateWindowExA(
			0, MAIN_WND_CLASS, "", 0, 0, 0, 0, 0,
			HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
	}

	void Destroy_Main_Window()
	{
		if (g_MainWnd) {
			DestroyWindow(g_MainWnd);
			g_MainWnd = nullptr;
		}
		UnregisterClassA(MAIN_WND_CLASS, GetModuleHandle(nullptr));
	}

} // namespace

// ----------------------------------------------------------------------

void Init(int worker_count)
{
	if (g_Initialized) return;

	g_MainThreadID = GetCurrentThreadId();

	if (worker_count <= 0) {
		unsigned hw = std::thread::hardware_concurrency();
		if (hw == 0) hw = 2;
		worker_count = (hw > 1) ? static_cast<int>(hw) - 1 : 1;
	}
	if (worker_count < 1) worker_count = 1;
	if (worker_count > 8) worker_count = 8;

	g_WakeEvent = CreateEvent(nullptr, FALSE /*auto-reset*/, FALSE /*initial*/, nullptr);

	Create_Main_Window();

	g_Workers.reserve(worker_count);
	for (int i = 0; i < worker_count; ++i) {
		char name[32];
		_snprintf(name, sizeof(name), "W3DViewJob%d", i);
		name[sizeof(name) - 1] = '\0';
		WorkerThread* t = new WorkerThread(name);
		t->Execute();
		g_Workers.push_back(t);
	}

	g_Initialized = true;
}

void Shutdown()
{
	if (!g_Initialized) return;

	// Tell workers to stop. ThreadClass::Stop sets running=false and waits.
	// Pulse the wake event so any parked worker exits its wait promptly.
	for (WorkerThread* t : g_Workers) {
		if (g_WakeEvent) SetEvent(g_WakeEvent);
		t->Stop(3000);
		delete t;
	}
	g_Workers.clear();

	if (g_WakeEvent) {
		CloseHandle(g_WakeEvent);
		g_WakeEvent = nullptr;
	}

	// Drain any leftover main-thread jobs so resources captured in their
	// lambdas get released before we tear down the window.
	Drain_Main_Queue();
	Destroy_Main_Window();

	// Clear any worker-queue stragglers (jobs submitted but never picked up).
	{
		FastCriticalSectionClass::LockClass lock(g_QueueCS);
		g_Queue.clear();
	}

	g_Initialized = false;
	g_MainThreadID = 0;
}

bool Is_Initialized()
{
	return g_Initialized;
}

void Submit(std::function<void()> job)
{
	if (!job) return;
	if (!g_Initialized) {
		// Run inline so callers don't have to special-case startup/shutdown.
		job();
		return;
	}
	{
		FastCriticalSectionClass::LockClass lock(g_QueueCS);
		g_Queue.push_back(std::move(job));
	}
	if (g_WakeEvent) SetEvent(g_WakeEvent);
}

void Submit_To_Main(std::function<void()> job)
{
	if (!job) return;
	if (!g_Initialized || !g_MainWnd) {
		// If we're on the main thread we can just run it; otherwise we have
		// nowhere to post it. The latter only happens during shutdown.
		if (GetCurrentThreadId() == g_MainThreadID || g_MainThreadID == 0) {
			job();
		}
		return;
	}
	{
		FastCriticalSectionClass::LockClass lock(g_MainQueueCS);
		g_MainQueue.push_back(std::move(job));
	}
	PostMessage(g_MainWnd, WM_W3DVIEW_RUN_MAIN_JOB, 0, 0);
}

} // namespace W3DViewJobs
