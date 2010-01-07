// vid_sdl.h -- sdl video driver 

#include "SDL.h"
#include "quakedef.h"
#include "d_local.h"

viddef_t    vid;                // global video state
unsigned short  d_8to16table[256];

// The original defaults
#define    BASEWIDTH    480
#define    BASEHEIGHT   320

#define    FIRE_SIZE    160
#define    JUMP_SIZE    120
#define    JOY_SIZE     160
#define    JOY_DEAD     10
#define    JOY_X        80
#define    JOY_Y        80

byte    autofire = 0;
byte    mousedown = 0;
byte    normalkeyboard = 0;
byte    gesturedown = 0;
extern int in_impulse;

int    VGA_width, VGA_height, VGA_rowbytes, VGA_bufferrowbytes = 0;
byte    *VGA_pagebase;

static SDL_Surface *screen = NULL;

static qboolean mouse_avail;
static float   mouse_x, mouse_y;
static float   joy_x, joy_y;
static int mouse_oldbuttonstate = 0;

// No support for option menus
void (*vid_menudrawfn)(void) = NULL;
void (*vid_menukeyfn)(int key) = NULL;

void    VID_SetPalette (unsigned char *palette)
{
    int i;
    SDL_Color colors[256];

    for ( i=0; i<256; ++i )
    {
        colors[i].r = *palette++;
        colors[i].g = *palette++;
        colors[i].b = *palette++;
    }
    SDL_SetColors(screen, colors, 0, 256);
}

void    VID_ShiftPalette (unsigned char *palette)
{
    VID_SetPalette(palette);
}

void    VID_Init (unsigned char *palette)
{
    int pnum, chunk;
    byte *cache;
    int cachesize;
    Uint8 video_bpp;
    Uint16 video_w, video_h;
    Uint32 flags;

    // Load the SDL library
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0)
        Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());

    // Set up display mode (width and height)
    vid.width = BASEWIDTH;
    vid.height = BASEHEIGHT;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    if ((pnum=COM_CheckParm("-winsize")))
    {
        if (pnum >= com_argc-2)
            Sys_Error("VID: -winsize <width> <height>\n");
        vid.width = Q_atoi(com_argv[pnum+1]);
        vid.height = Q_atoi(com_argv[pnum+2]);
        if (!vid.width || !vid.height)
            Sys_Error("VID: Bad window width/height\n");
    }

    // Set video width, height and flags
    flags = (SDL_SWSURFACE|SDL_HWPALETTE);
    if ( COM_CheckParm ("-fullscreen") )
        flags |= SDL_FULLSCREEN;


    // Initialize display 
    if (!(screen = SDL_SetVideoMode(vid.width, vid.height, 8, flags)))
        Sys_Error("VID: Couldn't set video mode: %s\n", SDL_GetError());
    VID_SetPalette(palette);
    SDL_WM_SetCaption("sdlquake","sdlquake");
    // now know everything we need to know about the buffer
    VGA_width = vid.conwidth = vid.width;
    VGA_height = vid.conheight = vid.height;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
    VGA_pagebase = vid.buffer = screen->pixels;
    VGA_rowbytes = vid.rowbytes = screen->pitch;
    vid.conbuffer = vid.buffer;
    vid.conrowbytes = vid.rowbytes;
    vid.direct = 0;
    
    // allocate z buffer and surface cache
    chunk = vid.width * vid.height * sizeof (*d_pzbuffer);
    cachesize = D_SurfaceCacheForRes (vid.width, vid.height);
    chunk += cachesize;
    d_pzbuffer = Hunk_HighAllocName(chunk, "video");
    if (d_pzbuffer == NULL)
        Sys_Error ("Not enough memory for video mode\n");

    // initialize the cache memory 
        cache = (byte *) d_pzbuffer
                + vid.width * vid.height * sizeof (*d_pzbuffer);
    D_InitCaches (cache, cachesize);

    // initialize the mouse
    SDL_ShowCursor(0);
}

void    VID_Shutdown (void)
{
    SDL_Quit();
}

void D_DrawUIOverlay()
{
    //XXX: draw overlay here as appropriate

    int overlay_x = 0;
    int overlay_y = 160;
    int i;

    Uint8 *row = screen->pixels + overlay_y*screen->pitch + overlay_x;
    Uint8 *offset;
    for ( i = 0; i < 159; i++ )
    {
        for ( offset = row; offset < row + 160; offset+=2 )
        {
            *offset = 0;
        }
        row += screen->pitch;
    }

    //SDL_UpdateRect(screen, overlay_x, overlay_y, 160, 160);
}

void    VID_Update (vrect_t *rects)
{
    SDL_Rect *sdlrects;
    int n, i;
    vrect_t *rect;

    // Two-pass system, since Quake doesn't do it the SDL way...

    // First, count the number of rectangles
    n = 0;
    for (rect = rects; rect; rect = rect->pnext)
        ++n;

    // Second, copy them to SDL rectangles and update
    if (!(sdlrects = (SDL_Rect *)alloca(n*sizeof(*sdlrects))))
        Sys_Error("Out of memory");
    i = 0;
    for (rect = rects; rect; rect = rect->pnext)
    {
        sdlrects[i].x = rect->x;
        sdlrects[i].y = rect->y;
        sdlrects[i].w = rect->width;
        sdlrects[i].h = rect->height;
        ++i;
    }

    D_DrawUIOverlay();
    SDL_UpdateRects(screen, n, sdlrects);

}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
    Uint8 *offset;


    if (!screen) return;
    if ( x < 0 ) x = screen->w+x-1;
    offset = (Uint8 *)screen->pixels + y*screen->pitch + x;
    while ( height-- )
    {
        memcpy(offset, pbitmap, width);
        offset += screen->pitch;
        pbitmap += width;
    }
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
    if (!screen) return;
    if (x < 0) x = screen->w+x-1;

    D_DrawUIOverlay();
    SDL_UpdateRect(screen, x, y, width, height);

}


/*
================
Sys_SendKeyEvents
================
*/

void Sys_SendKeyEvents(void)
{
    SDL_Event event;
    int sym, state;
     int modstate;

    while (SDL_PollEvent(&event))
    {
        switch (event.type) {

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                sym = event.key.keysym.sym;
                state = event.key.state;
                modstate = SDL_GetModState();
                switch(sym)
                {
                   case SDLK_DELETE: sym = K_DEL; break;
                   case SDLK_BACKSPACE: sym = K_BACKSPACE; break;
                   case SDLK_F1: sym = K_F1; break;
                   case SDLK_F2: sym = K_F2; break;
                   case SDLK_F3: sym = K_F3; break;
                   case SDLK_F4: sym = K_F4; break;
                   case SDLK_F5: sym = K_F5; break;
                   case SDLK_F6: sym = K_F6; break;
                   case SDLK_F7: sym = K_F7; break;
                   case SDLK_F8: sym = K_F8; break;
                   case SDLK_F9: sym = K_F9; break;
                   case SDLK_F10: sym = K_F10; break;
                   case SDLK_F11: sym = K_F11; break;
                   case SDLK_F12: sym = K_F12; break;
                   case SDLK_BREAK:
                   case SDLK_PAUSE: sym = K_PAUSE; break;
                   case SDLK_UP: sym = K_UPARROW; break;
                   case SDLK_DOWN: sym = K_DOWNARROW; break;
                   case SDLK_RIGHT: sym = K_RIGHTARROW; break;
                   case SDLK_LEFT: sym = K_LEFTARROW; break;
                   case SDLK_INSERT: sym = K_INS; break;
                   case SDLK_HOME: sym = K_HOME; break;
                   case SDLK_END: sym = K_END; break;
                   case SDLK_PAGEUP: sym = K_PGUP; break;
                   case SDLK_PAGEDOWN: sym = K_PGDN; break;
                   case SDLK_RSHIFT:
                   case SDLK_LSHIFT: sym = K_SHIFT; break;
                   case SDLK_RCTRL:
                   case SDLK_LCTRL: sym = K_CTRL; break;
                   case SDLK_RALT:
                   case SDLK_LALT: sym = K_ALT; break;
                   case SDLK_KP0: 
                       if(modstate & KMOD_NUM) sym = K_INS; 
                       else sym = SDLK_0;
                       break;
                   case SDLK_KP1:
                       if(modstate & KMOD_NUM) sym = K_END;
                       else sym = SDLK_1;
                       break;
                   case SDLK_KP2:
                       if(modstate & KMOD_NUM) sym = K_DOWNARROW;
                       else sym = SDLK_2;
                       break;
                   case SDLK_KP3:
                       if(modstate & KMOD_NUM) sym = K_PGDN;
                       else sym = SDLK_3;
                       break;
                   case SDLK_KP4:
                       if(modstate & KMOD_NUM) sym = K_LEFTARROW;
                       else sym = SDLK_4;
                       break;
                   case SDLK_KP5: sym = SDLK_5; break;
                   case SDLK_KP6:
                       if(modstate & KMOD_NUM) sym = K_RIGHTARROW;
                       else sym = SDLK_6;
                       break;
                   case SDLK_KP7:
                       if(modstate & KMOD_NUM) sym = K_HOME;
                       else sym = SDLK_7;
                       break;
                   case SDLK_KP8:
                       if(modstate & KMOD_NUM) sym = K_UPARROW;
                       else sym = SDLK_8;
                       break;
                   case SDLK_KP9:
                       if(modstate & KMOD_NUM) sym = K_PGUP;
                       else sym = SDLK_9;
                       break;
                   case SDLK_KP_PERIOD:
                       if(modstate & KMOD_NUM) sym = K_DEL;
                       else sym = SDLK_PERIOD;
                       break;
                   case SDLK_KP_DIVIDE: sym = SDLK_SLASH; break;
                   case SDLK_KP_MULTIPLY: sym = SDLK_ASTERISK; break;
                   case SDLK_KP_MINUS: sym = SDLK_MINUS; break;
                   case SDLK_KP_PLUS: sym = SDLK_PLUS; break;
                   case SDLK_KP_ENTER: sym = SDLK_RETURN; break;
                   case SDLK_KP_EQUALS: sym = SDLK_EQUALS; break;
                }
                // If we're not directly handled and still above 255
                // just force it to 0
                if(sym > 255) sym = 0;

                //XXX: This is a terrible hack
                // because for some reason
                // parsing configs doesn't work?

                //sym+i is tilde
                if ( sym == 37 ) sym = SDLK_BACKQUOTE;

                if ( !normalkeyboard )
                {
                    //these values are from keys.h
                    if ( sym == SDLK_j ) sym = K_CTRL;//fire!
                    if ( sym == SDLK_b ) sym = K_SPACE;//jump!
                    //if ( sym == SDLK_j ) sym = K_UPARROW;//forward!
                    //if ( sym == SDLK_b ) sym = K_DOWNARROW;//back
                    if ( sym == SDLK_h ) sym = 44;//strafeleft
                    if ( sym == SDLK_n ) sym = 46;//straferight

                    //same, only sprint versions
                    if ( sym == SDLK_i ) 
                    {
                        Key_Event( K_SHIFT, state );
                        Key_Event( K_UPARROW, state );
                        Key_Event( K_SHIFT, state );
                        sym = 0;
                    }
                    if ( sym == SDLK_u ) 
                    {
                        Key_Event( K_SHIFT, state );
                        Key_Event( 44, state );
                        Key_Event( K_SHIFT, state );
                        sym = 0;
                    }
                    if ( sym == SDLK_k ) 
                    {
                        Key_Event( K_SHIFT, state );
                        Key_Event( 46, state );
                        Key_Event( K_SHIFT, state );
                        sym = 0;
                    }

                    //remap the numbers  to the weapons, so no orange needed
                    if ( sym == SDLK_e ) sym = SDLK_1;
                    if ( sym == SDLK_r ) sym = SDLK_2;
                    if ( sym == SDLK_t ) sym = SDLK_3;
                    if ( sym == SDLK_d ) sym = SDLK_4;
                    if ( sym == SDLK_f ) sym = SDLK_5;
                    if ( sym == SDLK_g ) sym = SDLK_6;
                    if ( sym == SDLK_x ) sym = SDLK_7;
                    if ( sym == SDLK_c ) sym = SDLK_8;
                    if ( sym == SDLK_v ) sym = SDLK_9;

                    //quick load/quick save
                    if ( sym == SDLK_QUOTE ) sym = K_F9;//load
                    if ( sym == SDLK_UNDERSCORE ) sym = K_F6;//save

                    //menu
                    if ( sym == SDLK_q ) sym = K_ESCAPE;

                    //arrow keys for menu nav
                    if ( sym == SDLK_w ) sym = K_LEFTARROW;
                    if ( sym == SDLK_s ) sym = K_UPARROW;
                    if ( sym == SDLK_z ) sym = K_RIGHTARROW;
                    if ( sym == SDLK_a ) sym = K_DOWNARROW;
                }

                //Weapon cycling!

                //gesture down
                //here we use the full name since in normal keyboard mode
                //we bind 'q' to escape, same as swipe down
                if ( event.key.keysym.sym == 27 && state )
                {
                    in_impulse = 10;
                    break;
                }

                //gesture up
                if ( sym == 229 && state )
                {
                    in_impulse = 12;
                    break;
                }

                //gesture button
                if ( sym == 231 )
                {
                    gesturedown = state;
                }

                if ( sym == SDLK_AT && state )
                {
                    normalkeyboard = !normalkeyboard;
                    if ( normalkeyboard )
                    {
                        Con_Printf( "Normal keyboard enabled. Press '@' to toggle back.\n" );
                    }
                    else
                    {
                        Con_Printf( "Action keyboard enabled. Press '@' to toggle back.\n" );
                    }
                }

                Key_Event(sym, state);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                
                if ( event.motion.x > vid.width - FIRE_SIZE &&
                        event.motion.y > vid.height - FIRE_SIZE )
                {
                    //FIRE!
                    Key_Event( K_MOUSE1, event.button.state );
                    autofire = event.button.state;
                    break;
                }
                break;
            case SDL_MOUSEMOTION:
                //printf( "MOUSE: %d, %d\n", event.motion.xrel, event.motion.yrel );

                if ( mousedown && 
                        event.motion.y > vid.height - JOY_SIZE &&
                        event.motion.x < JOY_SIZE )
                {
                    joy_x = ( event.motion.x - JOY_X );
                    if ( joy_x < JOY_DEAD && joy_x > -JOY_DEAD )
                    {
                        joy_x = 0;
                    }
                    else
                    {
                        if ( joy_x >= JOY_DEAD )
                        {
                            joy_x -= JOY_DEAD;
                        }
                        else
                        {
                            joy_x += JOY_DEAD;
                        }
                    }

                    joy_x *= 3;

                    //printf( "%f, %f, %f\n", vid.height, event.motion.y, JOY_SIZE );
                    joy_y = -( (float)vid.height - event.motion.y - JOY_Y )*2;
                    break;
                }

                if ( !mousedown )
                {
                    joy_x = 0;
                    joy_y = 0;
                }

                //jump: top
                if ( event.motion.y < JUMP_SIZE )
                {
                    //top-left corner, jump!
                    Key_Event( 32, true );
                    Key_Event( 32, false );
                    break;
                }

                //wasn't a special area, do mouse-look

                //threshold sudden movements, and only use relative movement while finger down
                //if ( mousedown &&
                //        event.motion.xrel < 100 && event.motion.xrel > -100 && event.motion.yrel < 100 && event.motion.yrel > -100 ) {
                //    mouse_x = event.motion.xrel*20;

                //    //Mouse y--moves forward/back not look
                //    //cl.viewangles[PITCH] += m_pitch.value * event.motion.yrel * 10;
                //    //if (cl.viewangles[PITCH] > 80)
                //    //    cl.viewangles[PITCH] = 80;
                //    //if (cl.viewangles[PITCH] < -70)
                //    //    cl.viewangles[PITCH] = -70;
                //}

                break;

            case SDL_QUIT:
                CL_Disconnect ();
                Host_ShutdownServer(false);        
                Sys_Quit ();
                break;
            default:
                break;
        }

    }
}

void IN_Init (void)
{
    if ( COM_CheckParm ("-nomouse") )
        return;
    mouse_x = mouse_y = 0.0;
    joy_x = joy_y = 0.0;
    mouse_avail = 1;
}

void IN_Shutdown (void)
{
    mouse_avail = 0;
}

void IN_Commands (void)
{
    int i;
    int mouse_buttonstate;

    if (!mouse_avail) return;

    i = SDL_GetMouseState(NULL, NULL);
    /* Quake swaps the second and third buttons */
    mouse_buttonstate = (i & ~0x06) | ((i & 0x02)<<1) | ((i & 0x04)>>1);
    mousedown = mouse_buttonstate & 1;
    if ( !mousedown && autofire )
    {
        autofire = false;
    }

    Key_Event( K_MOUSE1, autofire );
    //for (i=0 ; i<3 ; i++) {
    //    if ( (mouse_buttonstate & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)) )
    //        Key_Event (K_MOUSE1 + i, true);

    //    if ( !(mouse_buttonstate & (1<<i)) && (mouse_oldbuttonstate & (1<<i)) )
    //        Key_Event (K_MOUSE1 + i, false);
    //}
    //mouse_oldbuttonstate = mouse_buttonstate;
}

void IN_Move (usercmd_t *cmd)
{
    if (!mouse_avail)
        return;

    mouse_x = joy_x * sensitivity.value;
    mouse_y = joy_y * sensitivity.value;
   
    //if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
    if( gesturedown )
        cmd->sidemove += m_side.value * mouse_x;
    else
        cl.viewangles[YAW] -= m_yaw.value * mouse_x;
    if (in_mlook.state & 1)
        V_StopPitchDrift ();
   
    cmd->forwardmove -= m_forward.value * mouse_y;
    //if ( (in_mlook.state & 1) && !(in_strafe.state & 1)) {
    //    cl.viewangles[PITCH] += m_pitch.value * mouse_y;
    //    if (cl.viewangles[PITCH] > 80)
    //        cl.viewangles[PITCH] = 80;
    //    if (cl.viewangles[PITCH] < -70)
    //        cl.viewangles[PITCH] = -70;
    //} else {
    //    if ((in_strafe.state & 1) && noclip_anglehack)
    //        cmd->upmove -= m_forward.value * mouse_y;
    //    else
    //        cmd->forwardmove -= m_forward.value * mouse_y;
    //}
    mouse_x = mouse_y = 0.0;

    if ( !mousedown )
    {
        joy_x = joy_y = 0.0;
    }
}

/*
================
Sys_ConsoleInput
================
*/
char *Sys_ConsoleInput (void)
{
    return 0;
}
