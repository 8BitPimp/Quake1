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
// gl_mesh.c: triangle model functions

#include "quakedef.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

static model_t* aliasmodel;
static aliashdr_t* paliashdr;

static byte used[8192];

// the command list holds counts and s/t values that are valid for
// every frame
static int commands[8192];
static int numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
static int vertexorder[8192];
static int numorder;

static int allverts, alltris;

static int stripverts[128];
static int striptris[128];
static int stripcount;

/*
================
StripLength
================
*/
int StripLength(int starttri, int startv)
{
    int m1, m2;
    int j;
    mtriangle_t *last, *check;
    int k;

    used[starttri] = 2;

    last = &triangles[starttri];

    stripverts[0] = last->vertindex[(startv) % 3];
    stripverts[1] = last->vertindex[(startv + 1) % 3];
    stripverts[2] = last->vertindex[(startv + 2) % 3];

    striptris[0] = starttri;
    stripcount = 1;

    m1 = last->vertindex[(startv + 2) % 3];
    m2 = last->vertindex[(startv + 1) % 3];

// look for a matching triangle
nexttri:
    for (j = starttri + 1, check = &triangles[starttri + 1]; j < pheader->numtris; j++, check++)
    {
        if (check->facesfront != last->facesfront)
            continue;
        for (k = 0; k < 3; k++)
        {
            if (check->vertindex[k] != m1)
                continue;
            if (check->vertindex[(k + 1) % 3] != m2)
                continue;

            // this is the next part of the fan

            // if we can't use this triangle, this tristrip is done
            if (used[j])
                goto done;

            // the new edge
            if (stripcount & 1)
                m2 = check->vertindex[(k + 2) % 3];
            else
                m1 = check->vertindex[(k + 2) % 3];

            stripverts[stripcount + 2] = check->vertindex[(k + 2) % 3];
            striptris[stripcount] = j;
            stripcount++;

            used[j] = 2;
            goto nexttri;
        }
    }
done:

    // clear the temp used flags
    for (j = starttri + 1; j < pheader->numtris; j++)
        if (used[j] == 2)
            used[j] = 0;

    return stripcount;
}

/*
===========
FanLength
===========
*/
int FanLength(int starttri, int startv)
{
    int m1, m2;
    int j;
    mtriangle_t *last, *check;
    int k;

    used[starttri] = 2;

    last = &triangles[starttri];

    stripverts[0] = last->vertindex[(startv) % 3];
    stripverts[1] = last->vertindex[(startv + 1) % 3];
    stripverts[2] = last->vertindex[(startv + 2) % 3];

    striptris[0] = starttri;
    stripcount = 1;

    m1 = last->vertindex[(startv + 0) % 3];
    m2 = last->vertindex[(startv + 2) % 3];

// look for a matching triangle
nexttri:
    for (j = starttri + 1, check = &triangles[starttri + 1]; j < pheader->numtris; j++, check++)
    {
        if (check->facesfront != last->facesfront)
            continue;
        for (k = 0; k < 3; k++)
        {
            if (check->vertindex[k] != m1)
                continue;
            if (check->vertindex[(k + 1) % 3] != m2)
                continue;

            // this is the next part of the fan

            // if we can't use this triangle, this tristrip is done
            if (used[j])
                goto done;

            // the new edge
            m2 = check->vertindex[(k + 2) % 3];

            stripverts[stripcount + 2] = m2;
            striptris[stripcount] = j;
            stripcount++;

            used[j] = 2;
            goto nexttri;
        }
    }
done:

    // clear the temp used flags
    for (j = starttri + 1; j < pheader->numtris; j++)
        if (used[j] == 2)
            used[j] = 0;

    return stripcount;
}

/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
void BuildTris(void)
{
    int i, j, k;
    int startv;
    float s, t;
    int len, bestlen, besttype;
    int bestverts[1024];
    int besttris[1024];
    int type;

    //
    // build tristrips
    //
    numorder = 0;
    numcommands = 0;
    memset(used, 0, sizeof(used));
    for (i = 0; i < pheader->numtris; i++)
    {
        // pick an unused triangle and start the trifan
        if (used[i])
            continue;

        bestlen = 0;
        for (type = 0; type < 2; type++)
        //	type = 1;
        {
            for (startv = 0; startv < 3; startv++)
            {
                if (type == 1)
                    len = StripLength(i, startv);
                else
                    len = FanLength(i, startv);
                if (len > bestlen)
                {
                    besttype = type;
                    bestlen = len;
                    for (j = 0; j < bestlen + 2; j++)
                        bestverts[j] = stripverts[j];
                    for (j = 0; j < bestlen; j++)
                        besttris[j] = striptris[j];
                }
            }
        }

        // mark the tris on the best strip as used
        for (j = 0; j < bestlen; j++)
            used[besttris[j]] = 1;

        if (besttype == 1)
            commands[numcommands++] = (bestlen + 2);
        else
            commands[numcommands++] = -(bestlen + 2);

        for (j = 0; j < bestlen + 2; j++)
        {
            // emit a vertex into the reorder buffer
            k = bestverts[j];
            vertexorder[numorder++] = k;

            // emit s/t coords into the commands stream
            s = stverts[k].s;
            t = stverts[k].t;
            if (!triangles[besttris[0]].facesfront && stverts[k].onseam)
                s += pheader->skinwidth / 2; // on back side
            s = (s + 0.5) / pheader->skinwidth;
            t = (t + 0.5) / pheader->skinheight;

            *(float*)&commands[numcommands++] = s;
            *(float*)&commands[numcommands++] = t;
        }
    }

    commands[numcommands++] = 0; // end of list marker

    Con_DPrintf("%3i tri %3i vert %3i cmd\n", pheader->numtris, numorder, numcommands);

    allverts += numorder;
    alltris += pheader->numtris;
}

/*
================
GL_MakeAliasModelDisplayLists
================
*/
void GL_MakeAliasModelDisplayLists(model_t* m, aliashdr_t* hdr)
{
    int i, j;
    int* cmds;
    trivertx_t* verts;
    float hscale, vscale; //johnfitz -- padded skins
    int count; //johnfitz -- precompute texcoords for padded skins
    int* loadcmds; //johnfitz

    //johnfitz -- padded skins
    hscale = (float)hdr->skinwidth / (float)TexMgr_PadConditional(hdr->skinwidth);
    vscale = (float)hdr->skinheight / (float)TexMgr_PadConditional(hdr->skinheight);
    //johnfitz

    aliasmodel = m;
    paliashdr = hdr; // (aliashdr_t *)Mod_Extradata (m);

//johnfitz -- generate meshes

#if 1 //always regenerate meshes

    Con_DPrintf("meshing %s...\n", m->name);
    BuildTris();

#else //conditional regeneration

    if (gl_alwaysmesh.value) // build it from scratch, and don't bother saving it to disk
    {
        Con_DPrintf("meshing %s...\n", m->name);
        BuildTris();
    }
    else // check disk cache, and rebuild it and save to disk if necessary
    {

        //create directories
        sprintf(gldir, "%s/glquake", com_gamedir);
        Sys_mkdir(com_gamedir);
        Sys_mkdir(gldir);

        //
        // look for a cached version
        //
        strcpy(cache, "glquake/");
        COM_StripExtension(m->name + strlen("progs/"), cache + strlen("glquake/"));
        strcat(cache, ".ms2");

        COM_FOpenFile(cache, &f);
        if (f)
        {
            fread(&numcommands, 4, 1, f);
            fread(&numorder, 4, 1, f);
            fread(&commands, numcommands * sizeof(commands[0]), 1, f);
            fread(&vertexorder, numorder * sizeof(vertexorder[0]), 1, f);
            fclose(f);
        }
        else
        {
            //
            // build it from scratch
            //
            Con_Printf("meshing %s...\n", m->name);
            BuildTris();

            //
            // save out the cached version
            //
            sprintf(fullpath, "%s/%s", com_gamedir, cache);
            f = fopen(fullpath, "wb");
            if (f)
            {
                fwrite(&numcommands, 4, 1, f);
                fwrite(&numorder, 4, 1, f);
                fwrite(&commands, numcommands * sizeof(commands[0]), 1, f);
                fwrite(&vertexorder, numorder * sizeof(vertexorder[0]), 1, f);
                fclose(f);
            }
        }
    }
#endif
    //johnfitz

    // save the data out

    paliashdr->poseverts = numorder;

    cmds = Hunk_Alloc(numcommands * 4);
    paliashdr->commands = (byte*)cmds - (byte*)paliashdr;

    //johnfitz -- precompute texcoords for padded skins
    loadcmds = commands;
    while (1)
    {
        *cmds++ = count = *loadcmds++;

        if (!count)
            break;

        if (count < 0)
            count = -count;

        do
        {
            *(float*)cmds++ = hscale * (*(float*)loadcmds++);
            *(float*)cmds++ = vscale * (*(float*)loadcmds++);
        } while (--count);
    }
    //johnfitz

    verts = Hunk_Alloc(paliashdr->numposes * paliashdr->poseverts * sizeof(trivertx_t));
    paliashdr->posedata = (byte*)verts - (byte*)paliashdr;
    for (i = 0; i < paliashdr->numposes; i++)
        for (j = 0; j < numorder; j++)
            *verts++ = poseverts[i][vertexorder[j]];
}