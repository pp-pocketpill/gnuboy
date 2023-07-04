/*
 * sdl.c
 * sdl interfaces -- based on svga.c
 *
 * (C) 2001 Damian Gryski <dgryski@uwaterloo.ca>
 *
 * Licensed under the GPLv2, or later.
 */

#include <stdlib.h>
#include <stdio.h>

#include <SDL/SDL.h>


#include "fb.h"
#include "input.h"
#include "rc.h"

extern void sdljoy_process_event(SDL_Event *event);

struct fb fb;

static int use_yuv = -1;
static int fullscreen = 0;
static int use_altenter = 1;

static SDL_Surface *screen;
static SDL_Surface *gb_screen;
static SDL_Overlay *overlay;
static SDL_Rect overlay_rect;

static int vmode[3] = { 240, 240, 16 };

rcvar_t vid_exports[] =
{
	//RCV_VECTOR("vmode", &vmode, 3, "video mode: w h bpp"),
	RCV_BOOL("yuv", &use_yuv, "try to use hardware YUV scaling"),
	RCV_BOOL("fullscreen", &fullscreen, "whether to start in fullscreen mode"),
	RCV_BOOL("altenter", &use_altenter, "alt-enter can toggle fullscreen"),
	RCV_END
};

/* keymap - mappings of the form { scancode, localcode } - from sdl/keymap.c */
extern int keymap[][2];

static int mapscancode(SDLKey sym)
{
	/* this could be faster:  */
	/*  build keymap as int keymap[256], then ``return keymap[sym]'' */

	int i;
	for (i = 0; keymap[i][0]; i++)
		if (keymap[i][0] == sym)
			return keymap[i][1];
	if (sym >= '0' && sym <= '9')
		return sym;
	if (sym >= 'a' && sym <= 'z')
		return sym;
	return 0;
}

static void overlay_init()
{
}

void vid_init()
{
	int flags;

	if (!vmode[0] || !vmode[1])
	{
		int scale = rc_getint("scale");
		if (scale < 1) scale = 1;
		vmode[0] = 160 * scale;
		vmode[1] = 144 * scale;
	}

	flags = SDL_ANYFORMAT | SDL_HWPALETTE | SDL_HWSURFACE;

	if (fullscreen)
		flags |= SDL_FULLSCREEN;

	if (SDL_Init(SDL_INIT_VIDEO))
		die("SDL: Couldn't initialize SDL: %s\n", SDL_GetError());

	if (!(screen = SDL_SetVideoMode(vmode[0], vmode[1], vmode[2], flags)))
		die("SDL: can't set video mode: %s\n", SDL_GetError());
	
	gb_screen = SDL_CreateRGBSurface(SDL_SWSURFACE, 160, 144, vmode[2], 0, 0, 0, 0);

	SDL_ShowCursor(0);

	overlay_init();

	if (fb.yuv) return;

	SDL_LockSurface(gb_screen);

	fb.w = gb_screen->w;
	fb.h = gb_screen->h;
	fb.pelsize = gb_screen->format->BytesPerPixel;
	fb.pitch = gb_screen->pitch;
	fb.indexed = fb.pelsize == 1;
	fb.ptr = gb_screen->pixels;
	fb.cc[0].r = gb_screen->format->Rloss;
	fb.cc[0].l = gb_screen->format->Rshift;
	fb.cc[1].r = gb_screen->format->Gloss;
	fb.cc[1].l = gb_screen->format->Gshift;
	fb.cc[2].r = gb_screen->format->Bloss;
	fb.cc[2].l = gb_screen->format->Bshift;

	SDL_UnlockSurface(gb_screen);

	fb.enabled = 1;
	fb.dirty = 0;

}


void ev_poll(int wait)
{
	event_t ev;
	SDL_Event event;
	(void) wait;

	while (SDL_PollEvent(&event))
	{
		switch(event.type)
		{
		case SDL_ACTIVEEVENT:
			if (event.active.state == SDL_APPACTIVE)
				fb.enabled = event.active.gain;
			break;
		case SDL_KEYDOWN:
			if ((event.key.keysym.sym == SDLK_RETURN) && (event.key.keysym.mod & KMOD_ALT))
				SDL_WM_ToggleFullScreen(screen);
			ev.type = EV_PRESS;
			ev.code = mapscancode(event.key.keysym.sym);
			ev_postevent(&ev);
			break;
		case SDL_KEYUP:
			ev.type = EV_RELEASE;
			ev.code = mapscancode(event.key.keysym.sym);
			ev_postevent(&ev);
			break;
		case SDL_JOYHATMOTION:
		case SDL_JOYAXISMOTION:
		case SDL_JOYBUTTONUP:
		case SDL_JOYBUTTONDOWN:
			sdljoy_process_event(&event);
			break;
		case SDL_QUIT:
			exit(1);
			break;
		default:
			break;
		}
	}
}

void vid_setpal(int i, int r, int g, int b)
{
	SDL_Color col;

	col.r = r; col.g = g; col.b = b;

	SDL_SetColors(gb_screen, &col, i, 1);
}

void vid_preinit()
{
}

void vid_close()
{
	if (overlay)
	{
		SDL_UnlockYUVOverlay(overlay);
		SDL_FreeYUVOverlay(overlay);
	}
	else SDL_UnlockSurface(screen);
	SDL_Quit();
	fb.enabled = 0;
}

void vid_settitle(char *title)
{
	SDL_WM_SetCaption(title, title);
}

void vid_begin()
{
	if (overlay)
	{
		SDL_LockYUVOverlay(overlay);
		fb.ptr = overlay->pixels[0];
		return;
	}
	SDL_LockSurface(gb_screen);
	fb.ptr = gb_screen->pixels;
}

void vid_end()
{
	if (overlay)
	{
		SDL_UnlockYUVOverlay(overlay);
		if (fb.enabled)
			SDL_DisplayYUVOverlay(overlay, &overlay_rect);
		return;
	}
	SDL_UnlockSurface(gb_screen);

	flip_fast_scale(gb_screen, screen);

	if (fb.enabled) SDL_Flip(screen);
}

static const uint8_t scale_table[160] = {0,1,3,4,6,7,9,10,12,13,15,16,18,19,21,22,24,25,27,28,30,31,33,34,36,37,39,40,42,43,45,46,48,49,51,52,54,55,57,58,60,61,63,64,66,67,69,70,72,73,75,76,78,79,81,82,84,85,87,88,90,91,93,94,96,97,99,100,102,103,105,106,108,109,111,112,114,115,117,118,120,121,123,124,126,127,129,130,132,133,135,136,138,139,141,142,144,145,147,148,150,151,153,154,156,157,159,160,162,163,165,166,168,169,171,172,174,175,177,178,180,181,183,184,186,187,189,190,192,193,195,196,198,199,201,202,204,205,207,208,210,211,213,214,216,217,219,220,222,223,225,226,228,229,231,232,234,235,237,238};

void flip_fast_scale(SDL_Surface *virtual_screen, SDL_Surface *hardware_screen){
	uint8_t nx, ny, x, y;
	uint16_t *source_pixel, *target_pixel;
	for (y = 0; y < 144; y ++) {
		ny = scale_table[y] + 12;
		for (x = 0; x < 160; x++) {
			nx = scale_table[x];
			source_pixel = (uint16_t*) ((uint8_t *) virtual_screen->pixels + y * virtual_screen->pitch + x * virtual_screen->format->BytesPerPixel);
			target_pixel = (uint16_t*) ((uint8_t *) hardware_screen->pixels + ny * hardware_screen->pitch + nx * hardware_screen->format->BytesPerPixel);
			
			*target_pixel = *source_pixel;
			
			if (x & 1) {
				target_pixel = (uint16_t*) ((uint8_t *) hardware_screen->pixels + ny * hardware_screen->pitch + (nx+1) * hardware_screen->format->BytesPerPixel);
				*target_pixel = *source_pixel;
			}
			
			if (y & 1) {
				target_pixel = (uint16_t*) ((uint8_t *) hardware_screen->pixels + (ny+1) * hardware_screen->pitch + nx * hardware_screen->format->BytesPerPixel);
				*target_pixel = *source_pixel;
			}
			if (x & 1 && y & 1) {
				target_pixel = (uint16_t*) ((uint8_t *) hardware_screen->pixels + (ny+1) * hardware_screen->pitch + (nx+1) * hardware_screen->format->BytesPerPixel);
				*target_pixel = *source_pixel;
			}
			
			
		}
	}

}