/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "sys/platform.h"
#include "framework/FileSystem.h"
#include "framework/KeyInput.h"

#include "framework/EventLoop.h"
#include "idlib/math/Vector.h"

#include <SDL_joystick.h>

#ifndef SDL_JOYSTICK_AXIS_MAX
	#define SDL_JOYSTICK_AXIS_MAX 32767
#endif

idCVar idEventLoop::com_journal( "com_journal", "0", CVAR_INIT|CVAR_SYSTEM, "1 = record journal, 2 = play back journal", 0, 2, idCmdSystem::ArgCompletion_Integer<0,2> );

idEventLoop eventLoopLocal;
idEventLoop *eventLoop = &eventLoopLocal;

//mouse coordinates based on the right joystick
int dx = 0;
int dy = 0;
int mouseSpeed = 15;

/*
=================
idEventLoop::idEventLoop
=================
*/
idEventLoop::idEventLoop( void ) {
	com_journalFile = NULL;
	com_journalDataFile = NULL;
	initialTimeOffset = 0;
}

/*
=================
idEventLoop::~idEventLoop
=================
*/
idEventLoop::~idEventLoop( void ) {
}

/*
=================
idEventLoop::GetRealEvent
=================
*/
sysEvent_t	idEventLoop::GetRealEvent( void ) {
	int			r;
	sysEvent_t	ev;

	// either get an event from the system or the journal file
	if ( com_journal.GetInteger() == 2 ) {
		r = com_journalFile->Read( &ev, sizeof(ev) );
		if ( r != sizeof(ev) ) {
			common->FatalError( "Error reading from journal file" );
		}
		if ( ev.evPtrLength ) {
			ev.evPtr = Mem_ClearedAlloc( ev.evPtrLength );
			r = com_journalFile->Read( ev.evPtr, ev.evPtrLength );
			if ( r != ev.evPtrLength ) {
				common->FatalError( "Error reading from journal file" );
			}
		}
	} else {
		ev = Sys_GetEvent();

		// write the journal value out if needed
		if ( com_journal.GetInteger() == 1 ) {
			r = com_journalFile->Write( &ev, sizeof(ev) );
			if ( r != sizeof(ev) ) {
				common->FatalError( "Error writing to journal file" );
			}
			if ( ev.evPtrLength ) {
				r = com_journalFile->Write( ev.evPtr, ev.evPtrLength );
				if ( r != ev.evPtrLength ) {
					common->FatalError( "Error writing to journal file" );
				}
			}
		}
	}

	return ev;
}

/*
=================
idEventLoop::PushEvent
=================
*/
void idEventLoop::PushEvent( sysEvent_t *event ) {
	sysEvent_t		*ev;
	static			bool printedWarning;

	ev = &com_pushedEvents[ com_pushedEventsHead & (MAX_PUSHED_EVENTS-1) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = true;
			common->Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			Mem_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = false;
	}

	*ev = *event;
	com_pushedEventsHead++;
}

/*
=================
idEventLoop::GetEvent
=================
*/
sysEvent_t idEventLoop::GetEvent( void ) {
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ (com_pushedEventsTail-1) & (MAX_PUSHED_EVENTS-1) ];
	}
	return GetRealEvent();
}

/*
=================
idEventLoop::ProcessEvent
=================
*/
void idEventLoop::ProcessEvent( sysEvent_t ev ) {
	// track key up / down states
	if ( ev.evType == SE_KEY ) {
		idKeyInput::PreliminaryKeyEvent( ev.evValue, ( ev.evValue2 != 0 ) );
	}

	if ( ev.evType == SE_CONSOLE ) {
		// from a text console outside the game window
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, (char *)ev.evPtr );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "\n" );
	} else {
		session->ProcessEvent( &ev );
	}

	// free any block data
	if ( ev.evPtr ) {
		Mem_Free( ev.evPtr );
	}
}

sysEvent_t CreateMouseEvent(int dx, int dy){
	idVec2 v = idVec2();
	v.x = (dx / (float) SDL_JOYSTICK_AXIS_MAX) * mouseSpeed; //TODO - add variable - controller mouse speed
	v.y = (dy / (float) SDL_JOYSTICK_AXIS_MAX) * mouseSpeed;

	v.Truncate(mouseSpeed);

	sysEvent_t mouse = {};

	mouse.evType = SE_MOUSE;
	mouse.evValue = (int) v.x;
	mouse.evValue2 = (int) v.y;

	return mouse;
}

/*
===============
idEventLoop::RunEventLoop
===============
*/
int idEventLoop::RunEventLoop( bool commandExecution ) {
	sysEvent_t	ev;

	if(dx != 0 || dy != 0){
		//send the simulated mouse events
		//downstream
		ProcessEvent(CreateMouseEvent(dx, dy)); //MouseEvent for GUI
	}

	while ( 1 ) {

		if ( commandExecution ) {
			// execute any bound commands before processing another event
			cmdSystem->ExecuteCommandBuffer();
		}

		ev = GetEvent();

		if(ev.evType != SE_NONE){
			ProcessEvent(ev);

			if(ev.evType == SE_JOYSTICK_AXIS){
				//if the right stick was moved update
				//the simulated mouse dx/dy values
				if(ev.evValue == RX_AXIS || ev.evValue == RY_AXIS){
					if(ev.evValue == RX_AXIS){
						dx = ev.evValue2;
					}
					else {
						dy = ev.evValue2;
					}
				}
			}

			//one SDL event can lead to multiple sys events
			//therefore poll them from the sys event queue 
			//and send them downstream
			sysEvent_t event = {};
			int count = Sys_PollSysEvents();

			for(int i = 0; i < count; i++){
				Sys_ReturnSysEvent(i, event);

				ProcessEvent(event);
			}

			Sys_EndSysEvents();
		}
		else{
			return 0; //no more events available
		}
	}

	return 0;	// never reached
}

/*
=============
idEventLoop::Init
=============
*/
void idEventLoop::Init( void ) {

	initialTimeOffset = Sys_Milliseconds();

	common->StartupVariable( "journal", false );

	if ( com_journal.GetInteger() == 1 ) {
		common->Printf( "Journaling events\n" );
		com_journalFile = fileSystem->OpenFileWrite( "journal.dat" );
		com_journalDataFile = fileSystem->OpenFileWrite( "journaldata.dat" );
	} else if ( com_journal.GetInteger() == 2 ) {
		common->Printf( "Replaying journaled events\n" );
		com_journalFile = fileSystem->OpenFileRead( "journal.dat" );
		com_journalDataFile = fileSystem->OpenFileRead( "journaldata.dat" );
	}

	if ( !com_journalFile || !com_journalDataFile ) {
		com_journal.SetInteger( 0 );
		com_journalFile = 0;
		com_journalDataFile = 0;
		common->Printf( "Couldn't open journal files\n" );
	}
}

/*
=============
idEventLoop::Shutdown
=============
*/
void idEventLoop::Shutdown( void ) {
	if ( com_journalFile ) {
		fileSystem->CloseFile( com_journalFile );
		com_journalFile = NULL;
	}
	if ( com_journalDataFile ) {
		fileSystem->CloseFile( com_journalDataFile );
		com_journalDataFile = NULL;
	}
}

/*
================
idEventLoop::Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int idEventLoop::Milliseconds( void ) {
#if 1	// FIXME!
	return Sys_Milliseconds() - initialTimeOffset;
#else
	sysEvent_t	ev;

	// get events and push them until we get a null event with the current time
	do {

		ev = Com_GetRealEvent();
		if ( ev.evType != SE_NONE ) {
			Com_PushEvent( &ev );
		}
	} while ( ev.evType != SE_NONE );

	return ev.evTime;
#endif
}

/*
================
idEventLoop::JournalLevel
================
*/
int idEventLoop::JournalLevel( void ) const {
	return com_journal.GetInteger();
}
