#include "CD.H"

#include "CONTROL.H"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <SYS/TYPES.H>
#include <LIBCD.H>
#include <LIBSPU.H>
#include <LIBETC.H>

//Number of XA files on disc (XA1-17.XA)
#define NUM_XA_FILES 17

#define XA_FILE_NAME "\\XA%d.XA;1"

#define CDS "CDS!" + __TIMESTAMP__ + " ."

unsigned short XATrackClip[] =//Probably used for XA audio start/end pos of each track
{
	0xFF00, 0xFF00, 0xFE80, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF80,
	0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00,
	0xFE80, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF80, 0xFF80,
	0xFE00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFE00, 0xFF80, 0xFF80,
	0xFF00, 0xFF00, 0xFF80, 0xFF80, 0xFF00, 0xFF00, 0xFF00, 0xFF80,
	0xFF80, 0xFE00, 0xFF80, 0xFF00, 0xFF80, 0xFF80, 0xFF80, 0xFF80,
	0xFF00, 0xFF00, 0xFF80, 0xFF80, 0xFF80, 0xFF80, 0xFF80, 0xFF00,
	0xFE00, 0xFF00, 0xFF80, 0xFF80, 0xFF80, 0xFF80, 0xFF00, 0xFF80,
	0xFE00, 0xFD80, 0xFD80, 0xFE80, 0xFE80, 0xFE80, 0xFF00, 0xFF80,
	0xFA00, 0xFC00, 0xFC00, 0xFC00, 0xFD80, 0xFF00, 0xFE00, 0xFF80,
	0xFE00, 0xFE00, 0xFD80, 0xFE80, 0xFF00, 0xFF00, 0xFF00, 0xFF00,
	0xFA00, 0xFB00, 0xFC00, 0xFB80, 0xFC80, 0xFC80, 0xFF00, 0xFFC0,
	0xFC00, 0xFD00, 0xFD00, 0xFD00, 0xFE80, 0xFE80, 0xFF00, 0xFF80,
	0xF700, 0xF880, 0xF780, 0xFC80, 0xFE80, 0xFE80, 0xFD80, 0xFE80,
	0xEF80, 0xF000, 0xF280, 0xF480, 0xF700, 0xFD80, 0xFD00, 0xFF80,
	0xF880, 0xFD80, 0xFD00, 0xFE00, 0xFE80, 0xFF40, 0xFF80, 0xFF80,
	0xD200, 0xD500, 0xD600, 0xD880, 0xD880, 0xDB80, 0xE900, 0xFE80
};

short XAFadeRate = 8;
int current_cd_mode = -1;
volatile int XACurPos;
volatile int XAEndPos;
volatile short XAVolume;
volatile short XAReqVolume;
volatile char XAFlag;
short XAMasterVolume;
short XATrack;
short XAReqTrack;
char XAWait;
static char XARepeat;
int XAStartPos;
static int XATrackList[17][2];

#if PLAY_FMVS
int dword_A5660[8];
#endif

//Holds all game data file positions or offsets in GAMEWAD.OBJ.
struct GAMEWAD_header gwHeader;

//LBA for the GAMEWAD.OBJ file on disc, this is set by InitNewCDSystem(), CDPosToInt() (See CD.C)
int gwLba = 0;

//Start sector for the current gamewad file entry.
static int cdStartSector = 0;

//Current sector for the gamewad file entry, updated as data is read from disk.
int cdCurrentSector = 0;

void CDDA_SetVolume(int nVolume)//5D7FC(<), 5DC78(<) (F) (*) (D)
{
#ifndef NO_SOUND
	SpuCommonAttr attr;
	
	attr.mask = SPU_COMMON_CDVOLL | SPU_COMMON_CDVOLR | SPU_COMMON_CDMIX;
	attr.cd.volume.left = nVolume << 6;
	attr.cd.volume.right = nVolume << 6;
	attr.cd.mix = SPU_ON;
	
	SpuSetCommonAttr(&attr);
#endif
}

void XAReplay()//5D838(<), 5DCB4(<) (F) (*) (D)
{
	CdlLOC loc;

	CdIntToPos(XAStartPos, &loc);
	
	if (CdControl(CdlReadS, (unsigned char*)&loc, NULL) == 1)
	{
		XACurPos = XAStartPos;
	}

	return;
}

void cbvsync()//5D884(<), 5DD00(<) (F)
{
	int ret;
	unsigned char io[8];
	int cnt;

	switch (XAFlag)
	{
	case 1:
	{
		if (XAVolume == 0)
		{
			XAFlag++;
		}
		//5DF9C
		break;
	}
	case 2:
	{
		XATrack = XAReqTrack;

		if (XAReqTrack < 0)
		{
			cnt = XAReqTrack + 7;
		}
		else
		{
			cnt = XAReqTrack;
		}

		XAStartPos = XATrackList[cnt >> 3][0];
		io[0] = 1;
		XAEndPos = XATrackList[cnt >> 3][1] + (short)XATrackClip[XAReqTrack];
		io[1] = XATrack & 7;
		XACurPos = XAStartPos;
		CdControlF(CdlSetfilter, &io[0]);
		XAFlag++;
		break;
	}
	case 3:
	{
		//loc_5D980
		XAReplay();
		XAReqVolume = XAMasterVolume;
		XAFlag++;

		break;
	}
	case 4:
	{
		//loc_5D9AC
		if (XAVolume == XAMasterVolume)
		{
			XAFlag++;
			XAWait = 60;
		}

		break;
	}
	case 5:
	{
		//loc_5D9E0
		if (XAWait == 0)
		{
			XAFlag++;
		}

		XAWait--;

		break;
	}
	case 6:
	{
		//loc_5DA18
		if (((VSync(-1)) & 7) == 0)
		{
			ret = CdSync(1, &io[0]);

			if (ret == 5)
			{
				XAReplay();
			}
			else if (ret == 2)
			{
				if (XACurPos >= XAEndPos)
				{
					if (XARepeat)
					{
						XAReplay();
					}
					else
					{
						if (CurrentAtmosphere == 0)
						{
							CdControlB(CdlPause, NULL, NULL);
							XAFlag = 7;
						}
						else
						{
							//5DAB8
							XAVolume = 0;
							XARepeat = 1;
							XAFlag = ret;
							XAReqTrack = CurrentAtmosphere;
							CDDA_SetVolume(0);
							IsAtmospherePlaying = 1;
						}
					}
				}
				else
				{
					//5DAEC
					if (CdLastCom() == CdlGetlocP)
					{
						cnt = CdPosToInt((CdlLOC*)&io[5]);
						if (cnt > 0)
						{
							XACurPos = cnt;
						}
					}

					CdControlF(CdlGetlocP, NULL);
				}
			}
		}
		break;
	}
	case 7:
		break;
	default:
		break;
	}

	if (XAVolume < XAReqVolume)
	{
		XAVolume += XAFadeRate;

		if (XAVolume >= XAReqVolume)
		{
			XAVolume = XAReqVolume;
		}

		CDDA_SetVolume(XAVolume);
	}
	else if (XAVolume > XAReqVolume)
	{
		XAVolume -= XAFadeRate;

		if (XAVolume <= XAVolume)
		{
			XAVolume = XAReqVolume;
		}

		CDDA_SetVolume(XAVolume);
	}

	//loc_5DC00
	return;
}

void S_CDPlay(short track, int mode)//5DC10(<), 5E08C(<) (F) (*) (D)
{
	unsigned char param[4];

	if (XATrack == -1)
	{
		param[0] = 200;
		CdControlB(CdlSetmode, &param[0], NULL);
		VSync(3);
		CdControlB(CdlPause, NULL, NULL);
		DEL_ChangeCDMode(1);
	}

	if (XATrack != track && XAReqTrack != track)
	{
		XAReqTrack = track;
		XARepeat = mode;

		if (XAFlag != 0)
		{
			XAFlag = 1;
			XAReqVolume = 0;
		}
		else
		{
			XAFlag = 2;
		}
	}

	//loc_5DCBC
	return;
}

void S_CDStop()//5DCD0(<), 5E14C(<) (F) (*) (D)
{
	XAFlag = 0;

	CdControlB(CdlPause, NULL, NULL);

	XAReqTrack = -1;
	XATrack = -1;

	DEL_ChangeCDMode(0);
	return;
}

void S_CDPause()//5DD14(<), 5E190(<) (F) (*) (D)
{
	if (XATrack >= 0)
	{
		CdControlF(CdlPause, NULL);
	}

	return;
}

void S_CDRestart()//5DD40(<) (F) (*)
{
	if (XATrack >= 0 && XAFlag != 7)
	{
		CdControlF(CdlReadS, NULL);
	}

	return;
}

void S_StartSyncedAudio(int nTrack)//5DD78(<), 5E1F4(<) (F) (*) (D)
{
	IsAtmospherePlaying = 0;

	S_CDPlay(nTrack, 0);

	while (XAFlag < 4) {}

	VSync(29);

	return;
}

void CDDA_SetMasterVolume(int nVolume)//5DDC4(<), 5E240(<) (F) (*) (D)
{
	XAMasterVolume = nVolume;
	CDDA_SetVolume(nVolume);
}

void InitNewCDSystem()//5DDE8(<), 5E264(<) (F) (*) (D) (ND)
{
	int i;
	long local_wadfile_header[512];
	CdlFILE fp;
	char buf[80];

	DEL_ChangeCDMode(0);

#if DISC_VERSION
	CdSearchFile(&fp, GAMEWAD_FILENAME);//662F0
	CdControlB(CdlSetloc, (unsigned char*)&fp, NULL);//6956C
	CdRead(1, (unsigned long*)&local_wadfile_header[0], CdlModeSpeed); //69C4C

	while (CdReadSync(1, 0) > 0)
	{
		VSync(0);
	}

	memcpy(&gwHeader, &local_wadfile_header[0], 512);//5F6AC

	gwLba = CdPosToInt(&fp.pos);//66270
#endif

	//loc_5E2E8
	for (i = 0; i < NUM_XA_FILES; i++)
	{
		sprintf(&buf[0], XA_FILE_NAME, i + 1);
		CdSearchFile(&fp, &buf[0]);
		XATrackList[i][0] = CdPosToInt(&fp.pos);
		XATrackList[i][1] = XATrackList[i][0] + ((fp.size + ((1 << CD_SECTOR_SHIFT) - 1)) >> CD_SECTOR_SHIFT);
	}

	XAFlag = 0;
	XAVolume = 0;
	XAReqTrack = -1;
	XATrack = -1;

#if PLAY_FMVS
	sub_5E708();
#endif
}

void DEL_ChangeCDMode(int mode)//5DEB0(<), 5E650 (F) (*) (D)
{
	unsigned char param[4];

	if (mode == 0)
	{
		if (current_cd_mode != 0)
		{
			current_cd_mode = 0;
			param[0] = CdlModeSpeed;
			CdControlB(CdlSetmode, &param[0], NULL);
			VSync(3);
		}
	}
	else if (mode == 1)
	{
		//loc_5DEF8
		if (current_cd_mode != 1)
		{
			current_cd_mode = mode;
		}
	}
	else if (mode == 2)
	{
		//loc_5DF20
		if (current_cd_mode != 2)
		{
			current_cd_mode = mode;
			param[0] = CdlModeSpeed;
			CdControlB(CdlSetmode, &param[0], NULL);
			VSync(3);
		}
	}

	//loc_5DF58
	return;
}

/*
* [FUNCTIONALITY] - DEL_CDFS_OpenFile.
* GAMEWAD_header is already in the memory at this point, we loaded it during InitNewCDSystem() (See CD.C)
* This method initialises the CD's read sector for a specific gamewad entry.
* File ID indices must match GAMEWAD.OBJ, see gw_files enum (GAMEWAD.H)
* Note: A File ID of "NONE" or 0 will initialise the reader position back to 0.
*
* [USAGE]
* @PARAM - [filenum] index into GAMEWAD_header.entries you wish to seek to.
* @RETURN - Filesize of the gamewad entry in bytes.
*/

int DEL_CDFS_OpenFile(int filenum /*$a0*/)//*, 5E3C0(<) (F) (D)
{
	DEL_ChangeCDMode(0);
	cdCurrentSector = cdStartSector = gwLba + (gwHeader.entries[filenum].fileOffset >> CD_SECTOR_SHIFT);
	return gwHeader.entries[filenum].fileSize;
}

/*
* [FUNCTIONALITY] - DEL_CDFS_Read
* It is assumed that prior to calling this you have initialised the cd's
* reader's position to the file entry you wish to read see (DEL_CDFS_OpenFile)
* This method reads data from disc at it's last sectior/read position.
*
* [USAGE]
* @PARAM - [size] the number of bytes you wish to read [addr] the memory location the data is read to.
*/

int DEL_CDFS_Read(char* addr, int size)//*, 5E414(<) (F) (*) (D)
{
	CdlFILE fp;
	long numSectorsToRead;
	long remainingDataToRead;
	char buf[1 << CD_SECTOR_SHIFT];

	DEL_ChangeCDMode(0);

	remainingDataToRead = size & ((1 << CD_SECTOR_SHIFT) - 1);
	numSectorsToRead = size >> CD_SECTOR_SHIFT;

	if (numSectorsToRead != 0)
	{
		CdIntToPos(cdCurrentSector, &fp.pos);
		CdControlB(CdlSetloc, (unsigned char*)&fp, NULL);

		CdRead(numSectorsToRead, (unsigned long*)addr, CdlModeSpeed);
		
		//loc_5E48C
		while (CdReadSync(1, 0) > 0)
		{
			VSync(0);
		}

		cdCurrentSector += numSectorsToRead;

	}//loc_5E4AC

	if (remainingDataToRead != 0)
	{
		
		CdIntToPos(cdCurrentSector, &fp.pos);
		CdControlB(CdlSetloc, (unsigned char*)&fp, NULL);
		CdRead(1, (unsigned long*)&buf[0], CdlModeSpeed);

		while (CdReadSync(1, 0) > 0)
		{
			VSync(0);
		}

		memcpy(&addr[numSectorsToRead << CD_SECTOR_SHIFT], &buf, remainingDataToRead);

		cdCurrentSector++;
	}

	return 0;
}

/*
* [FUNCTIONALITY] - DEL_CDFS_Seek
* Seeks from the cd reader's current position.
* Note: Negative numbers will allow backwards traversal.
* [USAGE]
* @PARAM - [offset] the number of bytes you wish to seek (not in sectors).
*/

int DEL_CDFS_Seek(int offset /*$a0*/)//*, 5E54C(<) (F) (*) (D)
{
	cdCurrentSector = cdStartSector + (offset >> CD_SECTOR_SHIFT);
	return 0;
}

/*
* [FUNCTIONALITY] - FRIG_CD_POS_TO_CUR
* Updates the cd reader's start sector to current reader position.
*/

void FRIG_CD_POS_TO_CUR()//*, 5E564(<) (F) (*)
{
	cdStartSector = cdCurrentSector;
}

#if PLAY_FMVS
void sub_5E708()
{
	CdlFILE fp;
	char buff[80];
	int i;

	//loc_5E738
	for(i = 0; i < 8; i++)
	{
		sprintf(&buff[0], "\\FMV%d.STR;1", i);
		if (CdSearchFile(&fp, &buff[0]))
		{
			//loc_5E764
			dword_A5660[i] = CdPosToInt(&fp.pos);
		}
		else
		{
			dword_A5660[i] = -1;
		}
	}
}
#endif