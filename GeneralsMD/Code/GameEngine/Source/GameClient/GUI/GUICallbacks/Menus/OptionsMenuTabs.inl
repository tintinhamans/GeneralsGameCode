/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
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

// Tabbed Options menu, included by OptionsMenu.cpp. Adapted from the tabbed menu by simonx645.
//
// A layout turns it on by pairing radio buttons named Tab<Name> with windows named Page<Name>.
// TabIcon<Name> and TabLabel<Name> are optional and drawn by their tab. The icon shows the
// image <its image>On on the current tab. Any other layout keeps the single page menu.
// TabHotkeys needs no page: it opens the Hotkeys screen over the menu and the shown page stays.
//
// A tab without the IMAGE status is a flat box coloured by its draw data: slot 0 of the enabled
// set at rest, slot 0 of the hilite set under the mouse, slot 1 of the hilite set when current.

namespace OptionsTabs
{

enum { MAX_TABS = 12, MAX_WRAPPED_INPUTS = 256 };

struct Tab
{
	AsciiString name;
	GameWindow *button;
	GameWindow *page;
	GameWindow *icon;
	GameWindow *label;
	const Image *iconOff;
	const Image *iconOn;
	GameWinDrawFunc buttonDraw;
	Color labelColor;
	Color labelBorderColor;
	Int screenX;
};

struct WrappedInput
{
	GameWindow *window;
	GameWinInputFunc input;
};

static Tab s_tabs[MAX_TABS];
static Int s_tabCount = 0;
static Int s_current = -1;
static GameWindow *s_parent = nullptr;
static WrappedInput s_wrappedInputs[MAX_WRAPPED_INPUTS];
static Int s_wrappedInputCount = 0;

// Setting a radio button re-sends GBM_SELECTED, which must not count as a click
static Bool s_selecting = FALSE;

// The tab to reopen, kept for the session
static AsciiString s_lastTabName;

static Bool isOn()
{
	return s_tabCount > 0;
}

static GameWindow *findWindow( const char *prefix, const AsciiString &name )
{
	AsciiString full;
	full.format( "OptionsMenu.wnd:%s%s", prefix, name.str() );
	return TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( full ) );
}

static Bool isInside( GameWindow *window, GameWindow *ancestor )
{
	for( ; window != nullptr; window = window->winGetParent() )
	{
		if( window == ancestor )
		{
			return TRUE;
		}
	}
	return FALSE;
}

static Bool isGadget( GameWindow *window )
{
	return ( window->winGetStyle() & ( GWS_PUSH_BUTTON | GWS_RADIO_BUTTON | GWS_CHECK_BOX | GWS_HORZ_SLIDER | GWS_ENTRY_FIELD | GWS_COMBO_BOX ) ) != 0;
}

static Int tabOfPage( GameWindow *window )
{
	for( Int i = 0; i < s_tabCount; ++i )
	{
		if( isInside( window, s_tabs[i].page ) )
		{
			return i;
		}
	}
	return -1;
}

static Int firstPageTab()
{
	for( Int i = 0; i < s_tabCount; ++i )
	{
		if( s_tabs[i].page != nullptr )
		{
			return i;
		}
	}
	return -1;
}

// The layout stretches with the screen, so the image keeps its own shape inside the window
static void drawImageInWindow( GameWindow *window, const Image *image )
{
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	const Int imageWidth = image->getImageWidth();
	const Int imageHeight = image->getImageHeight();
	if( width <= 0 || height <= 0 || imageWidth <= 0 || imageHeight <= 0 )
	{
		return;
	}
	Int drawWidth = width;
	Int drawHeight = width * imageHeight / imageWidth;
	if( drawHeight > height )
	{
		drawHeight = height;
		drawWidth = height * imageWidth / imageHeight;
	}
	x += ( width - drawWidth ) / 2;
	y += ( height - drawHeight ) / 2;
	TheWindowManager->winDrawImage( image, x, y, x + drawWidth, y + drawHeight );
}

// Radio button art keeps its end caps' pixel width at any height, so it stretches on a tab
static void drawTabBox( GameWindow *window, WinInstanceData *instData, Bool current )
{
	const Real lineWidth = 1.0f;
	Color fill = window->winGetEnabledColor( 0 );
	Color border = window->winGetEnabledBorderColor( 0 );
	if( current )
	{
		fill = window->winGetHiliteColor( 1 );
		border = window->winGetHiliteBorderColor( 1 );
	}
	else if( BitIsSet( instData->getState(), WIN_STATE_HILITED ) )
	{
		fill = window->winGetHiliteColor( 0 );
		border = window->winGetHiliteBorderColor( 0 );
	}
	Int x, y, width, height;
	window->winGetScreenPosition( &x, &y );
	window->winGetSize( &width, &height );
	TheWindowManager->winOpenRect( border, lineWidth, x, y, x + width, y + height );
	TheWindowManager->winFillRect( fill, lineWidth, x + 1, y + 1, x + width - 1, y + height - 1 );
}

// The icon and label stay hidden so they never take the tab's clicks, and the tab draws them
static void drawTab( GameWindow *window, WinInstanceData *instData )
{
	for( Int i = 0; i < s_tabCount; ++i )
	{
		const Tab &tab = s_tabs[i];
		if( tab.button != window )
		{
			continue;
		}
		if( !BitIsSet( window->winGetStatus(), WIN_STATUS_IMAGE ) )
		{
			drawTabBox( window, instData, i == s_current );
		}
		else if( tab.buttonDraw != nullptr )
		{
			tab.buttonDraw( window, instData );
		}
		const Image *image = ( i == s_current && tab.iconOn != nullptr ) ? tab.iconOn : tab.iconOff;
		if( tab.icon != nullptr && image != nullptr )
		{
			drawImageInWindow( tab.icon, image );
		}
		if( tab.label != nullptr && tab.label->winGetDrawFunc() != nullptr )
		{
			tab.label->winGetDrawFunc()( tab.label, tab.label->winGetInstanceData() );
		}
		return;
	}
}

static void collectGadgets( GameWindow *window, std::vector<GameWindow *> &gadgets )
{
	for( GameWindow *child = window->winGetChild(); child != nullptr; child = child->winGetNext() )
	{
		if( child->winIsHidden() )
		{
			continue;
		}
		if( isGadget( child ) )
		{
			if( !BitIsSet( child->winGetStatus(), WIN_STATUS_NO_INPUT ) )
			{
				gadgets.push_back( child );
			}
			continue;
		}
		collectGadgets( child, gadgets );
	}
}

// Reading order, with a few pixels of slack for controls that share a row
static bool isBeforeOnScreen( GameWindow *a, GameWindow *b )
{
	enum { ROW_SLACK = 8 };
	Int ax, ay, bx, by, width, height;
	a->winGetScreenPosition( &ax, &ay );
	a->winGetSize( &width, &height );
	ay += height / 2;
	b->winGetScreenPosition( &bx, &by );
	b->winGetSize( &width, &height );
	by += height / 2;
	if( ay / ROW_SLACK != by / ROW_SLACK )
	{
		return ay < by;
	}
	return ax < bx;
}

// The Tab key walks the shown page's controls, then the menu's own buttons
static void registerFocusOrder( Int index )
{
	std::vector<GameWindow *> gadgets;
	collectGadgets( s_tabs[index].page, gadgets );
	std::stable_sort( gadgets.begin(), gadgets.end(), isBeforeOnScreen );

	GameWindowList order( gadgets.begin(), gadgets.end() );
	static const char *const buttons[] = { "ButtonDefaults", "ButtonAccept", "ButtonBack" };
	for( Int i = 0; i < ARRAY_SIZE( buttons ); ++i )
	{
		GameWindow *button = findWindow( "", buttons[i] );
		if( button != nullptr && !button->winIsHidden() )
		{
			order.push_back( button );
		}
	}
	TheWindowManager->registerTabList( order );
}

static void show( Int index )
{
	if( !isOn() )
	{
		return;
	}
	index = clamp( 0, index, s_tabCount - 1 );
	if( s_tabs[index].page == nullptr )
	{
		return;
	}

	// an open combo list closes with its page
	TheWindowManager->winSetLoneWindow( nullptr );

	GameWindow *focus = TheWindowManager->winGetFocus();
	if( focus != nullptr && tabOfPage( focus ) >= 0 && tabOfPage( focus ) != index )
	{
		TheWindowManager->winSetFocus( s_parent );
	}

	for( Int i = 0; i < s_tabCount; ++i )
	{
		const Tab &tab = s_tabs[i];
		if( tab.page != nullptr )
		{
			tab.page->winHide( i != index );
		}
		if( tab.label != nullptr )
		{
			const Bool current = i == index;
			tab.label->winSetEnabledTextColors( current ? tab.label->winGetHiliteTextColor() : tab.labelColor,
				current ? tab.label->winGetHiliteTextBorderColor() : tab.labelBorderColor );
		}
	}

	s_selecting = TRUE;
	GadgetRadioSetSelection( s_tabs[index].button, FALSE );
	s_selecting = FALSE;

	s_current = index;
	s_lastTabName = s_tabs[index].name;
	registerFocusOrder( index );
}

static void step( Int delta )
{
	if( !isOn() )
	{
		return;
	}
	Int index = max( s_current, 0 );
	for( Int i = 0; i < s_tabCount; ++i )
	{
		index = ( index + delta + s_tabCount ) % s_tabCount;
		if( s_tabs[index].page != nullptr )
		{
			show( index );
			return;
		}
	}
}

// A click or an F key on a tab
static void pick( Int index )
{
	if( s_tabs[index].page != nullptr )
	{
		show( index );
		return;
	}
	if( s_current >= 0 )
	{
		// the click moved the radio group's mark off the shown page's tab
		s_selecting = TRUE;
		GadgetRadioSetSelection( s_tabs[s_current].button, FALSE );
		s_selecting = FALSE;
	}
	HotkeysMenuOpen();
}

static void showPageOf( GameWindow *control )
{
	const Int index = control != nullptr ? tabOfPage( control ) : -1;
	if( index >= 0 && index != s_current )
	{
		show( index );
	}
}

// A click on a tab's radio button
static Bool onButtonSelected( GameWindow *control )
{
	if( s_selecting )
	{
		return isOn();
	}
	for( Int i = 0; i < s_tabCount; ++i )
	{
		if( s_tabs[i].button == control )
		{
			pick( i );
			return TRUE;
		}
	}
	return FALSE;
}

// F1 and up pick a tab; PageUp, PageDown, Ctrl+Tab, Q and E step through them. Returns TRUE
// for a tab key. The key acts on release, so holding it down does not race through the tabs.
static Bool onKey( UnsignedByte key, UnsignedByte state, GameWindow *from )
{
	if( !isOn() )
	{
		return FALSE;
	}
	const Bool ctrl = ( state & KEY_STATE_CONTROL ) != 0;
	const Bool shift = ( state & KEY_STATE_SHIFT ) != 0;
	const Bool alt = ( state & KEY_STATE_ALT ) != 0;
	Int target = -1;
	Int delta = 0;
	if( key >= KEY_F1 && key < KEY_F1 + s_tabCount && key <= KEY_F10 && !ctrl && !alt )
	{
		target = key - KEY_F1;
	}
	else if( key == KEY_PGUP && !alt )
	{
		delta = -1;
	}
	else if( key == KEY_PGDN && !alt )
	{
		delta = 1;
	}
	else if( key == KEY_TAB && ctrl )
	{
		delta = shift ? -1 : 1;
	}
	else if( ( key == KEY_Q || key == KEY_E ) && !ctrl && !alt )
	{
		GameWindow *focus = TheWindowManager->winGetFocus();
		if( ( focus != nullptr && BitIsSet( focus->winGetStyle(), GWS_ENTRY_FIELD ) ) ||
			( from != nullptr && BitIsSet( from->winGetStyle(), GWS_ENTRY_FIELD ) ) )
		{
			return FALSE;
		}
		delta = key == KEY_Q ? -1 : 1;
	}
	else
	{
		return FALSE;
	}

	if( BitIsSet( state, KEY_STATE_UP ) )
	{
		if( target >= 0 )
		{
			pick( target );
		}
		else
		{
			step( delta );
		}
	}
	return TRUE;
}

// A focused gadget takes keys such as Tab before the menu sees them, so the tab keys go first
static WindowMsgHandledType wrappedInput( GameWindow *window, UnsignedInt msg, WindowMsgData mData1, WindowMsgData mData2 )
{
	if( msg == GWM_CHAR && onKey( (UnsignedByte)mData1, (UnsignedByte)mData2, window ) )
	{
		return MSG_HANDLED;
	}
	for( Int i = 0; i < s_wrappedInputCount; ++i )
	{
		if( s_wrappedInputs[i].window == window )
		{
			return s_wrappedInputs[i].input != nullptr ? s_wrappedInputs[i].input( window, msg, mData1, mData2 ) : MSG_IGNORED;
		}
	}
	return MSG_IGNORED;
}

static void wrapGadgetInputs( GameWindow *window )
{
	for( GameWindow *child = window->winGetChild(); child != nullptr; child = child->winGetNext() )
	{
		if( !isGadget( child ) )
		{
			wrapGadgetInputs( child );
			continue;
		}
		if( s_wrappedInputCount < MAX_WRAPPED_INPUTS && child->winGetInputFunc() != wrappedInput )
		{
			s_wrappedInputs[s_wrappedInputCount].window = child;
			s_wrappedInputs[s_wrappedInputCount].input = child->winGetInputFunc();
			++s_wrappedInputCount;
			child->winSetInputFunc( wrappedInput );
		}
	}
}

static void collectTabs( GameWindow *window )
{
	static const char *const prefix = "OptionsMenu.wnd:Tab";
	for( GameWindow *child = window->winGetChild(); child != nullptr; child = child->winGetNext() )
	{
		const AsciiString &name = child->winGetInstanceData()->m_decoratedNameString;
		if( !BitIsSet( child->winGetStyle(), GWS_RADIO_BUTTON ) )
		{
			if( !isGadget( child ) )
			{
				collectTabs( child );
			}
			continue;
		}
		if( s_tabCount >= MAX_TABS || !name.startsWith( prefix ) )
		{
			continue;
		}

		Tab tab;
		tab.name = name.str() + strlen( prefix );
		tab.button = child;
		tab.page = findWindow( "Page", tab.name );
		if( tab.page == nullptr && tab.name.compareNoCase( "Hotkeys" ) != 0 )
		{
			continue;
		}
		tab.icon = findWindow( "TabIcon", tab.name );
		tab.label = findWindow( "TabLabel", tab.name );
		tab.iconOff = tab.icon != nullptr ? tab.icon->winGetEnabledImage( 0 ) : nullptr;
		tab.iconOn = nullptr;
		if( tab.iconOff != nullptr && TheMappedImageCollection != nullptr )
		{
			AsciiString onName = tab.iconOff->getName();
			onName.concat( "On" );
			tab.iconOn = TheMappedImageCollection->findImageByName( onName );
		}
		tab.buttonDraw = child->winGetDrawFunc();
		tab.labelColor = tab.label != nullptr ? tab.label->winGetEnabledTextColor() : 0;
		tab.labelBorderColor = tab.label != nullptr ? tab.label->winGetEnabledTextBorderColor() : 0;
		Int y;
		child->winGetScreenPosition( &tab.screenX, &y );
		s_tabs[s_tabCount++] = tab;
	}
}

static bool isTabLeftOf( const Tab &a, const Tab &b )
{
	return a.screenX < b.screenX;
}

// Called before the menu fills its controls. Returns TRUE for a tabbed layout.
static Bool init()
{
	s_tabCount = 0;
	s_current = -1;
	s_wrappedInputCount = 0;
	s_selecting = FALSE;
	s_parent = findWindow( "", "OptionsMenuParent" );
	if( s_parent == nullptr )
	{
		return FALSE;
	}
	collectTabs( s_parent );
	// a layout needs at least one page to be tabbed
	if( firstPageTab() < 0 )
	{
		s_tabCount = 0;
		return FALSE;
	}
	std::sort( s_tabs, s_tabs + s_tabCount, isTabLeftOf );

	for( Int i = 0; i < s_tabCount; ++i )
	{
		Tab &tab = s_tabs[i];
		tab.button->winSetDrawFunc( drawTab );
		if( tab.icon != nullptr )
		{
			tab.icon->winHide( TRUE );
		}
		if( tab.label != nullptr )
		{
			AsciiString textLabel;
			textLabel.format( "GUI:OptionsTab%s", tab.name.str() );
			UnicodeString fallback;
			fallback.translate( tab.name );
			GadgetStaticTextSetText( tab.label, TheGameText->FETCH_OR_SUBSTITUTE( textLabel.str(), fallback.str() ) );
			tab.label->winHide( TRUE );
		}
	}
	wrapGadgetInputs( s_parent );
	return TRUE;
}

// Called once every control holds its value
static void start()
{
	if( !isOn() )
	{
		return;
	}
	Int index = firstPageTab();
	for( Int i = 0; i < s_tabCount; ++i )
	{
		if( s_tabs[i].page != nullptr && s_tabs[i].name == s_lastTabName )
		{
			index = i;
		}
	}
	show( index );
}

// Called before the menu's windows are destroyed
static void shutdown()
{
	if( isOn() )
	{
		TheWindowManager->clearTabList();
	}
	s_tabCount = 0;
	s_current = -1;
	s_wrappedInputCount = 0;
	s_parent = nullptr;
}

}	// namespace OptionsTabs
