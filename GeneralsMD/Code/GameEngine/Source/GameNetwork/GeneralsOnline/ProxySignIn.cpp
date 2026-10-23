#include "PreRTS.h"

#include "GameNetwork/GeneralsOnline/ProxySignIn.h"
#include "GameNetwork/GeneralsOnline/ServiceConnectionPolicy.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/KeyDefs.h"
#include <algorithm>
#include <deque>
#include <mutex>
#include <thread>

// One dialog for the sign-in of a proxy and the connection test. It reuses the retail GameSpy login layout, which Generals Online
// does not use otherwise, resized into the frame of the game's message box. States: testing, form, checking, result.
// Fonts and looks come from the retail controls of that layout; distances are one padding and one row gap in 800x600 units.
// The Windows sign-in is tried silently before this form is ever shown, so the form is only for typed credentials.
namespace ServiceConnection
{
namespace
{
std::mutex mainThreadMutex;
std::deque<std::function<void()>> mainThreadTasks;

enum class State { Testing, Form, Checking, Result };

struct Dialog
{
	WindowLayout *layout = nullptr;
	GameWindow *parent = nullptr;
	GameWindow *panel = nullptr;
	GameWindow *inner = nullptr;
	GameWindow *header = nullptr;
	GameWindow *title = nullptr;
	GameWindow *info = nullptr;
	GameWindow *error = nullptr;
	GameWindow *userLabel = nullptr;
	GameWindow *userName = nullptr;
	GameWindow *passwordLabel = nullptr;
	GameWindow *password = nullptr;
	GameWindow *remember = nullptr;
	GameWindow *ok = nullptr;
	GameWindow *cancel = nullptr;
	GameWindow *middle = nullptr;
	GameWindow *previousFocus = nullptr;
	State state = State::Testing;
	bool testPath = false;
	bool acceptMode = false;
	bool failed = false;
	unsigned generation = 0;
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	std::string proxyURL;
	ServiceConnectionSettings settings;
	std::string pendingUser;
	std::string pendingPassword;
	bool pendingRemember = false;
	std::function<void(bool)> done;
	std::function<void(AcceptChoice)> decided;
};

Dialog dialog;

const char *environmentName()
{
	return NGMP_OnlineServicesManager::g_Environment == NGMP_OnlineServicesManager::EEnvironment::DEV ? "dev" :
		NGMP_OnlineServicesManager::g_Environment == NGMP_OnlineServicesManager::EEnvironment::TEST ? "test" : "prod";
}

GameWindow *control(const char *name)
{
	return TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(name));
}

void hideRetail(const char *name)
{
	GameWindow *window = control((std::string("GameSpyLoginProfile.wnd:") + name).c_str());
	if (window != nullptr)
	{
		window->winHide(TRUE);
	}
}

UnicodeString wideText(const std::string &text)
{
	UnicodeString result;
	result.translate(text.c_str());
	return result;
}

std::string narrowText(const UnicodeString &text)
{
	AsciiString ascii;
	ascii.translate(text);
	return ascii.str();
}

void wipeString(std::string &value)
{
	if (!value.empty())
	{
		SecureZeroMemory(value.data(), value.size());
	}
	value.clear();
}

void copyLook(GameWindow *to, GameWindow *from)
{
	WinInstanceData *target = to->winGetInstanceData();
	WinInstanceData *source = from->winGetInstanceData();
	for (Int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		target->m_enabledDrawData[i] = source->m_enabledDrawData[i];
		target->m_disabledDrawData[i] = source->m_disabledDrawData[i];
		target->m_hiliteDrawData[i] = source->m_hiliteDrawData[i];
	}
	target->m_enabledText = source->m_enabledText;
	target->m_disabledText = source->m_disabledText;
	target->m_hiliteText = source->m_hiliteText;
	target->m_imeCompositeText = source->m_imeCompositeText;
	target->m_imageOffset = source->m_imageOffset;
	to->winSetFont(from->winGetFont());
}

GameWindow *createEntry(GameWindow *like, const char *name)
{
	WinInstanceData instance;
	instance.m_style = like->winGetStyle();
	EntryData data;
	memset(&data, 0, sizeof(data));
	data.aSCIIOnly = TRUE;
	data.maxTextLen = 64;
	GameWindow *entry = TheWindowManager->gogoGadgetTextEntry(like->winGetParent(), like->winGetStatus(), 0, 0, 10, 10, &instance, &data, like->winGetFont(), FALSE);
	if (entry != nullptr)
	{
		copyLook(entry, like);
		entry->winSetWindowId(TheNameKeyGenerator->nameToKey(name));
	}
	return entry;
}

GameWindow *createButton(GameWindow *like, const char *name)
{
	WinInstanceData instance;
	instance.m_style = like->winGetStyle();
	GameWindow *button = TheWindowManager->gogoGadgetPushButton(like->winGetParent(), like->winGetStatus(), 0, 0, 10, 10, &instance, like->winGetFont(), FALSE);
	if (button != nullptr)
	{
		copyLook(button, like);
		button->winSetWindowId(TheNameKeyGenerator->nameToKey(name));
	}
	return button;
}

GameWindow *createLabel(GameWindow *like, const char *name)
{
	WinInstanceData instance;
	instance.m_style = like->winGetStyle();
	TextData data;
	memset(&data, 0, sizeof(data));
	GameWindow *label = TheWindowManager->gogoGadgetStaticText(like->winGetParent(), like->winGetStatus(), 0, 0, 10, 10, &instance, &data, like->winGetFont(), FALSE);
	if (label != nullptr)
	{
		copyLook(label, like);
		label->winSetWindowId(TheNameKeyGenerator->nameToKey(name));
	}
	return label;
}

void place(GameWindow *window, Int x, Int y, Int width, Int height)
{
	window->winSetPosition(x, y);
	window->winSetSize(width, height);
}

// the panel and the dimmed screen draw as a flat color with a border; the inner layout windows draw nothing
void flatLook(GameWindow *window, bool visible, Color color, Color border)
{
	WinInstanceData *data = window->winGetInstanceData();
	for (Int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		data->m_enabledDrawData[i].image = nullptr;
		data->m_enabledDrawData[i].color = i == 0 && visible ? color : WIN_COLOR_UNDEFINED;
		data->m_enabledDrawData[i].borderColor = i == 0 && visible ? border : WIN_COLOR_UNDEFINED;
		data->m_disabledDrawData[i] = data->m_enabledDrawData[i];
		data->m_hiliteDrawData[i] = data->m_enabledDrawData[i];
	}
	window->winClearStatus(WIN_STATUS_IMAGE | WIN_STATUS_BORDER);
}

void setCenteredVertically(GameWindow *label, bool centered)
{
	if (TextData *text = static_cast<TextData *>(label->winGetUserData()))
	{
		text->centeredVertically = centered;
		text->leftMargin = 0;
		text->topMargin = 0;
	}
}

// ---- layout ----------------------------------------------------------------------------------------------------------------------

// Same frame as the game's message box (Menus/MessageBox.wnd): fill 2 2 2, border 47 55 168, a header, a dimmed screen behind it,
// 100 from the top at 800x600. All distances come from the pixel height and width of the retail fonts of the login layout
// (already scaled for the resolution), never below the message box's own size. The top left corner stays put across states.
struct Metrics
{
	Int fontHeight = 0;
	Int padding = 0;
	Int gap = 0;
	Int rowHeight = 0;
	Int lineHeight = 0;
	Int checkHeight = 0;
	Int headerHeight = 0;
	Int labelWidth = 0;
	Int buttonWidth = 0;
	Int buttonHeight = 0;
	Int edge = 0;
	Int bottomMargin = 0;
	Int panelWidth = 0;
	Int panelX = 0;
	Int panelY = 0;
};

Metrics metrics;

Int px(Int value)
{
	return (Int)(value * dialog.scaleX + 0.5f);
}

Int py(Int value)
{
	return (Int)(value * dialog.scaleY + 0.5f);
}

Int measure(GameFont *font, const std::string &text)
{
	DisplayString *string = TheDisplayStringManager->newDisplayString();
	string->setFont(font);
	string->setText(wideText(text));
	const Int width = string->getWidth();
	TheDisplayStringManager->freeDisplayString(string);
	return width;
}

void computeMetrics(Int screenWidth, Int screenHeight, const std::string &formMessage)
{
	GameFont *textFont = dialog.password->winGetFont();
	Metrics m;
	m.fontHeight = TheWindowManager->winFontHeight(textFont);
	const Int titleHeight = TheWindowManager->winFontHeight(dialog.title->winGetFont());
	m.padding = std::max<Int>(px(14), m.fontHeight);
	m.gap = std::max<Int>(py(6), m.fontHeight * 6 / 10);
	m.rowHeight = std::max<Int>(py(24), m.fontHeight * 18 / 10);
	m.lineHeight = std::max<Int>(py(16), m.fontHeight * 13 / 10);
	m.checkHeight = std::max<Int>(py(22), m.fontHeight * 16 / 10);
	m.headerHeight = std::max<Int>(py(50), titleHeight * 2);
	m.labelWidth = std::max(measure(textFont, "Username"), measure(textFont, "Password")) + m.fontHeight;
	// the buttons keep the size and the margins of the message box (90x24, 8 from the edges, 27 from the bottom) and are never stretched
	m.buttonWidth = px(90);
	m.buttonHeight = py(24);
	m.edge = px(8);
	m.bottomMargin = py(27);
	const Int widest = std::max({ measure(textFont, formMessage), measure(textFont, "The proxy didn't accept this sign-in."), measure(textFont, "No answer from the proxy.") });
	m.panelWidth = std::max({ px(300), widest * 118 / 100 + 2 * m.padding, 2 * m.padding + m.labelWidth + px(150) });
	m.panelWidth = std::min<Int>(m.panelWidth, screenWidth * 62 / 100);
	m.panelX = (screenWidth - m.panelWidth) / 2;
	m.panelY = py(100) + std::max<Int>(0, (screenHeight - py(600)) / 2);
	metrics = m;
}

// lines a text takes in the message area, counting wrapped and explicit lines
Int linesFor(const std::string &text)
{
	GameFont *font = dialog.password->winGetFont();
	const Int available = metrics.panelWidth - 2 * metrics.padding;
	Int lines = 0;
	size_t start = 0;
	for (;;)
	{
		const size_t end = text.find('\n', start);
		const std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		lines += std::max<Int>(1, (measure(font, part) + available - 1) / available);
		if (end == std::string::npos)
		{
			break;
		}
		start = end + 1;
	}
	return lines;
}

void showControls(std::initializer_list<GameWindow *> windows, bool show)
{
	for (GameWindow *window : windows)
	{
		window->winHide(!show);
	}
}

void updateFormEnabled()
{
	const bool editable = dialog.state == State::Form;
	dialog.userName->winEnable(editable);
	dialog.password->winEnable(editable);
	dialog.remember->winEnable(editable);
	dialog.ok->winEnable(editable);
	dialog.cancel->winEnable(TRUE);
}

// a label that fits the fixed button size, otherwise the short one
std::string fittingLabel(GameWindow *button, const std::string &label, const std::string &shortLabel)
{
	return measure(button->winGetFont(), label) + metrics.fontHeight <= metrics.buttonWidth ? label : shortLabel;
}

void setState(State state, const std::string &title, const std::string &message, const std::string &errorText)
{
	dialog.state = state;
	const Metrics &m = metrics;
	const bool form = state == State::Form || state == State::Checking;
	const Int messageY = m.headerHeight + m.padding;
	const Int messageHeight = form ? m.lineHeight : linesFor(message) * m.lineHeight;
	const Int errorY = messageY + m.lineHeight;
	const Int userY = errorY + m.lineHeight + m.gap;
	const Int passwordY = userY + m.rowHeight + m.gap;
	const Int rememberY = passwordY + m.rowHeight + m.gap;
	const Int buttonY = form ? rememberY + m.checkHeight + m.gap * 2 : messageY + messageHeight + m.gap * 2;
	const Int panelHeight = buttonY + m.buttonHeight + m.bottomMargin;
	const Int fieldX = m.padding + m.labelWidth;
	const Int fieldWidth = m.panelWidth - fieldX - m.padding;
	const Int contentWidth = m.panelWidth - 2 * m.padding;

	place(dialog.panel, m.panelX, m.panelY, m.panelWidth, panelHeight);
	place(dialog.inner, 0, 0, m.panelWidth, panelHeight);
	place(dialog.header, 0, 0, m.panelWidth, m.headerHeight);
	place(dialog.title, m.padding, 0, contentWidth, m.headerHeight);
	place(dialog.info, m.padding, messageY, contentWidth, messageHeight);
	setCenteredVertically(dialog.info, form);
	place(dialog.error, m.padding, errorY, contentWidth, m.lineHeight);
	place(dialog.userLabel, m.padding, userY, m.labelWidth, m.rowHeight);
	place(dialog.userName, fieldX, userY, fieldWidth, m.rowHeight);
	place(dialog.passwordLabel, m.padding, passwordY, m.labelWidth, m.rowHeight);
	place(dialog.password, fieldX, passwordY, fieldWidth, m.rowHeight);
	place(dialog.remember, fieldX, rememberY, fieldWidth, m.checkHeight);
	// button slots of the message box: primary bottom left, optional middle, cancel bottom right
	place(dialog.ok, m.edge, buttonY, m.buttonWidth, m.buttonHeight);
	place(dialog.middle, (m.panelWidth - m.buttonWidth) / 2, buttonY, m.buttonWidth, m.buttonHeight);
	place(dialog.cancel, m.panelWidth - m.edge - m.buttonWidth, buttonY, m.buttonWidth, m.buttonHeight);
	const bool failedAccept = state == State::Result && dialog.acceptMode && dialog.failed;
	GadgetButtonSetText(dialog.ok, wideText(failedAccept ? fittingLabel(dialog.ok, "Keep editing", "Edit") : "OK"));
	GadgetButtonSetText(dialog.middle, wideText(fittingLabel(dialog.middle, "Save anyway", "Save")));

	GadgetStaticTextSetText(dialog.title, wideText(title));
	GadgetStaticTextSetText(dialog.info, wideText(message));
	GadgetStaticTextSetText(dialog.error, wideText(errorText));
	showControls({ dialog.error, dialog.userLabel, dialog.userName, dialog.passwordLabel, dialog.password, dialog.remember }, form);
	dialog.ok->winHide(state == State::Testing || state == State::Checking);
	dialog.middle->winHide(!failedAccept);
	dialog.cancel->winHide(state == State::Result && !failedAccept);
	updateFormEnabled();
}

// ---- results ---------------------------------------------------------------------------------------------------------------------

void finishDialog(bool signedIn, AcceptChoice choice = AcceptChoice::KeepEditing)
{
	std::function<void(bool)> done = std::move(dialog.done);
	std::function<void(AcceptChoice)> decided = std::move(dialog.decided);
	GameWindow *previousFocus = dialog.previousFocus;
	TheWindowManager->winUnsetModal(dialog.parent);
	GadgetTextEntrySetText(dialog.password, UnicodeString::TheEmptyString);
	wipeString(dialog.pendingPassword);
	dialog.layout->destroyWindows();
	deleteInstance(dialog.layout);
	const unsigned generation = dialog.generation + 1;
	dialog = Dialog();
	dialog.generation = generation;
	if (previousFocus != nullptr)
	{
		TheWindowManager->winSetFocus(previousFocus);
	}
	if (done)
	{
		done(signedIn);
	}
	if (decided)
	{
		decided(choice);
	}
}

// the proxy accepted the sign-in: keep it as the player chose
void commitSignIn()
{
	std::string error;
	if (dialog.pendingRemember)
	{
		ClearSessionProxyCredentials(dialog.proxyURL);
		if (!SaveProxyCredentials(dialog.proxyURL, dialog.pendingUser, dialog.pendingPassword, error))
		{
			NetworkLog(ELogVerbosity::LOG_RELEASE, "[Connection] Unable to save the proxy sign-in: %s", error.c_str());
			SetSessionProxyCredentials(dialog.proxyURL, dialog.pendingUser, dialog.pendingPassword);
		}
	}
	wipeString(dialog.pendingPassword);
}

void showForm(const std::string &errorText, bool clearPassword)
{
	setState(State::Form, "Proxy sign-in", "The proxy " + ProxyDisplayName(dialog.proxyURL) + " needs a sign-in.", errorText);
	if (clearPassword)
	{
		GadgetTextEntrySetText(dialog.password, UnicodeString::TheEmptyString);
	}
	TheWindowManager->winSetFocus(narrowText(GadgetTextEntryGetText(dialog.userName)).empty() ? dialog.userName : dialog.password);
}

void startProbe(ServiceConnectionSettings settings)
{
	const unsigned generation = ++dialog.generation;
	const std::string environment = environmentName();
	std::thread([generation, settings, environment]()
	{
		ConnectionTestResult result;
		try
		{
			result = TestServiceConnection(settings, environment, 12);
		}
		catch (...)
		{
		}
		RunOnMainThread([generation, result]()
		{
			if (dialog.layout == nullptr || dialog.generation != generation)
			{
				return;
			}
			using Outcome = ConnectionTestResult::Outcome;
			if (dialog.state == State::Testing)
			{
				if (result.outcome == Outcome::ProxyAuthRequired && !result.proxyURL.empty())
				{
					dialog.proxyURL = result.proxyURL;
					const std::string savedUser = ProxyUsername(result.proxyURL);
					GadgetTextEntrySetText(dialog.userName, wideText(savedUser));
					GadgetCheckBoxSetChecked(dialog.remember, HasProxyCredentials(result.proxyURL));
					showForm(result.detail == "refused" ? "The proxy didn't accept this sign-in." : "", false);
				}
				else if (dialog.acceptMode && result.outcome == Outcome::Connected)
				{
					finishDialog(true, AcceptChoice::Save);
				}
				else
				{
					dialog.failed = result.outcome != Outcome::Connected;
					setState(State::Result, "Connection test", DescribeConnectionTest(result), "");
				}
				return;
			}
			// checking a sign-in
			if (result.outcome == Outcome::ProxyAuthRequired)
			{
				ClearSessionProxyCredentials(dialog.proxyURL);
				wipeString(dialog.pendingPassword);
				showForm("The proxy didn't accept this sign-in.", true);
				return;
			}
			if (result.outcome == Outcome::Unreachable && !result.proxyAnswered)
			{
				ClearSessionProxyCredentials(dialog.proxyURL);
				wipeString(dialog.pendingPassword);
				showForm("No answer from the proxy.", false);
				return;
			}
			commitSignIn();
			if (!dialog.testPath)
			{
				finishDialog(true);
				return;
			}
			if (dialog.acceptMode && result.outcome == Outcome::Connected)
			{
				finishDialog(true, AcceptChoice::Save);
				return;
			}
			dialog.failed = result.outcome != Outcome::Connected;
			setState(State::Result, "Connection test",
				result.outcome == Outcome::Connected ? DescribeConnectionTest(result) :
				result.outcome == Outcome::Unreachable ? "Signed in to the proxy, but could not reach the service." :
				"Signed in to the proxy. " + DescribeConnectionTest(result), "");
		});
	}).detach();
}

// switches to the checking state; stays on the form when the input is incomplete
void acceptForm()
{
	std::string user = narrowText(GadgetTextEntryGetText(dialog.userName));
	std::string password = narrowText(GadgetTextEntryGetText(dialog.password));
	if (user.empty())
	{
		showForm("Enter the user name.", false);
		wipeString(password);
		return;
	}
	dialog.pendingUser = user;
	dialog.pendingPassword = password;
	dialog.pendingRemember = GadgetCheckBoxIsChecked(dialog.remember) != FALSE;
	wipeString(password);
	GadgetTextEntrySetText(dialog.password, UnicodeString::TheEmptyString);

	// the sign-in is used for the check first; it is kept once the proxy accepted it
	SetSessionProxyCredentials(dialog.proxyURL, dialog.pendingUser, dialog.pendingPassword);
	setState(State::Checking, "Proxy sign-in", "Checking...", "");
	startProbe(dialog.testPath ? dialog.settings : NGMP_OnlineServicesManager::Settings.Network_GetConnectionSettings());
}

void cancelDialog()
{
	if (dialog.state == State::Checking)
	{
		ClearSessionProxyCredentials(dialog.proxyURL);
	}
	// Cancel after a failed check drops the connection changes, earlier it only stops the check
	finishDialog(false, dialog.acceptMode && dialog.state == State::Result ? AcceptChoice::Discard : AcceptChoice::KeepEditing);
}

WindowMsgHandledType dialogSystem(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	if (dialog.layout == nullptr)
	{
		return MSG_IGNORED;
	}
	switch (msg)
	{
		case GWM_INPUT_FOCUS:
		{
			if (mData1 == TRUE)
			{
				*(Bool *)mData2 = TRUE;
			}
			return MSG_HANDLED;
		}
		case GBM_SELECTED:
		{
			GameWindow *source = (GameWindow *)mData1;
			if (source == dialog.ok)
			{
				if (dialog.state == State::Form)
				{
					acceptForm();
				}
				else if (dialog.state == State::Result)
				{
					// with a failed check before Accept this button is "Keep editing"
					finishDialog(!dialog.acceptMode, AcceptChoice::KeepEditing);
				}
			}
			else if (source == dialog.middle)
			{
				finishDialog(true, AcceptChoice::Save);
			}
			else if (source == dialog.cancel)
			{
				cancelDialog();
			}
			return MSG_HANDLED;
		}
		case GEM_EDIT_DONE:
		{
			GameWindow *source = (GameWindow *)mData1;
			if (dialog.state != State::Form)
			{
				return MSG_HANDLED;
			}
			if (source == dialog.userName)
			{
				TheWindowManager->winSetFocus(dialog.password);
			}
			else if (source == dialog.password)
			{
				acceptForm();
			}
			return MSG_HANDLED;
		}
	}
	return MSG_IGNORED;
}

WindowMsgHandledType dialogInput(GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2)
{
	if (dialog.layout == nullptr)
	{
		return MSG_IGNORED;
	}
	if (msg == GWM_CHAR)
	{
		if ((UnsignedByte)mData1 == KEY_ESC)
		{
			if (BitIsSet((UnsignedByte)mData2, KEY_STATE_UP))
			{
				cancelDialog();
			}
			return MSG_HANDLED;
		}
	}
	return MSG_IGNORED;
}

// builds the dialog; returns false when the layout could not be built
bool openDialog(bool testPath, const std::string &formMessage)
{
	if (dialog.layout != nullptr || TheWindowManager == nullptr)
	{
		return false;
	}
	WindowLayout *layout = TheWindowManager->winCreateLayout(AsciiString("Menus/GameSpyLoginProfile.wnd"));
	Dialog d;
	d.generation = dialog.generation;
	d.layout = layout;
	d.parent = control("GameSpyLoginProfile.wnd:WOLLoginMenuParent");
	d.panel = control("GameSpyLoginProfile.wnd:GadgetParent");
	d.header = control("GameSpyLoginProfile.wnd:Line");
	d.userLabel = control("GameSpyLoginProfile.wnd:StaticTextLoginName");
	d.info = control("GameSpyLoginProfile.wnd:StaticTextEmail");
	d.title = control("GameSpyLoginProfile.wnd:StaticTextTitle");
	d.passwordLabel = control("GameSpyLoginProfile.wnd:StaticTextPassword");
	d.password = control("GameSpyLoginProfile.wnd:TextEntryPassword");
	d.remember = control("GameSpyLoginProfile.wnd:CheckBoxRememberInfo");
	d.ok = control("GameSpyLoginProfile.wnd:ButtonLogin");
	d.cancel = control("GameSpyLoginProfile.wnd:ButtonBack");
	d.inner = d.password != nullptr ? d.password->winGetParent() : nullptr;
	if (layout == nullptr || d.parent == nullptr || d.panel == nullptr || d.header == nullptr || d.inner == nullptr || d.userLabel == nullptr || d.info == nullptr
		|| d.title == nullptr || d.passwordLabel == nullptr || d.password == nullptr || d.remember == nullptr || d.ok == nullptr || d.cancel == nullptr)
	{
		if (layout != nullptr)
		{
			layout->destroyWindows();
			deleteInstance(layout);
		}
		return false;
	}
	for (const char *name : { "ParentESRB", "ComboBoxLoginName", "ComboBoxEmail", "WinAge", "ButtonCreateAccount", "ButtonTOS", "ParentTOS" })
	{
		hideRetail(name);
	}
	d.userName = createEntry(d.password, "GameSpyLoginProfile.wnd:TextEntryProxyUser");
	d.error = createLabel(d.info, "GameSpyLoginProfile.wnd:StaticTextProxyError");
	d.middle = createButton(d.ok, "GameSpyLoginProfile.wnd:ButtonProxyMiddle");
	if (d.userName == nullptr || d.error == nullptr || d.middle == nullptr)
	{
		layout->destroyWindows();
		deleteInstance(layout);
		return false;
	}
	d.testPath = testPath;
	d.previousFocus = TheWindowManager->winGetFocus();
	dialog = std::move(d);

	if (EntryData *entry = static_cast<EntryData *>(dialog.password->winGetUserData()))
	{
		entry->secretText = TRUE;
		entry->aSCIIOnly = TRUE;
		entry->maxTextLen = 64;
	}
	Int screenWidth = 0;
	Int screenHeight = 0;
	dialog.parent->winGetSize(&screenWidth, &screenHeight);
	// the scale of the layout comes from a retail control (the login button is 180x32 at 800x600), so it follows however the layout was scaled
	Int buttonWidthNow = 0;
	Int buttonHeightNow = 0;
	dialog.ok->winGetSize(&buttonWidthNow, &buttonHeightNow);
	dialog.scaleX = buttonWidthNow > 0 ? buttonWidthNow / 180.0f : screenWidth / 800.0f;
	dialog.scaleY = buttonHeightNow > 0 ? buttonHeightNow / 32.0f : screenHeight / 600.0f;

	flatLook(dialog.parent, true, GameMakeColor(2, 2, 2, 115), WIN_COLOR_UNDEFINED);
	dialog.parent->winClearStatus(WIN_STATUS_NO_INPUT | WIN_STATUS_BORDER);
	const Color border = GameMakeColor(47, 55, 168, 255);
	flatLook(dialog.panel, true, GameMakeColor(2, 2, 2, 255), border);
	flatLook(dialog.inner, false, 0, 0);
	flatLook(dialog.header, true, GameMakeColor(0, 0, 0, 0), border);
	dialog.header->winHide(FALSE);
	dialog.error->winSetEnabledTextColors(GameMakeColor(255, 96, 96, 255), GameMakeColor(0, 0, 0, 255));
	// labels use the font of the entry fields, the title keeps the retail title font
	for (GameWindow *label : { dialog.userLabel, dialog.passwordLabel, dialog.error, dialog.info })
	{
		label->winSetFont(dialog.password->winGetFont());
	}
	for (GameWindow *label : { dialog.userLabel, dialog.passwordLabel, dialog.error, dialog.title })
	{
		setCenteredVertically(label, true);
	}
	computeMetrics(screenWidth, screenHeight, formMessage);
	GadgetStaticTextSetText(dialog.userLabel, UnicodeString(L"Username"));
	GadgetStaticTextSetText(dialog.passwordLabel, UnicodeString(L"Password"));
	GadgetCheckBoxSetText(dialog.remember, UnicodeString(L"Remember"));
	GadgetButtonSetText(dialog.ok, UnicodeString(L"OK"));
	GadgetButtonSetText(dialog.cancel, UnicodeString(L"Cancel"));
	dialog.middle->winHide(TRUE);
	dialog.userName->winSetTooltip(UnicodeString(L"The proxy user name, often DOMAIN\\name."));
	dialog.password->winSetTooltip(UnicodeString(L"The proxy password."));
	dialog.remember->winSetTooltip(UnicodeString(L"Save this sign-in for next time on this PC. Otherwise it is only used until the game closes."));
	GadgetCheckBoxSetChecked(dialog.remember, FALSE);
	GadgetTextEntrySetText(dialog.password, UnicodeString::TheEmptyString);

	dialog.parent->winSetSystemFunc(dialogSystem);
	dialog.parent->winSetInputFunc(dialogInput);
	return true;
}

void showDialog()
{
	dialog.layout->hide(FALSE);
	dialog.layout->bringForward();
	dialog.parent->winBringToTop();
	TheWindowManager->winSetModal(dialog.parent);
	TheWindowManager->winSetFocus(dialog.state == State::Form ? dialog.userName : dialog.cancel);
}
}

void RunOnMainThread(std::function<void()> task)
{
	std::lock_guard<std::mutex> lock(mainThreadMutex);
	mainThreadTasks.push_back(std::move(task));
}

void PumpMainThreadTasks()
{
	std::deque<std::function<void()>> tasks;
	{
		std::lock_guard<std::mutex> lock(mainThreadMutex);
		tasks.swap(mainThreadTasks);
	}
	for (auto &task : tasks)
	{
		task();
	}
}

bool IsProxySignInOpen()
{
	return dialog.layout != nullptr;
}

void RequestProxySignIn(const std::string &proxyURL, std::function<void(bool)> done)
{
	if (!openDialog(false, "The proxy " + ProxyDisplayName(proxyURL) + " needs a sign-in."))
	{
		done(false);
		return;
	}
	dialog.proxyURL = proxyURL;
	dialog.done = std::move(done);
	GadgetTextEntrySetText(dialog.userName, wideText(ProxyUsername(proxyURL)));
	GadgetCheckBoxSetChecked(dialog.remember, HasProxyCredentials(proxyURL));
	showForm("", true);
	showDialog();
}

// Accept in Options: tests the new settings first and reports what to do with them
void ShowAcceptTest(const ServiceConnectionSettings &settings, std::function<void(AcceptChoice)> decided)
{
	if (!openDialog(true, "The proxy needs a sign-in."))
	{
		decided(AcceptChoice::Save);
		return;
	}
	dialog.acceptMode = true;
	dialog.settings = settings;
	dialog.decided = std::move(decided);
	setState(State::Testing, "Connection test", "Testing...", "");
	showDialog();
	startProbe(settings);
}
}
