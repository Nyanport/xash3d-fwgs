/*
gl_sprite.c - sprite rendering
Copyright (C) 2010 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "gl_local.h"
#include "pm_local.h"
#include "sprite.h"
#include "studio.h"
#include "entity_types.h"
#include "cl_tent.h"

// it's a Valve default value for LoadMapSprite (probably must be power of two)
#define MAPSPRITE_SIZE	128
#define GLARE_FALLOFF	19000.0f

convar_t		*r_sprite_lerping;
convar_t		*r_sprite_lighting;
char		sprite_name[MAX_QPATH];
char		group_suffix[8];
static uint	r_texFlags = 0;
static int	sprite_version;
float		sprite_radius;

/*
====================
R_SpriteInit

====================
*/
void R_SpriteInit( void )
{
	r_sprite_lerping = Cvar_Get( "r_sprite_lerping", "1", FCVAR_ARCHIVE, "enables sprite animation lerping" );
	r_sprite_lighting = Cvar_Get( "r_sprite_lighting", "1", FCVAR_ARCHIVE, "enables sprite lighting (blood etc)" );
}

/*
====================
R_SpriteLoadFrame

upload a single frame
====================
*/
static dframetype_t *R_SpriteLoadFrame( model_t *mod, void *pin, mspriteframe_t **ppframe, int num )
{
	dspriteframe_t	*pinframe;
	mspriteframe_t	*pspriteframe;
	int		gl_texturenum = 0;
	char		texname[128];
	int		bytes = 1;

	pinframe = (dspriteframe_t *)pin;
	if( sprite_version == SPRITE_VERSION_32 )
		bytes = 4;

	// build uinque frame name
	if( FBitSet( mod->flags, MODEL_CLIENT )) // it's a HUD sprite
	{
		Q_snprintf( texname, sizeof( texname ), "#HUD/%s(%s:%i%i).spr", sprite_name, group_suffix, num / 10, num % 10 );
		gl_texturenum = GL_LoadTexture( texname, pin, pinframe->width * pinframe->height * bytes, r_texFlags, NULL );
	}
	else
	{
		Q_snprintf( texname, sizeof( texname ), "#%s(%s:%i%i).spr", sprite_name, group_suffix, num / 10, num % 10 );
		gl_texturenum = GL_LoadTexture( texname, pin, pinframe->width * pinframe->height * bytes, r_texFlags, NULL );
	}	

	// setup frame description
	pspriteframe = Mem_Malloc( mod->mempool, sizeof( mspriteframe_t ));
	pspriteframe->width = pinframe->width;
	pspriteframe->height = pinframe->height;
	pspriteframe->up = pinframe->origin[1];
	pspriteframe->left = pinframe->origin[0];
	pspriteframe->down = pinframe->origin[1] - pinframe->height;
	pspriteframe->right = pinframe->width + pinframe->origin[0];
	pspriteframe->gl_texturenum = gl_texturenum;
	*ppframe = pspriteframe;

	return (dframetype_t *)((byte *)(pinframe + 1) + pinframe->width * pinframe->height * bytes );
}

/*
====================
R_SpriteLoadGroup

upload a group frames
====================
*/
static dframetype_t *R_SpriteLoadGroup( model_t *mod, void *pin, mspriteframe_t **ppframe, int framenum )
{
	dspritegroup_t	*pingroup;
	mspritegroup_t	*pspritegroup;
	dspriteinterval_t	*pin_intervals;
	float		*poutintervals;
	int		i, groupsize, numframes;
	void		*ptemp;

	pingroup = (dspritegroup_t *)pin;
	numframes = pingroup->numframes;

	groupsize = sizeof( mspritegroup_t ) + (numframes - 1) * sizeof( pspritegroup->frames[0] );
	pspritegroup = Mem_Calloc( mod->mempool, groupsize );
	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;
	pin_intervals = (dspriteinterval_t *)(pingroup + 1);
	poutintervals = Mem_Calloc( mod->mempool, numframes * sizeof( float ));
	pspritegroup->intervals = poutintervals;

	for( i = 0; i < numframes; i++ )
	{
		*poutintervals = pin_intervals->interval;
		if( *poutintervals <= 0.0f )
			*poutintervals = 1.0f; // set error value
		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;
	for( i = 0; i < numframes; i++ )
	{
		ptemp = R_SpriteLoadFrame( mod, ptemp, &pspritegroup->frames[i], framenum * 10 + i );
	}

	return (dframetype_t *)ptemp;
}

/*
====================
Mod_LoadSpriteModel

load sprite model
====================
*/
void Mod_LoadSpriteModel( model_t *mod, const void *buffer, qboolean *loaded, uint texFlags )
{
	dsprite_q1_t	*pinq1;
	dsprite_hl_t	*pinhl;
	dsprite_t		*pin;
	short		*numi = NULL;
	dframetype_t	*pframetype;
	msprite_t		*psprite;
	int		i, size;

	if( loaded ) *loaded = false;
	pin = (dsprite_t *)buffer;
	mod->type = mod_sprite;
	r_texFlags = texFlags;
	i = pin->version;

	if( pin->ident != IDSPRITEHEADER )
	{
		MsgDev( D_ERROR, "%s has wrong id (%x should be %x)\n", mod->name, pin->ident, IDSPRITEHEADER );
		return;
	}
		
	if( i != SPRITE_VERSION_Q1 && i != SPRITE_VERSION_HL && i != SPRITE_VERSION_32 )
	{
		MsgDev( D_ERROR, "%s has wrong version number (%i should be %i or %i)\n", mod->name, i, SPRITE_VERSION_Q1, SPRITE_VERSION_HL );
		return;
	}

	mod->mempool = Mem_AllocPool( va( "^2%s^7", mod->name ));
	sprite_version = i;

	if( i == SPRITE_VERSION_Q1 || i == SPRITE_VERSION_32 )
	{
		pinq1 = (dsprite_q1_t *)buffer;
		size = sizeof( msprite_t ) + ( pinq1->numframes - 1 ) * sizeof( psprite->frames );
		psprite = Mem_Calloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinq1->type;
		psprite->texFormat = SPR_ADDITIVE;	//SPR_ALPHTEST;
		psprite->numframes = mod->numframes = pinq1->numframes;
		psprite->facecull = SPR_CULL_FRONT;
		psprite->radius = pinq1->boundingradius;
		psprite->synctype = pinq1->synctype;

		mod->mins[0] = mod->mins[1] = -pinq1->bounds[0] * 0.5f;
		mod->maxs[0] = mod->maxs[1] = pinq1->bounds[0] * 0.5f;
		mod->mins[2] = -pinq1->bounds[1] * 0.5f;
		mod->maxs[2] = pinq1->bounds[1] * 0.5f;
		numi = NULL;
	}
	else if( i == SPRITE_VERSION_HL )
	{
		pinhl = (dsprite_hl_t *)buffer;
		size = sizeof( msprite_t ) + ( pinhl->numframes - 1 ) * sizeof( psprite->frames );
		psprite = Mem_Calloc( mod->mempool, size );
		mod->cache.data = psprite;	// make link to extradata

		psprite->type = pinhl->type;
		psprite->texFormat = pinhl->texFormat;
		psprite->numframes = mod->numframes = pinhl->numframes;
		psprite->facecull = pinhl->facetype;
		psprite->radius = pinhl->boundingradius;
		psprite->synctype = pinhl->synctype;

		mod->mins[0] = mod->mins[1] = -pinhl->bounds[0] * 0.5f;
		mod->maxs[0] = mod->maxs[1] = pinhl->bounds[0] * 0.5f;
		mod->mins[2] = -pinhl->bounds[1] * 0.5f;
		mod->maxs[2] = pinhl->bounds[1] * 0.5f;
		numi = (short *)(pinhl + 1);
	}

	if( host.type == HOST_DEDICATED )
	{
		// skip frames loading
		if( loaded ) *loaded = true;	// done
		psprite->numframes = 0;
		return;
	}

	Q_strncpy( sprite_name, mod->name, sizeof( sprite_name ));
	COM_StripExtension( sprite_name );

	if( numi == NULL )
	{
		rgbdata_t	*pal;
	
		pal = FS_LoadImage( "#id.pal", (byte *)&i, 768 );
		pframetype = (dframetype_t *)(pinq1 + 1);
		FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else if( *numi == 256 )
	{	
		byte	*src = (byte *)(numi+1);
		rgbdata_t	*pal;
	
		// install palette
		switch( psprite->texFormat )
		{
                    case SPR_INDEXALPHA:
			pal = FS_LoadImage( "#gradient.pal", src, 768 ); 
			break;
		case SPR_ALPHTEST:		
			pal = FS_LoadImage( "#masked.pal", src, 768 );
                              break;
		default:
			pal = FS_LoadImage( "#normal.pal", src, 768 );
			break;
		}

		pframetype = (dframetype_t *)(src + 768);
		FS_FreeImage( pal ); // palette installed, no reason to keep this data
	}
	else 
	{
		MsgDev( D_ERROR, "%s has wrong number of palette colors %i (should be 256)\n", mod->name, *numi );
		return;
	}

	if( mod->numframes < 1 )
	{
		MsgDev( D_ERROR, "%s has invalid # of frames: %d\n", mod->name, mod->numframes );
		return;
	}

	for( i = 0; i < mod->numframes; i++ )
	{
		frametype_t frametype = pframetype->type;
		psprite->frames[i].type = frametype;

		switch( frametype )
		{
		case FRAME_SINGLE:
			Q_strncpy( group_suffix, "frame", sizeof( group_suffix ));
			pframetype = R_SpriteLoadFrame( mod, pframetype + 1, &psprite->frames[i].frameptr, i );
			break;
		case FRAME_GROUP:
			Q_strncpy( group_suffix, "group", sizeof( group_suffix ));
			pframetype = R_SpriteLoadGroup( mod, pframetype + 1, &psprite->frames[i].frameptr, i );
			break;
		case FRAME_ANGLED:
			Q_strncpy( group_suffix, "angle", sizeof( group_suffix ));
			pframetype = R_SpriteLoadGroup( mod, pframetype + 1, &psprite->frames[i].frameptr, i );
			break;
		}
		if( pframetype == NULL ) break; // technically an error
	}

	if( loaded ) *loaded = true;	// done
}

/*
====================
Mod_LoadMapSprite

Loading a bitmap image as sprite with multiple frames
as pieces of input image
====================
*/
void Mod_LoadMapSprite( model_t *mod, const void *buffer, size_t size, qboolean *loaded )
{
	byte		*src, *dst;
	rgbdata_t		*pix, temp;
	char		texname[128];
	int		i, j, x, y, w, h;
	int		xl, yl, xh, yh;
	int		linedelta, numframes;
	mspriteframe_t	*pspriteframe;
	msprite_t		*psprite;

	if( loaded ) *loaded = false;
	Q_snprintf( texname, sizeof( texname ), "#%s", mod->name );
	Image_SetForceFlags( IL_OVERVIEW );
	pix = FS_LoadImage( texname, buffer, size );
	Image_ClearForceFlags();
	if( !pix ) return;	// bad image or something else

	mod->type = mod_sprite;
	r_texFlags = 0; // no custom flags for map sprites

	if( pix->width % MAPSPRITE_SIZE )
		w = pix->width - ( pix->width % MAPSPRITE_SIZE );
	else w = pix->width;

	if( pix->height % MAPSPRITE_SIZE )
		h = pix->height - ( pix->height % MAPSPRITE_SIZE );
	else h = pix->height;

	if( w < MAPSPRITE_SIZE ) w = MAPSPRITE_SIZE;
	if( h < MAPSPRITE_SIZE ) h = MAPSPRITE_SIZE;

	// resample image if needed
	Image_Process( &pix, w, h, IMAGE_FORCE_RGBA|IMAGE_RESAMPLE, NULL );

	w = h = MAPSPRITE_SIZE;

	// check range
	if( w > pix->width ) w = pix->width;
	if( h > pix->height ) h = pix->height;

	// determine how many frames we needs
	numframes = (pix->width * pix->height) / (w * h);
	mod->mempool = Mem_AllocPool( va( "^2%s^7", mod->name ));
	psprite = Mem_Calloc( mod->mempool, sizeof( msprite_t ) + ( numframes - 1 ) * sizeof( psprite->frames ));
	mod->cache.data = psprite;	// make link to extradata

	psprite->type = SPR_FWD_PARALLEL_ORIENTED;
	psprite->texFormat = SPR_ALPHTEST;
	psprite->numframes = mod->numframes = numframes;
	psprite->radius = sqrt(((w >> 1) * (w >> 1)) + ((h >> 1) * (h >> 1)));

	mod->mins[0] = mod->mins[1] = -w / 2;
	mod->maxs[0] = mod->maxs[1] = w / 2;
	mod->mins[2] = -h / 2;
	mod->maxs[2] = h / 2;

	// create a temporary pic
	memset( &temp, 0, sizeof( temp ));
	temp.width = w;
	temp.height = h;
	temp.type = pix->type;
	temp.flags = pix->flags;	
	temp.size = w * h * PFDesc[temp.type].bpp;
	temp.buffer = Mem_Malloc( r_temppool, temp.size );
	temp.palette = NULL;

	// chop the image and upload into video memory
	for( i = xl = yl = 0; i < numframes; i++ )
	{
		xh = xl + w;
		yh = yl + h;

		src = pix->buffer + ( yl * pix->width + xl ) * 4;
		linedelta = ( pix->width - w ) * 4;
		dst = temp.buffer;

		// cut block from source
		for( y = yl; y < yh; y++ )
		{
			for( x = xl; x < xh; x++ )
				for( j = 0; j < 4; j++ )
					*dst++ = *src++;
			src += linedelta;
		}

		// build uinque frame name
		Q_snprintf( texname, sizeof( texname ), "#MAP/%s_%i%i.spr", mod->name, i / 10, i % 10 );

		psprite->frames[i].frameptr = Mem_Calloc( mod->mempool, sizeof( mspriteframe_t ));
		pspriteframe = psprite->frames[i].frameptr;
		pspriteframe->width = w;
		pspriteframe->height = h;
		pspriteframe->up = ( h >> 1 );
		pspriteframe->left = -( w >> 1 );
		pspriteframe->down = ( h >> 1 ) - h;
		pspriteframe->right = w + -( w >> 1 );
		pspriteframe->gl_texturenum = GL_LoadTextureInternal( texname, &temp, TF_IMAGE );

		xl += w;
		if( xl >= pix->width )
		{
			xl = 0;
			yl += h;
		}
	}

	FS_FreeImage( pix );
	Mem_Free( temp.buffer );

	if( loaded ) *loaded = true;
}

/*
====================
Mod_UnloadSpriteModel

release sprite model and frames
====================
*/
void Mod_UnloadSpriteModel( model_t *mod )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;	
	mspriteframe_t	*pspriteframe;
	int		i, j;

	Assert( mod != NULL );

	if( mod->type == mod_sprite )
	{
		if( host.type != HOST_DEDICATED )
		{
			psprite = mod->cache.data;

			if( psprite )
			{
				// release all textures
				for( i = 0; i < psprite->numframes; i++ )
				{
					if( psprite->frames[i].type == SPR_SINGLE )
					{
						pspriteframe = psprite->frames[i].frameptr;
						GL_FreeTexture( pspriteframe->gl_texturenum );
					}
					else
					{
						pspritegroup = (mspritegroup_t *)psprite->frames[i].frameptr;

						for( j = 0; j < pspritegroup->numframes; j++ )
						{
							pspriteframe = pspritegroup->frames[i];
							GL_FreeTexture( pspriteframe->gl_texturenum );
						}
					}
				}
			}
		}
	}

	Mem_FreePool( &mod->mempool );
	memset( mod, 0, sizeof( *mod ));
}

/*
================
R_GetSpriteFrame

assume pModel is valid
================
*/
mspriteframe_t *R_GetSpriteFrame( const model_t *pModel, int frame, float yaw )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe = NULL;
	float		*pintervals, fullinterval;
	int		i, numframes;
	float		targettime;

	Assert( pModel != NULL );
	psprite = pModel->cache.data;

	if( frame < 0 )
	{
		frame = 0;
	}
	else if( frame >= psprite->numframes )
	{
		if( frame > psprite->numframes )
			Con_Reportf( S_WARN "R_GetSpriteFrame: no such frame %d (%s)\n", frame, pModel->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == SPR_SINGLE )
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == SPR_GROUP ) 
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = cl.time - ((int)( cl.time / fullinterval )) * fullinterval;

		for( i = 0; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
		}
		pspriteframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		int	angleframe = (int)(Q_rint(( RI.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		// e.g. doom-style sprite monsters
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pspriteframe = pspritegroup->frames[angleframe];
	}

	return pspriteframe;
}

/*
================
R_GetSpriteFrameInterpolant

NOTE: we using prevblending[0] and [1] for holds interval
between frames where are we lerping
================
*/
float R_GetSpriteFrameInterpolant( cl_entity_t *ent, mspriteframe_t **oldframe, mspriteframe_t **curframe )
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	int		i, j, numframes, frame;
	float		lerpFrac, time, jtime, jinterval;
	float		*pintervals, fullinterval, targettime;
	int		m_fDoInterp;

	psprite = ent->model->cache.data;
	frame = (int)ent->curstate.frame;
	lerpFrac = 1.0f;

	// misc info
	m_fDoInterp = (ent->curstate.effects & EF_NOINTERP) ? false : true;

	if( frame < 0 )
	{
		frame = 0;
	}          
	else if( frame >= psprite->numframes )
	{
		Con_Reportf( S_WARN "R_GetSpriteFrameInterpolant: no such frame %d (%s)\n", frame, ent->model->name );
		frame = psprite->numframes - 1;
	}

	if( psprite->frames[frame].type == FRAME_SINGLE )
	{
		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_SINGLE )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = cl.time;
				lerpFrac = 1.0f;
			}
                              
			if( ent->latched.sequencetime < cl.time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = cl.time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (cl.time - ent->latched.sequencetime) * 11.0f;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = cl.time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		if( ent->latched.prevblending[0] >= psprite->numframes )
		{
			// reset interpolation on change model
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			ent->latched.sequencetime = cl.time;
			lerpFrac = 0.0f;
		}

		// get the interpolated frames
		if( oldframe ) *oldframe = psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( curframe ) *curframe = psprite->frames[frame].frameptr;
	}
	else if( psprite->frames[frame].type == FRAME_GROUP ) 
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];
		jinterval = pintervals[1] - pintervals[0];
		time = cl.time;
		jtime = 0.0f;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by zero
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		// LordHavoc: since I can't measure the time properly when it loops from numframes - 1 to 0,
		// i instead measure the time of the first frame, hoping it is consistent
		for( i = 0, j = numframes - 1; i < (numframes - 1); i++ )
		{
			if( pintervals[i] > targettime )
				break;
			j = i;
			jinterval = pintervals[i] - jtime;
			jtime = pintervals[i];
		}

		if( m_fDoInterp )
			lerpFrac = (targettime - jtime) / jinterval;
		else j = i; // no lerping

		// get the interpolated frames
		if( oldframe ) *oldframe = pspritegroup->frames[j];
		if( curframe ) *curframe = pspritegroup->frames[i];
	}
	else if( psprite->frames[frame].type == FRAME_ANGLED )
	{
		// e.g. doom-style sprite monsters
		float	yaw = ent->angles[YAW];
		int	angleframe = (int)(Q_rint(( RI.viewangles[1] - yaw + 45.0f ) / 360 * 8) - 4) & 7;

		if( m_fDoInterp )
		{
			if( ent->latched.prevblending[0] >= psprite->numframes || psprite->frames[ent->latched.prevblending[0]].type != FRAME_ANGLED )
			{
				// this can be happens when rendering switched between single and angled frames
				// or change model on replace delta-entity
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = cl.time;
				lerpFrac = 1.0f;
			}

			if( ent->latched.sequencetime < cl.time )
			{
				if( frame != ent->latched.prevblending[1] )
				{
					ent->latched.prevblending[0] = ent->latched.prevblending[1];
					ent->latched.prevblending[1] = frame;
					ent->latched.sequencetime = cl.time;
					lerpFrac = 0.0f;
				}
				else lerpFrac = (cl.time - ent->latched.sequencetime) * ent->curstate.framerate;
			}
			else
			{
				ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
				ent->latched.sequencetime = cl.time;
				lerpFrac = 0.0f;
			}
		}
		else
		{
			ent->latched.prevblending[0] = ent->latched.prevblending[1] = frame;
			lerpFrac = 1.0f;
		}

		pspritegroup = (mspritegroup_t *)psprite->frames[ent->latched.prevblending[0]].frameptr;
		if( oldframe ) *oldframe = pspritegroup->frames[angleframe];

		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		if( curframe ) *curframe = pspritegroup->frames[angleframe];
	}

	return lerpFrac;
}

/*
================
R_CullSpriteModel

Cull sprite model by bbox
================
*/
qboolean R_CullSpriteModel( cl_entity_t *e, vec3_t origin )
{
	vec3_t	sprite_mins, sprite_maxs;
	float	scale = 1.0f;

	if( !e->model->cache.data )
		return true;

	if( e->curstate.scale > 0.0f )
		scale = e->curstate.scale;

	// scale original bbox (no rotation for sprites)
	VectorScale( e->model->mins, scale, sprite_mins );
	VectorScale( e->model->maxs, scale, sprite_maxs );

	sprite_radius = RadiusFromBounds( sprite_mins, sprite_maxs );

	VectorAdd( sprite_mins, origin, sprite_mins );
	VectorAdd( sprite_maxs, origin, sprite_maxs );

	return R_CullModel( e, sprite_mins, sprite_maxs );
}

/*
================
R_GlowSightDistance

Set sprite brightness factor
================
*/
static float R_SpriteGlowBlend( vec3_t origin, int rendermode, int renderfx, float *pscale )
{
	float	dist, brightness;
	vec3_t	glowDist;
	pmtrace_t	*tr;

	VectorSubtract( origin, RI.vieworg, glowDist );
	dist = VectorLength( glowDist );

	if( RP_NORMALPASS( ))
	{
		tr = CL_VisTraceLine( RI.vieworg, origin, r_traceglow->value ? PM_GLASS_IGNORE : (PM_GLASS_IGNORE|PM_STUDIO_IGNORE));

		if(( 1.0f - tr->fraction ) * dist > 8.0f )
			return 0.0f;
	}

	if( renderfx == kRenderFxNoDissipation )
		return 1.0f;

	brightness = GLARE_FALLOFF / ( dist * dist );
	brightness = bound( 0.05f, brightness, 1.0f );
	*pscale *= dist * ( 1.0f / 200.0f );

	return brightness;
}

/*
================
R_SpriteOccluded

Do occlusion test for glow-sprites
================
*/
qboolean R_SpriteOccluded( cl_entity_t *e, vec3_t origin, float *pscale )
{
	if( e->curstate.rendermode == kRenderGlow )
	{
		float	blend;
		vec3_t	v;

		TriWorldToScreen( origin, v );

		if( v[0] < RI.viewport[0] || v[0] > RI.viewport[0] + RI.viewport[2] )
			return true; // do scissor
		if( v[1] < RI.viewport[1] || v[1] > RI.viewport[1] + RI.viewport[3] )
			return true; // do scissor

		blend = R_SpriteGlowBlend( origin, e->curstate.rendermode, e->curstate.renderfx, pscale );
		tr.blend *= blend;

		if( blend <= 0.01f )
			return true; // faded
	}
	else
	{
		if( R_CullSpriteModel( e, origin ))
			return true;
	}

	return false;	
}

/*
=================
R_DrawSpriteQuad
=================
*/
static void R_DrawSpriteQuad( mspriteframe_t *frame, vec3_t org, vec3_t v_right, vec3_t v_up, float scale )
{
	vec3_t	point;

	r_stats.c_sprite_polys++;

	pglBegin( GL_QUADS );
		pglTexCoord2f( 0.0f, 1.0f );
		VectorMA( org, frame->down * scale, v_up, point );
		VectorMA( point, frame->left * scale, v_right, point );
		pglVertex3fv( point );
		pglTexCoord2f( 0.0f, 0.0f );
		VectorMA( org, frame->up * scale, v_up, point );
		VectorMA( point, frame->left * scale, v_right, point );
		pglVertex3fv( point );
		pglTexCoord2f( 1.0f, 0.0f );
		VectorMA( org, frame->up * scale, v_up, point );
		VectorMA( point, frame->right * scale, v_right, point );
		pglVertex3fv( point );
 	        	pglTexCoord2f( 1.0f, 1.0f );
		VectorMA( org, frame->down * scale, v_up, point );
		VectorMA( point, frame->right * scale, v_right, point );
		pglVertex3fv( point );
	pglEnd();
}

static qboolean R_SpriteHasLightmap( cl_entity_t *e, int texFormat )
{
	if( !r_sprite_lighting->value )
		return false;
	
	if( texFormat != SPR_ALPHTEST )
		return false;

	if( e->curstate.effects & EF_FULLBRIGHT )
		return false;

	if( e->curstate.renderamt <= 127 )
		return false;

	switch( e->curstate.rendermode )
	{
	case kRenderNormal:
	case kRenderTransAlpha:
	case kRenderTransTexture:
		break;
	default:
		return false;
	}

	return true;
}

/*
=================
R_SpriteAllowLerping
=================
*/
static qboolean R_SpriteAllowLerping( cl_entity_t *e, msprite_t *psprite )
{
	if( !r_sprite_lerping->value )
		return false;

	if( psprite->numframes <= 1 )
		return false;

	if( psprite->texFormat != SPR_ADDITIVE )
		return false;

	if( e->curstate.rendermode == kRenderNormal || e->curstate.rendermode == kRenderTransAlpha )
		return false;

	return true;
}

/*
=================
R_DrawSpriteModel
=================
*/
void R_DrawSpriteModel( cl_entity_t *e )
{
	mspriteframe_t	*frame, *oldframe;
	msprite_t		*psprite;
	model_t		*model;
	int		i, type;
	float		angle, dot, sr, cr;
	float		lerp = 1.0f, ilerp, scale;
	vec3_t		v_forward, v_right, v_up;
	vec3_t		origin, color, color2;

	if( RI.params & RP_ENVVIEW )
		return;

	model = e->model;
	psprite = (msprite_t * )model->cache.data;
	VectorCopy( e->origin, origin );	// set render origin

	// do movewith
	if( e->curstate.aiment > 0 && e->curstate.movetype == MOVETYPE_FOLLOW )
	{
		cl_entity_t	*parent;
	
		parent = CL_GetEntityByIndex( e->curstate.aiment );

		if( parent && parent->model )
		{
			if( parent->model->type == mod_studio && e->curstate.body > 0 )
			{
				int num = bound( 1, e->curstate.body, MAXSTUDIOATTACHMENTS );
				VectorCopy( parent->attachment[num-1], origin );
			}
			else VectorCopy( parent->origin, origin );
		}
	}

	scale = e->curstate.scale;
	if( !scale ) scale = 1.0f;

	if( R_SpriteOccluded( e, origin, &scale ))
		return; // sprite culled

	r_stats.c_sprite_models_drawn++;

	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( false );

	// select properly rendermode
	switch( e->curstate.rendermode )
	{
	case kRenderTransAlpha:
		pglDepthMask( GL_FALSE );
	case kRenderTransColor:
	case kRenderTransTexture:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		break;
	case kRenderGlow:
		pglDisable( GL_DEPTH_TEST );
	case kRenderTransAdd:
		pglEnable( GL_BLEND );
		pglBlendFunc( GL_SRC_ALPHA, GL_ONE );
		pglDepthMask( GL_FALSE );
		break;
	case kRenderNormal:
	default:
		pglDisable( GL_BLEND );
		break;
	}

	// all sprites can have color
	pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	pglEnable( GL_ALPHA_TEST );

	// NOTE: never pass sprites with rendercolor '0 0 0' it's a stupid Valve Hammer Editor bug
	if( e->curstate.rendercolor.r || e->curstate.rendercolor.g || e->curstate.rendercolor.b )
	{
		color[0] = (float)e->curstate.rendercolor.r * ( 1.0f / 255.0f );
		color[1] = (float)e->curstate.rendercolor.g * ( 1.0f / 255.0f );
		color[2] = (float)e->curstate.rendercolor.b * ( 1.0f / 255.0f );
	}
	else
	{
		color[0] = 1.0f;
		color[1] = 1.0f;
		color[2] = 1.0f;
	}
          
	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		colorVec lightColor = R_LightPoint( origin );
		// FIXME: collect light from dlights?
		color2[0] = (float)lightColor.r * ( 1.0f / 255.0f );
		color2[1] = (float)lightColor.g * ( 1.0f / 255.0f );
		color2[2] = (float)lightColor.b * ( 1.0f / 255.0f );
		// NOTE: sprites with 'lightmap' looks ugly when alpha func is GL_GREATER 0.0
		pglAlphaFunc( GL_GREATER, 0.5f );
	}

	if( R_SpriteAllowLerping( e, psprite ))
		lerp = R_GetSpriteFrameInterpolant( e, &oldframe, &frame );
	else frame = oldframe = R_GetSpriteFrame( model, e->curstate.frame, e->angles[YAW] );

	type = psprite->type;

	// automatically roll parallel sprites if requested
	if( e->angles[ROLL] != 0.0f && type == SPR_FWD_PARALLEL )
		type = SPR_FWD_PARALLEL_ORIENTED;

	switch( type )
	{
	case SPR_ORIENTED:
		AngleVectors( e->angles, v_forward, v_right, v_up );
		VectorScale( v_forward, 0.01f, v_forward );	// to avoid z-fighting
		VectorSubtract( origin, v_forward, origin );
		break;
	case SPR_FACING_UPRIGHT:
		VectorSet( v_right, origin[1] - RI.vieworg[1], -(origin[0] - RI.vieworg[0]), 0.0f );
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_UPRIGHT:
		dot = RI.vforward[2];
		if(( dot > 0.999848f ) || ( dot < -0.999848f ))	// cos(1 degree) = 0.999848
			return; // invisible
		VectorSet( v_up, 0.0f, 0.0f, 1.0f );
		VectorSet( v_right, RI.vforward[1], -RI.vforward[0], 0.0f );
		VectorNormalize( v_right );
		break;
	case SPR_FWD_PARALLEL_ORIENTED:
		angle = e->angles[ROLL] * (M_PI2 / 360.0f);
		SinCos( angle, &sr, &cr );
		for( i = 0; i < 3; i++ )
		{
			v_right[i] = (RI.vright[i] * cr + RI.vup[i] * sr);
			v_up[i] = RI.vright[i] * -sr + RI.vup[i] * cr;
		}
		break;
	case SPR_FWD_PARALLEL: // normal sprite
	default:
		VectorCopy( RI.vright, v_right ); 
		VectorCopy( RI.vup, v_up );
		break;
	}

	if( psprite->facecull == SPR_CULL_NONE )
		GL_Cull( GL_NONE );
		
	if( oldframe == frame )
	{
		// draw the single non-lerped frame
		pglColor4f( color[0], color[1], color[2], tr.blend );
		GL_Bind( XASH_TEXTURE0, frame->gl_texturenum );
		R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
	}
	else
	{
		// draw two combined lerped frames
		lerp = bound( 0.0f, lerp, 1.0f );
		ilerp = 1.0f - lerp;

		if( ilerp != 0.0f )
		{
			pglColor4f( color[0], color[1], color[2], tr.blend * ilerp );
			GL_Bind( XASH_TEXTURE0, oldframe->gl_texturenum );
			R_DrawSpriteQuad( oldframe, origin, v_right, v_up, scale );
		}

		if( lerp != 0.0f )
		{
			pglColor4f( color[0], color[1], color[2], tr.blend * lerp );
			GL_Bind( XASH_TEXTURE0, frame->gl_texturenum );
			R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
		}
	}

	// draw the sprite 'lightmap' :-)
	if( R_SpriteHasLightmap( e, psprite->texFormat ))
	{
		if( !r_lightmap->value )
			pglEnable( GL_BLEND );
		else pglDisable( GL_BLEND );
		pglDepthFunc( GL_EQUAL );
		pglDisable( GL_ALPHA_TEST );
		pglBlendFunc( GL_ZERO, GL_SRC_COLOR );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		pglColor4f( color2[0], color2[1], color2[2], tr.blend );
		GL_Bind( XASH_TEXTURE0, tr.whiteTexture );
		R_DrawSpriteQuad( frame, origin, v_right, v_up, scale );
		pglAlphaFunc( GL_GREATER, DEFAULT_ALPHATEST );
		pglDepthFunc( GL_LEQUAL );
	}

	if( psprite->facecull == SPR_CULL_NONE )
		GL_Cull( GL_FRONT );

	pglDisable( GL_ALPHA_TEST );
	pglDepthMask( GL_TRUE );

	if( e->curstate.rendermode == kRenderGlow || e->curstate.rendermode == kRenderTransAdd )
		R_AllowFog( true );

	if( e->curstate.rendermode != kRenderNormal )
	{
		pglDisable( GL_BLEND );
		pglTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
		pglEnable( GL_DEPTH_TEST );
	}
}