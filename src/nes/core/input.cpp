/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 1998 BERO
*  Copyright (C) 2002 Xodnizel
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, 
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <string>
#include <ostream>
#include <string.h>

#include "types.h"
#include "x6502.h"

#include "fceu.h"
#include "sound.h"
//#include "netplay.h"
#include "state.h"
#include "input.h"
#include "vsuni.h"
#include "driver.h"

//it is easier to declare these input drivers extern here than include a bunch of files
//-------------
extern INPUTC *FCEU_InitPowerpadA(int w);
extern INPUTC *FCEU_InitPowerpadB(int w);
extern INPUTC *FCEU_InitArkanoid(int w);

extern INPUTCFC *FCEU_InitArkanoidFC(void);
extern INPUTCFC *FCEU_InitSpaceShadow(void);
extern INPUTCFC *FCEU_InitFKB(void);
extern INPUTCFC *FCEU_InitSuborKB(void);
extern INPUTCFC *FCEU_InitHS(void);
extern INPUTCFC *FCEU_InitMahjong(void);
extern INPUTCFC *FCEU_InitQuizKing(void);
extern INPUTCFC *FCEU_InitFamilyTrainerA(void);
extern INPUTCFC *FCEU_InitFamilyTrainerB(void);
extern INPUTCFC *FCEU_InitOekaKids(void);
extern INPUTCFC *FCEU_InitTopRider(void);
extern INPUTCFC *FCEU_InitBarcodeWorld(void);
//---------------

static uint8 joy_readbit[2];
uint8 joy[4]={0, 0, 0, 0}; //HACK - should be static but movie needs it
static uint8 LastStrobe;

//This function is a quick hack to get the NSF player to use emulated gamepad input.
uint8 FCEU_GetJoyJoy(void)
{
	return(joy[0]|joy[1]|joy[2]|joy[3]);
}

extern uint8 coinon;

//set to true if the fourscore is attached
static bool FSAttached = false;

JOYPORT joyports[2] = { JOYPORT(0), JOYPORT(1) };
FCPORT portFC;

static DECLFR(JPRead)
{
	uint8 ret=0;

	ret|=joyports[A&1].driver->Read(A&1);

	if(portFC.driver)
		ret = portFC.driver->Read(A&1, ret);

	ret|=X.DB&0xC0;
	return(ret);
}

static DECLFW(B4016)
{
	if(portFC.driver)
		portFC.driver->Write(V&7);

	for(int i=0;i<2;i++)
		joyports[i].driver->Write(V&1);

	if((LastStrobe&1) && (!(V&1)))
	{
		//old comment:
		//This strobe code is just for convenience.  If it were
		//with the code in input / *.c, it would more accurately represent
		//what's really going on.  But who wants accuracy? ;)
		//Seriously, though, this shouldn't be a problem.
		//new comment:

		//mbg 6/7/08 - I guess he means that the input drivers could track the strobing themselves
		//I dont see why it is unreasonable here.
		for(int i=0;i<2;i++)
			joyports[i].driver->Strobe(i);
		if(portFC.driver)
			portFC.driver->Strobe();
	}
	LastStrobe=V&0x1;
}

//a main joystick port driver representing the case where nothing is plugged in
static INPUTC DummyJPort={0};
//and an expansion port driver for the same ting
static INPUTCFC DummyPortFC={0};


//--------4 player driver for expansion port--------
static uint8 F4ReadBit[2];
static void StrobeFami4(void)
{
	F4ReadBit[0]=F4ReadBit[1]=0;
}

static uint8 ReadFami4(int w, uint8 ret)
{
	ret&=1;

	ret |= ((joy[2+w]>>(F4ReadBit[w]))&1)<<1;
	if(F4ReadBit[w]>=8) ret|=2;
	else F4ReadBit[w]++;

	return(ret);
}

static INPUTCFC FAMI4C={ReadFami4, 0, StrobeFami4, 0, 0, 0};
//------------------

//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


static uint8 ReadGPVS(int w)
{
	uint8 ret=0;

	if(joy_readbit[w]>=8)
		ret=1;
	else
	{
		ret = ((joy[w]>>(joy_readbit[w]))&1);
		if(!fceuindbg)
			joy_readbit[w]++;
	}
	return ret;
}

static void UpdateGP(int w, void *data, int arg)
{
	if(w==0)	//adelikat, 3/14/09: Changing the joypads to inclusive OR the user's joypad + the Lua joypad, this way lua only takes over the buttons it explicity says to
	{
		joy[0] = *(uint32 *)joyports[0].ptr;
	}
	else
	{
		joy[1] = *(uint32 *)joyports[1].ptr >> 8;
	}

}

//basic joystick port driver
static uint8 ReadGP(int w)
{
	uint8 ret;

	if(joy_readbit[w]>=8)
		ret = ((joy[2+w]>>(joy_readbit[w]&7))&1);
	else
		ret = ((joy[w]>>(joy_readbit[w]))&1);
	if(joy_readbit[w]>=16) ret=0;
	if(!FSAttached)
	{
		if(joy_readbit[w]>=8) ret|=1;
	}
	else
	{
		if(joy_readbit[w]==19-w) ret|=1;
	}
	if(!fceuindbg)
		joy_readbit[w]++;
	return ret;
}

static void StrobeGP(int w)
{
	joy_readbit[w]=0;
}

//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^6


static INPUTC GPC={ReadGP, 0, StrobeGP, UpdateGP, 0};
static INPUTC GPCVS={ReadGPVS, 0, StrobeGP, UpdateGP, 0};

void FCEU_UpdateInput(void)
{
	//tell all drivers to poll input and set up their logical states
	{
		for(int port = 0; port < 2; port++)
		{
			joyports[port].driver->Update(port, joyports[port].ptr, joyports[port].attrib);
		}

		portFC.driver->Update(portFC.ptr, portFC.attrib);
	}

	//if(GameInfo->type == GIT_VSUNI)
	//{
	//	if(coinon)
	//	{
	//		coinon--;
	//	}
	//}

	//if(FCEUnetplay)
	//	NetplayUpdate(joy);

	//TODO - should this apply to the movie data? should this be displayed in the input hud?
	//if(GameInfo->type==GIT_VSUNI)
	//	FCEU_VSUniSwap(&joy[0], &joy[1]);
}

static DECLFR(VSUNIRead0)
{
	uint8 ret=0;

	ret|=(joyports[0].driver->Read(0))&1;

	ret|=(vsdip&3)<<3;
	if(coinon)
		ret|=0x4;
	return ret;
}

static DECLFR(VSUNIRead1)
{
	uint8 ret=0;

	ret|=(joyports[1].driver->Read(1))&1;
	ret|=vsdip&0xFC;
	return ret;
}



//calls from the ppu;
//calls the SLHook for any driver that needs it
void InputScanlineHook(uint8 *bg, uint8 *spr, uint32 linets, int final)
{
	for(int port=0;port<2;port++)
		joyports[port].driver->SLHook(port, bg, spr, linets, final);
	portFC.driver->SLHook(bg, spr, linets, final);
}

//binds JPorts[pad] to the driver specified in JPType[pad]
static void SetInputStuff(int port)
{
	switch(joyports[port].type)
	{
	case SI_GAMEPAD:
		if(GameInfo->type==GIT_VSUNI)
			joyports[port].driver = &GPCVS;
		else
			joyports[port].driver= &GPC;
		break;
	case SI_ARKANOID:
		joyports[port].driver=FCEU_InitArkanoid(port);
		break;
	case SI_POWERPADA:
		joyports[port].driver=FCEU_InitPowerpadA(port);
		break;
	case SI_POWERPADB:
		joyports[port].driver=FCEU_InitPowerpadB(port);
		break;
	case SI_NONE:
		joyports[port].driver=&DummyJPort;
		break;
	}
}

static void SetInputStuffFC()
{
	switch(portFC.type)
	{
	case SIFC_NONE:
		portFC.driver=&DummyPortFC;
		break;
	case SIFC_ARKANOID:
		portFC.driver=FCEU_InitArkanoidFC();
		break;
	case SIFC_SHADOW:
		portFC.driver=FCEU_InitSpaceShadow();
		break;
	case SIFC_OEKAKIDS:
		portFC.driver=FCEU_InitOekaKids();
		break;
	case SIFC_4PLAYER:
		portFC.driver=&FAMI4C;
		memset(&F4ReadBit, 0, sizeof(F4ReadBit));
		break;
	case SIFC_FKB:
		portFC.driver=FCEU_InitFKB();
		break;
	case SIFC_SUBORKB:
		portFC.driver=FCEU_InitSuborKB();
		break;
	case SIFC_HYPERSHOT:
		portFC.driver=FCEU_InitHS();
		break;
	case SIFC_MAHJONG:
		portFC.driver=FCEU_InitMahjong();
		break;
	case SIFC_QUIZKING:
		portFC.driver=FCEU_InitQuizKing();
		break;
	case SIFC_FTRAINERA:
		portFC.driver=FCEU_InitFamilyTrainerA();
		break;
	case SIFC_FTRAINERB:
		portFC.driver=FCEU_InitFamilyTrainerB();
		break;
	case SIFC_BWORLD:
		portFC.driver=FCEU_InitBarcodeWorld();
		break;
	case SIFC_TOPRIDER:
		portFC.driver=FCEU_InitTopRider();
		break;
	}
}

void FCEUI_SetInput(int port, ESI type, void *ptr, int attrib)
{
	joyports[port].attrib = attrib;
	joyports[port].type = type;
	joyports[port].ptr = ptr;
	SetInputStuff(port);
}

void FCEUI_SetInputFC(ESIFC type, void *ptr, int attrib)
{
	portFC.attrib = attrib;
	portFC.type = type;
	portFC.ptr = ptr;
	SetInputStuffFC();
}


//initializes the input system to power-on state
void InitializeInput(void)
{
	memset(joy_readbit, 0, sizeof(joy_readbit));
	memset(joy, 0, sizeof(joy));
	LastStrobe = 0;

	if(GameInfo->type==GIT_VSUNI)
	{
		SetReadHandler(0x4016, 0x4016, VSUNIRead0);
		SetReadHandler(0x4017, 0x4017, VSUNIRead1);
	}
	else
		SetReadHandler(0x4016, 0x4017, JPRead);

	SetWriteHandler(0x4016, 0x4016, B4016);

	//force the port drivers to be setup
	SetInputStuff(0);
	SetInputStuff(1);
	SetInputStuffFC();
}


bool FCEUI_GetInputFourscore()
{
	return FSAttached;
}
void FCEUI_SetInputFourscore(bool attachFourscore)
{
	FSAttached = attachFourscore;
}
