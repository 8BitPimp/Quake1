/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// wad.c

#include "quakedef.h"

int wad_numlumps;
lumpinfo_t* wad_lumps;
byte* wad_base = NULL; //johnfitz -- set to null

void SwapPic(qpic_t* pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void W_CleanupName(char* in, char* out)
{
    int i;
    int c;

    for (i = 0; i < 16; i++)
    {
        c = in[i];
        if (!c)
            break;

        if (c >= 'A' && c <= 'Z')
            c += ('a' - 'A');
        out[i] = c;
    }

    for (; i < 16; i++)
        out[i] = 0;
}

/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile(void) //johnfitz -- filename is now hard-coded for honesty
{
    lumpinfo_t* lump_p;
    wadinfo_t* header;
    unsigned i;
    int infotableofs;
    char* filename = WADFILENAME;

    //johnfitz -- modified to use malloc
    //TODO: use cache_alloc
    int h, len;

    Draw_BeginDisc();
    free(wad_base);
    len = COM_OpenFile(filename, &h);
    if (h == -1)
        Sys_Error("W_LoadWadFile: couldn't load %s", filename);
    wad_base = (unsigned char*)malloc(len);
    Sys_FileRead(h, wad_base, len);
    COM_CloseFile(h);
    //johnfitz

    header = (wadinfo_t*)wad_base;

    if (header->identification[0] != 'W' || header->identification[1] != 'A'
        || header->identification[2] != 'D' || header->identification[3] != '2')
        Sys_Error("Wad file %s doesn't have WAD2 id\n", filename);

    wad_numlumps = LittleLong(header->numlumps);
    infotableofs = LittleLong(header->infotableofs);
    wad_lumps = (lumpinfo_t*)(wad_base + infotableofs);

    for (i = 0, lump_p = wad_lumps; i < wad_numlumps; i++, lump_p++)
    {
        lump_p->filepos = LittleLong(lump_p->filepos);
        lump_p->size = LittleLong(lump_p->size);
        W_CleanupName(lump_p->name, lump_p->name);
        if (lump_p->type == TYP_QPIC)
            SwapPic((qpic_t*)(wad_base + lump_p->filepos));
    }
}

/*
=============
W_GetLumpinfo
=============
*/
lumpinfo_t* W_GetLumpinfo(char* name)
{
    int i;
    lumpinfo_t* lump_p;
    char clean[16];

    W_CleanupName(name, clean);

    for (lump_p = wad_lumps, i = 0; i < wad_numlumps; i++, lump_p++)
    {
        if (!strcmp(clean, lump_p->name))
            return lump_p;
    }

    Con_SafePrintf("W_GetLumpinfo: %s not found\n", name); //johnfitz -- was Sys_Error
    return NULL;
}

void* W_GetLumpName(char* name)
{
    lumpinfo_t* lump;

    lump = W_GetLumpinfo(name);

    if (!lump)
        return NULL; //johnfitz

    return (void*)(wad_base + lump->filepos);
}

void* W_GetLumpNum(int num)
{
    lumpinfo_t* lump;

    if (num < 0 || num > wad_numlumps)
        Sys_Error("W_GetLumpNum: bad number: %i", num);

    lump = wad_lumps + num;

    return (void*)(wad_base + lump->filepos);
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic(qpic_t* pic)
{
    pic->width = LittleLong(pic->width);
    pic->height = LittleLong(pic->height);
}
