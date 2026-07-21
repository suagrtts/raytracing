/*
 * Improved 2D Raytracing Engine
 * ==============================
 * GPU-accelerated ray casting with visual effects using SDL3.
 *
 * Controls:
 *   Mouse Move     - Move light source
 *   UP/DOWN        - Adjust ray count (100–2000)
 *   LEFT/RIGHT     - Adjust ray length
 *   T              - Toggle ray thickness (1, 2, 3)
 *   R              - Randomize obstacle positions & velocities
 *   SPACE          - Pause/resume obstacle movement
 *   ESC            - Quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

/* ─── Configuration ─────────────────────────────────────────── */

#define WIDTH            1200
#define HEIGHT           600
#define MAX_RAYS         2000
#define DEFAULT_RAYS     500
#define MIN_RAYS         100
#define RAY_STEP         50
#define DEFAULT_RAY_LEN  800
#define MIN_RAY_LEN      100
#define MAX_RAY_LEN      1600
#define RAY_LEN_STEP     50
#define OBSTACLE_COUNT   5
#define PI               3.14159265359
#define LIGHT_RADIUS     18
#define GLOW_RADIUS      80


/* ─── Color helpers (ARGB8888) ──────────────────────────────── */

static inline Uint32 argb(Uint8 a, Uint8 r, Uint8 g, Uint8 b)
{
    return ((Uint32)a << 24) | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

/* Blend src (with alpha) over dst (assumed opaque) */
static inline Uint32 alpha_blend(Uint32 dst, Uint32 src)
{
    Uint8 sa = (src >> 24) & 0xFF;
    if (sa == 0)   return dst;
    if (sa == 255) return src;

    Uint8 sr = (src >> 16) & 0xFF;
    Uint8 sg = (src >>  8) & 0xFF;
    Uint8 sb =  src        & 0xFF;
    Uint8 dr = (dst >> 16) & 0xFF;
    Uint8 dg = (dst >>  8) & 0xFF;
    Uint8 db =  dst        & 0xFF;

    Uint8 out_r = (sr * sa + dr * (255 - sa)) / 255;
    Uint8 out_g = (sg * sa + dg * (255 - sa)) / 255;
    Uint8 out_b = (sb * sa + db * (255 - sa)) / 255;

    return argb(255, out_r, out_g, out_b);
}

/* ─── Data Structures ───────────────────────────────────────── */

typedef struct {
    double x, y, r;
    Uint8 cr, cg, cb;       /* obstacle fill color */
    double vx, vy;           /* velocity */
} Obstacle;

typedef struct {
    double x, y;             /* light position */
    int    ray_count;
    int    ray_max_len;
    int    ray_thickness;    /* 1, 2, or 3 */
    int    paused;           /* obstacle movement paused */
} Scene;

/* ─── Random helpers ────────────────────────────────────────── */

static double rand_range(double lo, double hi)
{
    return lo + ((double)rand() / RAND_MAX) * (hi - lo);
}

/* ─── Obstacle helpers ──────────────────────────────────────── */

/* Pre-defined vibrant palette for obstacles */
static const Uint8 obstacle_palette[][3] = {
    { 0xFF, 0x6B, 0x6B },   /* coral red   */
    { 0x48, 0xDB, 0xFB },   /* sky blue    */
    { 0xFF, 0xD9, 0x3D },   /* golden      */
    { 0x6B, 0xCB, 0x77 },   /* mint green  */
    { 0xBB, 0x86, 0xFC },   /* lavender    */
};

static void randomize_obstacle(Obstacle *o, int index)
{
    o->r  = rand_range(30, 120);
    o->x  = rand_range(o->r + 100, WIDTH  - o->r);
    o->y  = rand_range(o->r,       HEIGHT - o->r);
    o->vx = rand_range(-120, 120);
    o->vy = rand_range(-120, 120);
    /* Ensure a minimum speed so obstacles always move */
    if (fabs(o->vx) < 30) o->vx = (o->vx >= 0 ? 30 : -30);
    if (fabs(o->vy) < 30) o->vy = (o->vy >= 0 ? 30 : -30);
    int ci = index % (int)(sizeof(obstacle_palette) / sizeof(obstacle_palette[0]));
    o->cr = obstacle_palette[ci][0];
    o->cg = obstacle_palette[ci][1];
    o->cb = obstacle_palette[ci][2];
}

static void init_obstacles(Obstacle obs[OBSTACLE_COUNT])
{
    for (int i = 0; i < OBSTACLE_COUNT; i++)
        randomize_obstacle(&obs[i], i);
}

static void update_obstacles(Obstacle obs[OBSTACLE_COUNT], double dt)
{
    for (int i = 0; i < OBSTACLE_COUNT; i++) {
        obs[i].x += obs[i].vx * dt;
        obs[i].y += obs[i].vy * dt;

        /* Bounce off walls */
        if (obs[i].x - obs[i].r < 0)      { obs[i].x = obs[i].r;              obs[i].vx = fabs(obs[i].vx);  }
        if (obs[i].x + obs[i].r > WIDTH)   { obs[i].x = WIDTH  - obs[i].r;     obs[i].vx = -fabs(obs[i].vx); }
        if (obs[i].y - obs[i].r < 0)      { obs[i].y = obs[i].r;              obs[i].vy = fabs(obs[i].vy);  }
        if (obs[i].y + obs[i].r > HEIGHT)  { obs[i].y = HEIGHT - obs[i].r;     obs[i].vy = -fabs(obs[i].vy); }
    }
}

/* ─── Pixel buffer drawing ──────────────────────────────────── */

static Uint32 pixels[WIDTH * HEIGHT];

static inline void put_pixel(int x, int y, Uint32 color)
{
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
        pixels[y * WIDTH + x] = color;
}

static inline void blend_pixel(int x, int y, Uint32 color)
{
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
        pixels[y * WIDTH + x] = alpha_blend(pixels[y * WIDTH + x], color);
}

static void draw_gradient_background(void)
{
    for (int y = 0; y < HEIGHT; y++) {
        /* Dark navy gradient: top → bottom */
        double t = (double)y / HEIGHT;
        Uint8 r = (Uint8)(5  + t * 8);
        Uint8 g = (Uint8)(5  + t * 12);
        Uint8 b = (Uint8)(15 + t * 25);
        Uint32 c = argb(255, r, g, b);
        for (int x = 0; x < WIDTH; x++)
            pixels[y * WIDTH + x] = c;
    }
}

static void draw_filled_circle(double cx, double cy, double radius,
                                Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    int x0 = (int)(cx - radius);
    int x1 = (int)(cx + radius);
    int y0 = (int)(cy - radius);
    int y1 = (int)(cy + radius);
    double r2 = radius * radius;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx;
            double dy = y - cy;
            double d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                /* Anti-alias edge: smooth alpha falloff on the last 1.5 pixels */
                double d = sqrt(d2);
                Uint8 a = ca;
                if (d > radius - 1.5)
                    a = (Uint8)(ca * (radius - d) / 1.5);
                blend_pixel(x, y, argb(a, cr, cg, cb));
            }
        }
    }
}

/* ─── Glow halo around light source ────────────────────────── */

static void draw_glow(double cx, double cy, double radius)
{
    int x0 = (int)(cx - radius);
    int x1 = (int)(cx + radius);
    int y0 = (int)(cy - radius);
    int y1 = (int)(cy + radius);
    double r2 = radius * radius;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            double dx = x - cx;
            double dy = y - cy;
            double d2 = dx * dx + dy * dy;
            if (d2 < r2) {
                double t = 1.0 - sqrt(d2) / radius;   /* 1 at center, 0 at edge */
                t = t * t;                              /* quadratic falloff */
                Uint8 a = (Uint8)(90 * t);
                /* Warm white glow */
                blend_pixel(x, y, argb(a, 255, 230, 180));
            }
        }
    }
}

/* ─── Ray casting ───────────────────────────────────────────── */

static void cast_rays(Scene *scene, Obstacle obs[OBSTACLE_COUNT])
{
    int thickness = scene->ray_thickness;

    for (int i = 0; i < scene->ray_count; i++) {
        double angle = ((double)i / scene->ray_count) * 2.0 * PI;
        double cos_a = cos(angle);
        double sin_a = sin(angle);
        double dx = scene->x;
        double dy = scene->y;
        int    steps = 0;
        int    max_steps = scene->ray_max_len;
        int    hit = 0;

        while (steps < max_steps && !hit) {
            dx += cos_a;
            dy += sin_a;
            steps++;

            int ix = (int)dx;
            int iy = (int)dy;

            /* Out of bounds */
            if (ix < 0 || ix >= WIDTH || iy < 0 || iy >= HEIGHT)
                break;

            /* Check obstacle collision */
            for (int j = 0; j < OBSTACLE_COUNT; j++) {
                double odx = dx - obs[j].x;
                double ody = dy - obs[j].y;
                if (odx * odx + ody * ody < obs[j].r * obs[j].r) {
                    hit = 1;
                    break;
                }
            }
            if (hit) break;

            /* Color: warm yellow/orange fading to transparent */
            double progress = (double)steps / max_steps;
            double fade = 1.0 - progress;
            fade = fade * fade;  /* quadratic falloff for softer look */

            Uint8 a = (Uint8)(180 * fade);
            /* Gradient from bright warm yellow → dim orange-red */
            Uint8 r = (Uint8)(255 - 60 * progress);
            Uint8 g = (Uint8)(210 - 150 * progress);
            Uint8 b = (Uint8)(50  - 40 * progress);

            if (a < 3) break;  /* Skip nearly invisible pixels */

            /* Draw with thickness */
            if (thickness == 1) {
                blend_pixel(ix, iy, argb(a, r, g, b));
            } else {
                int half = thickness / 2;
                for (int ty = -half; ty <= half; ty++)
                    for (int tx = -half; tx <= half; tx++)
                        blend_pixel(ix + tx, iy + ty, argb(a, r, g, b));
            }
        }
    }
}

/* ─── Render frame ──────────────────────────────────────────── */

static void render_frame(SDL_Renderer *renderer, SDL_Texture *texture,
                          Scene *scene, Obstacle obs[OBSTACLE_COUNT])
{
    /* 1. Gradient background */
    draw_gradient_background();

    /* 2. Cast rays */
    cast_rays(scene, obs);

    /* 3. Draw obstacles (colored circles with anti-aliased edges) */
    for (int i = 0; i < OBSTACLE_COUNT; i++) {
        /* Darker outline ring */
        draw_filled_circle(obs[i].x, obs[i].y, obs[i].r + 2,
                           obs[i].cr / 3, obs[i].cg / 3, obs[i].cb / 3, 180);
        /* Main body */
        draw_filled_circle(obs[i].x, obs[i].y, obs[i].r,
                           obs[i].cr, obs[i].cg, obs[i].cb, 230);
        /* Inner highlight (smaller, brighter circle) */
        draw_filled_circle(obs[i].x - obs[i].r * 0.2, obs[i].y - obs[i].r * 0.2,
                           obs[i].r * 0.4,
                           (Uint8)fmin(obs[i].cr + 40, 255),
                           (Uint8)fmin(obs[i].cg + 40, 255),
                           (Uint8)fmin(obs[i].cb + 40, 255), 80);
    }

    /* 4. Light source glow + circle */
    draw_glow(scene->x, scene->y, GLOW_RADIUS);
    draw_filled_circle(scene->x, scene->y, LIGHT_RADIUS,
                       255, 240, 200, 255);
    /* Bright core */
    draw_filled_circle(scene->x, scene->y, LIGHT_RADIUS * 0.5,
                       255, 255, 255, 255);

    /* 5. Upload pixel buffer to GPU texture and present */
    SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(Uint32));
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* ─── Main ──────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    /* Initialize SDL */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Raytracing Engine", WIDTH, HEIGHT, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        WIDTH, HEIGHT);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Scene setup */
    Scene scene;
    scene.x             = WIDTH  / 2.0;
    scene.y             = HEIGHT / 2.0;
    scene.ray_count     = DEFAULT_RAYS;
    scene.ray_max_len   = DEFAULT_RAY_LEN;
    scene.ray_thickness = 1;
    scene.paused        = 0;

    Obstacle obstacles[OBSTACLE_COUNT];
    init_obstacles(obstacles);

    /* Timing */
    Uint64 freq       = SDL_GetPerformanceFrequency();
    Uint64 last_time  = SDL_GetPerformanceCounter();
    int    frame_count = 0;
    double fps_timer   = 0;

    /* Main loop */
    int running = 1;
    SDL_Event event;

    while (running) {
        /* ── Timing ── */
        Uint64 now = SDL_GetPerformanceCounter();
        double dt  = (double)(now - last_time) / (double)freq;
        last_time  = now;
        if (dt > 0.05) dt = 0.05;  /* Clamp large dt spikes */

        /* FPS counter */
        frame_count++;
        fps_timer += dt;
        if (fps_timer >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title),
                     "Raytracing Engine  |  FPS: %d  |  Rays: %d  |  Len: %d  |  [ARROWS] rays/len  [T] thick  [R] reset  [SPACE] pause",
                     frame_count, scene.ray_count, scene.ray_max_len);
            SDL_SetWindowTitle(window, title);
            frame_count = 0;
            fps_timer  -= 1.0;
        }

        /* ── Events ── */
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = 0;
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    scene.x = event.motion.x;
                    scene.y = event.motion.y;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    switch (event.key.key) {
                        case SDLK_ESCAPE:
                            running = 0;
                            break;
                        case SDLK_UP:
                            scene.ray_count += RAY_STEP;
                            if (scene.ray_count > MAX_RAYS) scene.ray_count = MAX_RAYS;
                            break;
                        case SDLK_DOWN:
                            scene.ray_count -= RAY_STEP;
                            if (scene.ray_count < MIN_RAYS) scene.ray_count = MIN_RAYS;
                            break;
                        case SDLK_RIGHT:
                            scene.ray_max_len += RAY_LEN_STEP;
                            if (scene.ray_max_len > MAX_RAY_LEN) scene.ray_max_len = MAX_RAY_LEN;
                            break;
                        case SDLK_LEFT:
                            scene.ray_max_len -= RAY_LEN_STEP;
                            if (scene.ray_max_len < MIN_RAY_LEN) scene.ray_max_len = MIN_RAY_LEN;
                            break;
                        case SDLK_T:
                            scene.ray_thickness = (scene.ray_thickness % 3) + 1;
                            break;
                        case SDLK_R:
                            init_obstacles(obstacles);
                            break;
                        case SDLK_SPACE:
                            scene.paused = !scene.paused;
                            break;
                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }
        }

        /* ── Update ── */
        if (!scene.paused)
            update_obstacles(obstacles, dt);

        /* ── Render ── */
        render_frame(renderer, texture, &scene, obstacles);

    }

    /* Cleanup */
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}