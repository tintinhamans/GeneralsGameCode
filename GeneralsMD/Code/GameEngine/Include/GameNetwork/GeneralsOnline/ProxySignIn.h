#pragma once

#include <functional>
#include <string>
#include "GameNetwork/GeneralsOnline/ServiceEndpointPolicy.h"

namespace ServiceConnection
{
bool IsProxySignInOpen();
// Worker threads hand work back to the main thread here; it runs once per frame from the game engine update
void RunOnMainThread(std::function<void()> task);
void PumpMainThreadTasks();

// Main thread only. One dialog does both jobs: it asks for the sign-in of a proxy that answered 407 and checks it in place.
// Login path: done(true) once the proxy accepted the sign-in (or the Windows sign-in was chosen), done(false) on cancel.
void RequestProxySignIn(const std::string& proxyURL, std::function<void(bool)> done);
// Accept in Options with changed connection settings: tests them first.
// Save: they passed, or the player chose to save anyway. KeepEditing: back to the options. Discard: leave the connection settings as they were.
enum class AcceptChoice { Save, KeepEditing, Discard };
void ShowAcceptTest(const ServiceConnectionSettings& settings, std::function<void(AcceptChoice)> decided);
}
