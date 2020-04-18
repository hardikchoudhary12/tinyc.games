// Blocko -- http://tinyc.games -- (c) 2018 Jer Wilson
//
// Blocko is a barebones 3D platformer using OpenGL via GLEW.
//
// Using OpenGL on Windows requires the Windows SDK.
// The run-windows.bat script will try hard to find the SDK files it needs,
// otherwise it will tell you what to do.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#define GL3_PROTOTYPES 1

#ifdef __APPLE__
#include <gl.h>
#else
#include <GL/glew.h>
#endif

#define SDL_DISABLE_IMMINTRIN_H
#include <SDL.h>
#define STBI_NO_SIMD
#define STB_IMAGE_IMPLEMENTATION
#include "../_stb/stb_image.h"

#include "./timer.c"
#include "./shader.c"

#define SCALE 3                    // x magnification
#define W 1366                     // window width, height
#define H 768                      // ^
#define TILESW 200                 // total level width, height
#define TILESH 40                  // ^
#define TILESD 200                 // ^
#define BS (20*SCALE)              // block size
#define BS2 (BS/2)                 // block size in half
#define PLYR_W (16*SCALE)          // physical width and height of the player
#define PLYR_H (BS)                // ^
#define PLYR_SPD (2*SCALE)         // units per frame
#define STARTPX (TILESW*BS2)       // starting position within start screen
#define STARTPY (0)                // ^
#define STARTPZ (TILESD*BS2)       // ^
#define NR_PLAYERS 1
#define GRAV_JUMP 0
#define GRAV_ZERO 14
#define GRAV_MAX 30

#define UP    1
#define EAST  2
#define NORTH 3
#define WEST  4
#define SOUTH 5
#define DOWN  6

#define DIRT 45
#define GRAS 46
#define LASTSOLID GRAS // everything less than here is solid
#define OPEN 75        // invisible open, walkable space

#define VERTEX_BUFLEN 100000

#define CLAMP(v, l, u) { if (v < l) v = l; else if (v > u) v = u; }

struct vbufv { // vertex buffer vertex
        float tex;
        float orient;
        float x, y, z;
        float illum0, illum1, illum2, illum3;
};

int gravity[] = { -20, -17, -14, -12, -10, -8, -6, -5, -4, -3,
                   -2,  -2,  -1,  -1,   0,  1,  1,  2,  2,  3,
                    4,   5,   6,   7,   8, 10, 12, 14, 17, 20,
                   22, };

unsigned char tiles[TILESD][TILESH][TILESW];
unsigned char skylt[TILESD+1][TILESH+1][TILESW+1];

struct box { float x, y, z, w, h ,d; };
struct point { float x, y, z; };

struct player {
        struct box pos;
        struct point vel;
        float yaw;
        float pitch;
        int goingf;
        int goingb;
        int goingl;
        int goingr;
        int fvel;
        int rvel;
        int grav;
        int ground;
} player[NR_PLAYERS];

int frame = 0;
int noisy = 1;
int polys = 0;

int mouselook = 1;
int target_x, target_y, target_z;
int place_x, place_y, place_z;
int screenw = W;
int screenh = H;
int zooming = 0;
float zoom_amt = 1.f;

SDL_Event event;
SDL_Window *win;
SDL_GLContext ctx;
SDL_Renderer *renderer;
SDL_Surface *surf;

unsigned int vbo, vao;
struct vbufv vbuf[VERTEX_BUFLEN + 1000]; // vertex buffer + padding
struct vbufv *v_limit = vbuf + VERTEX_BUFLEN;
struct vbufv *v = vbuf;

//prototypes
void setup();
void resize();
void new_game();
void gen_world();
void key_move(int down);
void mouse_move();
void mouse_button(int down);
void jump(int down);
void update_world();
void update_player();
int move_player(int velx, int vely, int velz);
int collide(struct box plyr, struct box block);
int block_collide(int bx, int by, int bz, struct box plyr);
int world_collide(struct box plyr);
void draw_stuff();
void debrief();

void GLAPIENTRY
MessageCallback( GLenum source,
                 GLenum type,
                 GLuint id,
                 GLenum severity,
                 GLsizei length,
                 const GLchar* message,
                 const void* userParam )
{
        /*
  fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
            */
}

//the entry point and main game loop
int main()
{
        setup();
        new_game();

        for(;;)
        {
                while(SDL_PollEvent(&event)) switch(event.type)
                {
                        case SDL_QUIT:            exit(0);
                        case SDL_KEYDOWN:         key_move(1);       break;
                        case SDL_KEYUP:           key_move(0);       break;
                        case SDL_MOUSEMOTION:     mouse_move();      break;
                        case SDL_MOUSEBUTTONDOWN: mouse_button(1);   break;
                        case SDL_MOUSEBUTTONUP:   mouse_button(0);   break;
                        case SDL_WINDOWEVENT:
                                switch(event.window.event) {
                                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                                                resize();
                                                break;
                                }
                                break;
                }

                update_player();
                update_world();
                draw_stuff();
                debrief();
                //SDL_Delay(1000 / 60);
                frame++;
        }
}

//initial setup to get the window and rendering going
void setup()
{
        srand(time(NULL));

        SDL_Init(SDL_INIT_VIDEO);
        win = SDL_CreateWindow("Blocko", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                W, H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
        if(!win) exit(fprintf(stderr, "%s\n", SDL_GetError()));
        ctx = SDL_GL_CreateContext(win);
        if(!ctx) exit(fprintf(stderr, "Could not create GL context\n"));
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetSwapInterval(1);
        #ifndef __APPLE__
        glewExperimental = GL_TRUE;
        glewInit();
        #endif

        // enable debug output
        glEnable              ( GL_DEBUG_OUTPUT );
        glDebugMessageCallback( MessageCallback, 0 );

        // load all the textures
        glActiveTexture(GL_TEXTURE0);
        int x, y, n, mode;
        GLuint texid = 0;
        glGenTextures(1, &texid);
        printf("texid: %d\n", texid);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texid);

        unsigned char *texels;
        char *files[] = { "res/top.png", "res/side.png", "res/bottom.png", "" };
        for(int f = 0; files[f][0]; f++)
        {
                texels = stbi_load(files[f], &x, &y, &n, 0);
                mode = n == 4 ? GL_RGBA : GL_RGB;
                if (f == 0)
                        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, x, y, 256);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, f, x, y, 1, mode, GL_UNSIGNED_BYTE, texels);
                printf("%d\n", glGetError());
                stbi_image_free(texels);
        }

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 1);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);


        load_shaders();

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // tex number
        glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, sizeof (struct vbufv), (void*)&((struct vbufv *)NULL)->tex);
        glEnableVertexAttribArray(0);
        // orientation
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof (struct vbufv), (void*)&((struct vbufv *)NULL)->orient);
        glEnableVertexAttribArray(1);
        // position
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof (struct vbufv), (void*)&((struct vbufv *)NULL)->x);
        glEnableVertexAttribArray(2);
        // illum
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof (struct vbufv), (void*)&((struct vbufv *)NULL)->illum0);
        glEnableVertexAttribArray(3);

	glUseProgram(prog_id);
        glUniform1i(glGetUniformLocation(prog_id, "tarray"), 0);

        SDL_SetRelativeMouseMode(SDL_TRUE);
}

void resize()
{
        screenw = event.window.data1;
        screenh = event.window.data2;
}

void key_move(int down)
{
        if(event.key.repeat) return;

        switch(event.key.keysym.sym)
        {
                case SDLK_w:
                        player[0].goingf = down;
                        break;
                case SDLK_s:
                        player[0].goingb = down;
                        break;
                case SDLK_a:
                        player[0].goingl = down;
                        break;
                case SDLK_d:
                        player[0].goingr = down;
                        break;
                case SDLK_SPACE:
                        jump(down);
                        break;
                case SDLK_ESCAPE:
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        mouselook = 0;
                        break;
                case SDLK_z:
                        zooming = down;
                        break;
                case SDLK_j:
                        if (!down) player[0].pos.y -= 1000;
                        break;
                case SDLK_n:
                        if (!down) noisy = !noisy;
                        break;
                case SDLK_q:
                        exit(-1);
        }
}

void mouse_move()
{
        if(!mouselook) return;

        float pitchlimit = 3.1415926535 * 0.5 - 0.001;
        player[0].yaw += event.motion.xrel * 0.001;
        player[0].pitch += event.motion.yrel * 0.001;

        if(player[0].pitch > pitchlimit)
                player[0].pitch = pitchlimit;

        if(player[0].pitch < -pitchlimit)
                player[0].pitch = -pitchlimit;
}

void mouse_button(int down)
{
        if(!down) return;

        if(event.button.button == SDL_BUTTON_LEFT)
        {
                if(!mouselook)
                {
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                        mouselook = 1;
                }
                else
                {
                        tiles[target_z][target_y][target_x] = OPEN;
                }
        }
        else if(event.button.button == SDL_BUTTON_RIGHT)
        {
                tiles[place_z][place_y][place_x] = DIRT;
        }
        else if(event.button.button == SDL_BUTTON_X1)
        {
                jump(down);
        }
}

//start a new game
void new_game()
{
        memset(player, 0, sizeof player);
        player[0].pos.x = STARTPX;
        player[0].pos.y = STARTPY;
        player[0].pos.z = STARTPZ;
        player[0].pos.w = PLYR_W;
        player[0].pos.h = PLYR_H;
        player[0].pos.d = PLYR_W;
        player[0].goingf = 0;
        player[0].goingb = 0;
        player[0].goingl = 0;
        player[0].goingr = 0;
        player[0].fvel = 0;
        player[0].rvel = 0;
        player[0].yaw = 3.1415926535 * 0.23;
        player[0].pitch = 0;
        player[0].grav = GRAV_ZERO;
        gen_world();
}

void gen_world()
{
        for (int x = 0; x < TILESW; x++) for (int z = 0; z < TILESD; z++) for (int y = 0; y < TILESH; y++)
        {
                float xd = (x - 2 * TILESW / 3);
                float zd = (z - 1 * TILESD / 3);
                float d = sqrt(xd * xd + zd * zd);
                float h =  3 + 3*sin(0.1 * x) + 3*cos(0.2 * z + 0.02 * x) + 0.1 * (z + x);
                float s = -14 + 12*sin(1 + 0.14 * x) + 13*cos(2 + 0.18 * z);
                if (d < 40)
                {
                        float hmin = h * (40 / d) * 0.6;
                        if (hmin > h) h = hmin;
                }

                if (y == TILESH-1 || y > TILESH - h || y > TILESH - s)
                {
                        tiles[z][y][x] = ((y == 0 || tiles[z][y-1][x] == OPEN) && rand() % 100 == 1) ? GRAS : DIRT;
                        skylt[z][y][x] = 0;
                }
                else
                {
                        tiles[z][y][x] = OPEN;
                        skylt[z][y][x] = 15;
                }

                // clunky thing
                if ((z == 4 && x + y < 10) || (x == 7 && z + y > 10 && z < 14))
                {
                        tiles[z][y][x] = DIRT;
                        skylt[z][y][x] = 0;
                }
        }
}

void jump(int down)
{
        if(player[0].ground && down)
                player[0].grav = GRAV_JUMP;
}

void update_world()
{
        int i, x, y, z;
        for (i = 0; i < 100; i++) {
                x = 1 + rand() % (TILESW - 2);
                z = 1 + rand() % (TILESD - 2);

                for (y = 1; y < TILESH - 1; y++) {
                        if (tiles[z][y][x] == DIRT) {
                                if (tiles[z  ][y-1][x  ] == OPEN && (
                                    tiles[z+1][y  ][x  ] == GRAS ||
                                    tiles[z-1][y  ][x  ] == GRAS ||
                                    tiles[z  ][y  ][x+1] == GRAS ||
                                    tiles[z  ][y  ][x-1] == GRAS ||
                                    tiles[z+1][y+1][x  ] == GRAS ||
                                    tiles[z-1][y+1][x  ] == GRAS ||
                                    tiles[z  ][y+1][x+1] == GRAS ||
                                    tiles[z  ][y+1][x-1] == GRAS ||
                                    tiles[z+1][y-1][x  ] == GRAS ||
                                    tiles[z-1][y-1][x  ] == GRAS ||
                                    tiles[z  ][y-1][x+1] == GRAS ||
                                    tiles[z  ][y-1][x-1] == GRAS) ) {
                                        //fprintf(stderr, "grassing %d %d %d\n", x, y, z);
                                        tiles[z][y][x] = GRAS;
                                }
                                break;
                        }
                }
        }
}

void update_player()
{
        struct player *p = player + 0;

        if(player[0].pos.y > TILESH*BS + 6000)
        {
                new_game();
                return;
        }

        if(p->goingf && !p->goingb) { p->fvel++; }
        else if(p->fvel > 0)        { p->fvel--; }

        if(p->goingb && !p->goingf) { p->fvel--; }
        else if(p->fvel < 0)        { p->fvel++; }

        if(p->goingr && !p->goingl) { p->rvel++; }
        else if(p->rvel > 0)        { p->rvel--; }

        if(p->goingl && !p->goingr) { p->rvel--; }
        else if(p->rvel < 0)        { p->rvel++; }

        //limit speed
        float totalvel = sqrt(p->fvel * p->fvel + p->rvel * p->rvel);
        if(totalvel > PLYR_SPD)
        {
                totalvel = PLYR_SPD / totalvel;
                if (p->fvel > 4 || p->fvel < -4) p->fvel *= totalvel;
                if (p->rvel > 4 || p->rvel < -4) p->rvel *= totalvel;
        }

        float fwdx = sin(p->yaw);
        float fwdz = cos(p->yaw);

        p->vel.x = fwdx * p->fvel + fwdz * p->rvel;
        p->vel.z = fwdz * p->fvel - fwdx * p->rvel;

        if(!move_player(p->vel.x, p->vel.y, p->vel.z))
        {
                p->fvel = 0;
                p->rvel = 0;
        }

        //gravity
        if(!p->ground || p->grav < GRAV_ZERO)
        {
                if(!move_player(0, gravity[p->grav], 0))
                        p->grav = GRAV_ZERO;
                else if(p->grav < GRAV_MAX)
                        p->grav++;
        }

        //detect ground
        struct box foot = (struct box){
                p->pos.x, p->pos.y + PLYR_H, p->pos.z,
                PLYR_W, 1, PLYR_W};
        p->ground = world_collide(foot);

        if(p->ground)
                p->grav = GRAV_ZERO;

        //zooming
        zoom_amt *= zooming ? 0.9f : 1.2f;
        CLAMP(zoom_amt, 0.25f, 1.0f);
}

//collide a box with nearby world tiles
int world_collide(struct box box)
{
        for(int i = -1; i < 2; i++) for(int j = -1; j < 3; j++) for(int k = -1; k < 2; k++)
        {
                int bx = box.x/BS + i;
                int by = box.y/BS + j;
                int bz = box.z/BS + k;

                if(block_collide(bx, by, bz, box))
                        return 1;
        }

        return 0;
}

//return 0 iff we couldn't actually move
int move_player(int velx, int vely, int velz)
{
        int last_was_x = 0;
        int last_was_z = 0;
        int already_stuck = 0;
        int moved = 0;

        if(!velx && !vely && !velz)
                return 1;

        if(world_collide(player[0].pos))
                already_stuck = 1;

        while(velx || vely || velz)
        {
                struct box testpos = player[0].pos;
                int amt;

                if((!velx && !velz) || ((last_was_x || last_was_z) && vely))
                {
                        amt = vely > 0 ? 1 : -1;
                        testpos.y += amt;
                        vely -= amt;
                        last_was_x = 0;
                        last_was_z = 0;
                }
                else if(!velz || (last_was_z && velx))
                {
                        amt = velx > 0 ? 1 : -1;
                        testpos.x += amt;
                        velx -= amt;
                        last_was_z = 0;
                        last_was_x = 1;
                }
                else
                {
                        amt = velz > 0 ? 1 : -1;
                        testpos.z += amt;
                        velz -= amt;
                        last_was_x = 0;
                        last_was_z = 1;
                }

                int would_be_stuck = 0;

                if(world_collide(testpos))
                        would_be_stuck = 1;
                else
                        already_stuck = 0;

                if(would_be_stuck && !already_stuck)
                {
                        if(last_was_x)
                                velx = 0;
                        else if(last_was_z)
                                velz = 0;
                        else
                                vely = 0;
                        continue;
                }

                player[0].pos = testpos;
                moved = 1;
        }

        return moved;
}

int legit_tile(int x, int y, int z)
{
        return x >= 0 && x < TILESW
            && y >= 0 && y < TILESH
            && z >= 0 && z < TILESD;
}

//collide a rect with a rect
int collide(struct box l, struct box r)
{
        int xcollide = l.x + l.w >= r.x && l.x < r.x + r.w;
        int ycollide = l.y + l.h >= r.y && l.y < r.y + r.h;
        int zcollide = l.z + l.d >= r.z && l.z < r.z + r.d;
        return xcollide && ycollide && zcollide;
}

//collide a rect with a block
int block_collide(int bx, int by, int bz, struct box box)
{
        if(!legit_tile(bx, by, bz))
                return 0;

        if(tiles[bz][by][bx] <= LASTSOLID)
                return collide(box, (struct box){BS*bx, BS*by, BS*bz, BS, BS, BS});

        return 0;
}

//select block from eye following vector f
void rayshot(float eye0, float eye1, float eye2, float f0, float f1, float f2)
{
        int x = (int)(eye0 / BS);
        int y = (int)(eye1 / BS);
        int z = (int)(eye2 / BS);

        for(int i = 0; ; i++)
        {
                float a0 = (BS * (x + (f0 > 0 ? 1 : 0)) - eye0) / f0;
                float a1 = (BS * (y + (f1 > 0 ? 1 : 0)) - eye1) / f1;
                float a2 = (BS * (z + (f2 > 0 ? 1 : 0)) - eye2) / f2;
                float amt = 0;

                place_x = x;
                place_y = y;
                place_z = z;

                if(a0 < a1 && a0 < a2) { x += (f0 > 0 ? 1 : -1); amt = a0; }
                else if(a1 < a2)       { y += (f1 > 0 ? 1 : -1); amt = a1; }
                else                   { z += (f2 > 0 ? 1 : -1); amt = a2; }

                eye0 += amt * f0 * 1.0001;
                eye1 += amt * f1 * 1.0001;
                eye2 += amt * f2 * 1.0001;

                if(x < 0 || y < 0 || z < 0 || x >= TILESW || y >= TILESH || z >= TILESD)
                        goto bad;

                if(tiles[z][y][x] != OPEN)
                        break;

                if(i == 6)
                        goto bad;
        }

        target_x = x;
        target_y = y;
        target_z = z;

        return;

        bad:
        target_x = target_y = target_z = 0;
        place_x = place_y = place_z = 0;
}

//draw everything in the game on the screen
void draw_stuff()
{
        glViewport(0, 0, screenw, screenh);
        glClearColor(0.3, 0.9, 1.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // compute proj matrix
        float near = 16.f;
        float far = 199999.f;
        float frustw = 9.f * zoom_amt * screenw / screenh;
        float frusth = 9.f * zoom_amt;
        float frustM[] = {
                near/frustw,           0,                                  0,  0,
                          0, near/frusth,                                  0,  0,
                          0,           0,       -(far + near) / (far - near), -1,
                          0,           0, -(2.f * far * near) / (far - near),  0
        };
        glUniformMatrix4fv(glGetUniformLocation(prog_id, "proj"), 1, GL_FALSE, frustM);

        // compute view matrix
        float eye0, eye1, eye2;
        eye0 = player[0].pos.x + PLYR_W / 2;
        eye1 = player[0].pos.y - BS * 3 / 4;
        eye2 = player[0].pos.z + PLYR_W / 2;
        float f0, f1, f2;
        f0 = cos(player[0].pitch) * sin(player[0].yaw);
        f1 = sin(player[0].pitch);
        f2 = cos(player[0].pitch) * cos(player[0].yaw);
        float wing0, wing1, wing2;
        wing0 = -cos(player[0].yaw);
        wing1 = 0;
        wing2 = sin(player[0].yaw);
        float up0, up1, up2;
        up0 = f1*wing2 - f2*wing1;
        up1 = f2*wing0 - f0*wing2;
        up2 = f0*wing1 - f1*wing0;
        float upm = sqrt(up0*up0 + up1*up1 + up2*up2);
        up0 /= upm;
        up1 /= upm;
        up2 /= upm;
        float s0, s1, s2;
        s0 = f1*up2 - f2*up1;
        s1 = f2*up0 - f0*up2;
        s2 = f0*up1 - f1*up0;
        float sm = sqrt(s0*s0 + s1*s1 + s2*s2);
        float z0, z1, z2;
        z0 = s0/sm;
        z1 = s1/sm;
        z2 = s2/sm;
        float u0, u1, u2;
        u0 = z1*f2 - z2*f1;
        u1 = z2*f0 - z0*f2;
        u2 = z0*f1 - z1*f0;
        float viewM[] = {
                s0, u0,-f0, 0,
                s1, u1,-f1, 0,
                s2, u2,-f2, 0,
                 0,  0,  0, 1
        };

        // find where we are pointing at
        rayshot(eye0, eye1, eye2, f0, f1, f2);

        // translate by hand
        viewM[12] = (viewM[0] * -eye0) + (viewM[4] * -eye1) + (viewM[ 8] * -eye2);
        viewM[13] = (viewM[1] * -eye0) + (viewM[5] * -eye1) + (viewM[ 9] * -eye2);
        viewM[14] = (viewM[2] * -eye0) + (viewM[6] * -eye1) + (viewM[10] * -eye2);

        glUniformMatrix4fv(glGetUniformLocation(prog_id, "view"), 1, GL_FALSE, viewM);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_LEQUAL);

        // identity for model view for world drawing
        float modelM[] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
        };
        glUniformMatrix4fv(glGetUniformLocation(prog_id, "model"), 1, GL_FALSE, modelM);

        glUniform1f(glGetUniformLocation(prog_id, "BS"), BS);

        v = vbuf; // reset vertex buffer pointer

        // draw world
        TIMER(buildvbo)
        for (int x = 0; x < TILESW; x++) for (int y = 0; y < TILESH; y++) for (int z = 0; z < TILESD; z++)
        {
                if (v >= v_limit) {
                        TIMERSTOP(buildvbo)
                        TIMER(glBufferData)
                        //printf("buffer full, draw %ld verts\n", v - vbuf);
                        polys += (v - vbuf) * 2;
                        glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STATIC_DRAW);
                        TIMERSTOP(glBufferData)
                        TIMER(glDrawArrays)
                        glDrawArrays(GL_POINTS, 0, v - vbuf);
                        TIMERSTOP(glDrawArrays);
                        v = vbuf;
                        TIMER(buildvbo)
                }

                if (tiles[z][y][x] == OPEN) continue;

                //lighting
                int x_ = (x == 0) ? 0 : x - 1;
                int y_ = (y == 0) ? 0 : y - 1;
                int z_ = (z == 0) ? 0 : z - 1;
                float mylight = skylt[z][y][x];
                float usw = 0.008f * (skylt[z_ ][y_ ][x_ ] + skylt[z  ][y_ ][x_ ] + skylt[z  ][y  ][x_ ] + skylt[z_ ][y  ][x_ ] + skylt[z_ ][y  ][x  ] + skylt[z_ ][y_ ][x  ] + skylt[z  ][y_ ][x_ ] + mylight);
                float use = 0.008f * (skylt[z_ ][y_ ][x+1] + skylt[z  ][y_ ][x+1] + skylt[z  ][y  ][x+1] + skylt[z_ ][y  ][x+1] + skylt[z_ ][y  ][x  ] + skylt[z_ ][y_ ][x  ] + skylt[z  ][y_ ][x+1] + mylight);
                float unw = 0.008f * (skylt[z+1][y_ ][x_ ] + skylt[z  ][y_ ][x_ ] + skylt[z  ][y  ][x_ ] + skylt[z+1][y  ][x_ ] + skylt[z+1][y  ][x  ] + skylt[z+1][y_ ][x  ] + skylt[z  ][y_ ][x_ ] + mylight);
                float une = 0.008f * (skylt[z+1][y_ ][x+1] + skylt[z  ][y_ ][x+1] + skylt[z  ][y  ][x+1] + skylt[z+1][y  ][x+1] + skylt[z+1][y  ][x  ] + skylt[z+1][y_ ][x  ] + skylt[z  ][y_ ][x+1] + mylight);
                float dsw = 0.008f * (skylt[z_ ][y+1][x_ ] + skylt[z  ][y+1][x_ ] + skylt[z  ][y  ][x_ ] + skylt[z_ ][y  ][x_ ] + skylt[z_ ][y  ][x  ] + skylt[z_ ][y+1][x  ] + skylt[z  ][y+1][x_ ] + mylight);
                float dse = 0.008f * (skylt[z_ ][y+1][x+1] + skylt[z  ][y+1][x+1] + skylt[z  ][y  ][x+1] + skylt[z_ ][y  ][x+1] + skylt[z_ ][y  ][x  ] + skylt[z_ ][y+1][x  ] + skylt[z  ][y+1][x+1] + mylight);
                float dnw = 0.008f * (skylt[z+1][y+1][x_ ] + skylt[z  ][y+1][x_ ] + skylt[z  ][y  ][x_ ] + skylt[z+1][y  ][x_ ] + skylt[z+1][y  ][x  ] + skylt[z+1][y+1][x  ] + skylt[z  ][y+1][x_ ] + mylight);
                float dne = 0.008f * (skylt[z+1][y+1][x+1] + skylt[z  ][y+1][x+1] + skylt[z  ][y  ][x+1] + skylt[z+1][y  ][x+1] + skylt[z+1][y  ][x  ] + skylt[z+1][y+1][x  ] + skylt[z  ][y+1][x+1] + mylight);
                if (tiles[z][y][x] == GRAS)
                {
                        if (y == 0        || tiles[z  ][y-1][x  ] == OPEN) *v++ = (struct vbufv){ 0,    UP, x, y, z, usw, use, unw, une };
                        if (z == 0        || tiles[z-1][y  ][x  ] == OPEN) *v++ = (struct vbufv){ 1, SOUTH, x, y, z, use, usw, dse, dsw };
                        if (z == TILESD-1 || tiles[z+1][y  ][x  ] == OPEN) *v++ = (struct vbufv){ 1, NORTH, x, y, z, unw, une, dnw, dne };
                        if (x == 0        || tiles[z  ][y  ][x-1] == OPEN) *v++ = (struct vbufv){ 1,  WEST, x, y, z, usw, unw, dsw, dnw };
                        if (x == TILESW-1 || tiles[z  ][y  ][x+1] == OPEN) *v++ = (struct vbufv){ 1,  EAST, x, y, z, une, use, dne, dse };
                        if (y == TILESH-1 || tiles[z  ][y+1][x  ] == OPEN) *v++ = (struct vbufv){ 2,  DOWN, x, y, z, dse, dsw, dne, dnw };
                }
                else if (tiles[z][y][x] == DIRT)
                {
                        if (y == 0        || tiles[z  ][y-1][x  ] == OPEN) *v++ = (struct vbufv){ 2,    UP, x, y, z, usw, use, unw, une };
                        if (z == 0        || tiles[z-1][y  ][x  ] == OPEN) *v++ = (struct vbufv){ 2, SOUTH, x, y, z, use, usw, dse, dsw };
                        if (z == TILESD-1 || tiles[z+1][y  ][x  ] == OPEN) *v++ = (struct vbufv){ 2, NORTH, x, y, z, unw, une, dnw, dne };
                        if (x == 0        || tiles[z  ][y  ][x-1] == OPEN) *v++ = (struct vbufv){ 2,  WEST, x, y, z, usw, unw, dsw, dnw };
                        if (x == TILESW-1 || tiles[z  ][y  ][x+1] == OPEN) *v++ = (struct vbufv){ 2,  EAST, x, y, z, une, use, dne, dse };
                        if (y == TILESH-1 || tiles[z  ][y+1][x  ] == OPEN) *v++ = (struct vbufv){ 2,  DOWN, x, y, z, dse, dsw, dne, dnw };
                }
        }
        TIMERSTOP(buildvbo)

        if (v > vbuf) {
                //printf("done, draw %ld verts\n", v - vbuf);
                polys += (v - vbuf) * 2;
                TIMER(glBufferData)
                glBufferData(GL_ARRAY_BUFFER, sizeof vbuf, vbuf, GL_STATIC_DRAW);
                TIMERSTOP(glBufferData)
                TIMER(glDrawArrays)
                glDrawArrays(GL_POINTS, 0, v - vbuf);
                TIMERSTOP(glDrawArrays)
                v = vbuf;
        }

        TIMER(swapwindow);
        SDL_GL_SwapWindow(win);
        TIMERSTOP(swapwindow);
}

void debrief()
{
        static unsigned last_ticks = 0;
        static unsigned last_frame = 0;
        unsigned ticks = SDL_GetTicks();

        if (ticks - last_ticks >= 1000) {
                if (noisy) {
                        float elapsed = ((float)ticks - last_ticks);
                        float frames = frame - last_frame;
                        printf("%.1f FPS\n", 1000.f * frames / elapsed );
                        printf("%.1f polys/sec\n", 1000.f * (float)polys / elapsed);
                        printf("%.1f polys/frame\n", (float)polys / frames);
                        printf("player pos %0.0f %0.0f %0.0f\n", player[0].pos.x, player[0].pos.y, player[0].pos.z);
                        timer_print();
                }
                last_ticks = ticks;
                last_frame = frame;
                polys = 0;
        }
}