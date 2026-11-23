/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/


#pragma once

#include "GameNetwork/Online/IOnlineLadder.h"

class GameSpyLadder : public IOnlineLadder
{
public:
	GameSpyLadder();
	virtual ~GameSpyLadder();

	virtual const LadderInfo* findLadder( const AsciiString& addr, UnsignedShort port );
	virtual const LadderInfo* findLadderByIndex( Int index );
	virtual const LadderInfoList* getLocalLadders();
	virtual const LadderInfoList* getSpecialLadders();
	virtual const LadderInfoList* getStandardLadders();

private:
	void loadLocalLadders();
	void checkLadder( AsciiString fname, Int index );
	LadderInfoList m_localLadders;
	LadderInfoList m_specialLadders;
	LadderInfoList m_standardLadders;
};
