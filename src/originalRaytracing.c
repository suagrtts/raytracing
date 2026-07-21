#include<stdio.h>
#include<stdlib.h>
#include<math.h>

#include<SDL3/SDL.h>

#define WIDTH 1200
#define HEIGHT 600
#define COLOR_WHITE 0xffffffff
#define COLOR_BLACK 0x00000000
#define RAY_COLOR 0xffd82b
#define RAYS_NUMBER 500
#define PI 3.14159265359
#define RAY_THICKNESS 1
#define OBSTACLE_AMOUNT 3

struct Circle{
    double x;
    double y;
    double r;
};

struct Ray{
    double x_start, y_start;
    double angle;
};


void fillCircle(SDL_Surface* surface,struct Circle circle, Uint32 color)
{
    double radius_squared = pow(circle.r, 2);
    for(double x = circle.x - circle.r; x <= circle.x + circle.r; x++)
    {
        for(double y = circle.y - circle.r; y <= circle.y + circle.r; y++)
        {
            double distance_squared = pow(x- circle.x, 2) + pow(y - circle.y, 2);

            if(distance_squared < radius_squared)
            {
                SDL_Rect point = (SDL_Rect) {(int)x, (int)y, RAY_THICKNESS, RAY_THICKNESS};
                SDL_FillSurfaceRect(surface, &point, color);
            }
        }
    }
}

void generate_rays(struct Circle circle, struct Ray rays[RAYS_NUMBER])
{
    for(int i = 0; i < RAYS_NUMBER; i++)
    {
        double angle = ((double) i / RAYS_NUMBER) * 2 * PI;
        struct Ray ray = {circle.x, circle.y, angle};
        rays[i] = ray;
        printf("%f\n", angle);
    }
}

void fill_rays(SDL_Surface* surface, struct Ray rays[RAYS_NUMBER], Uint32 color, struct Circle object[OBSTACLE_AMOUNT])
{

    for(int i = 0; i < RAYS_NUMBER; i++)
    {
        int end_of_screen = 0;
        int object_hit = 0;

        struct Ray ray = rays[i];

        int step = 1;
        double dx = ray.x_start;
        double dy = ray.y_start;
        while(!end_of_screen && !object_hit)
        {
            dx += step*cos(ray.angle);
            dy += step*sin(ray.angle);

            if(dx < 0 || dx > WIDTH)
            {
                    end_of_screen = 1;
                    break;
            }
            if(dy < 0 || dy > HEIGHT)
            {
                    end_of_screen = 1;
                    break;
            }

            for(int j = 0; j < OBSTACLE_AMOUNT; j++)
            {
                double radius_squared = pow(object[j].r, 2);
                double distance_squared = pow(dx - object[j].x, 2) + pow(dy - object[j].y, 2);
                if(distance_squared < radius_squared)
                {
                    object_hit = 1;
                    break;
                }
            }

            SDL_Rect pixel = (SDL_Rect) {dx, dy, 1, 1};
            SDL_FillSurfaceRect(surface, &pixel, color);
        }
    }
}

int main()
{
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Raytracing", WIDTH, HEIGHT, 0);
    SDL_Surface* surface = SDL_GetWindowSurface(window);

    struct Circle obstacles[OBSTACLE_AMOUNT];

    struct Circle circle = {200,200,40};

    struct Circle shadow_circle = {200,100,100};
    struct Circle obstacle1 = {950,250,150};
    struct Circle obstacle2 = {600,350,40};
    obstacles[0] = shadow_circle;
    obstacles[1] = obstacle1;
    obstacles[2] = obstacle2;

    SDL_Rect erase_rect = {0,0,WIDTH, HEIGHT};

    struct Ray rays[RAYS_NUMBER];
    generate_rays(circle, rays);

    int isRunning = 1;
    SDL_Event event;
    double obstacle_speed_y = 3;
    while(isRunning)
    {
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_EVENT_QUIT)
                isRunning = 0;
            if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.state != 0)
            {
                circle.x = event.motion.x;
                circle.y = event.motion.y;
                generate_rays(circle, rays);
            }
        }
        SDL_FillSurfaceRect(surface, &erase_rect, COLOR_BLACK);

        fill_rays(surface, rays, RAY_COLOR, obstacles);
        fillCircle(surface, circle, COLOR_WHITE);
        fillCircle(surface, obstacles[0], COLOR_WHITE);
        fillCircle(surface, obstacles[1], COLOR_WHITE);
        fillCircle(surface, obstacles[2], COLOR_WHITE);

        for(int i = 0; i < OBSTACLE_AMOUNT; i++)
        {
            obstacles[i].y += obstacle_speed_y;
            if(obstacles[i].y + obstacles[i].r > HEIGHT)
                obstacle_speed_y = -obstacle_speed_y;
            if(obstacles[i].y - obstacles[i].r < 0)
                obstacle_speed_y = -obstacle_speed_y;
        }
        SDL_UpdateWindowSurface(window);
        SDL_Delay(10);
    }
}
