#pragma once

#include <optional>
#include <string>

// The ban of a ModerationNotice, a refused session registration or a refused realtime upgrade.
struct OnlineBanDetails
{
	std::string reason;
	// Absent for a permanent ban.
	std::optional<std::string> expiresAt;
	// False when the service sent no ban body, then the dialog does not say how long the ban lasts.
	bool bKnown = true;
};

void ShowLoginBanDialog(const OnlineBanDetails& ban);
void ShowChatRateLimitNotice(const std::string& reason, const std::string& scopeType);
// Ends the online session for a ban and shows the ban dialog.
void HandleModerationBan(const OnlineBanDetails& ban);
