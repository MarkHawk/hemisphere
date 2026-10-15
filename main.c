/*
 * main.c  -  Hemisphere: the scenes this program offers, on keys 1 to 9 and 0;
 * , and . step through them all (Computers onwards, from the eleventh, are
 * only on those).
 * A new project is a new main.c listing its own scenes.
 */
#include "dome/dome.h"

extern Scene scene_deathstar, scene_planet, scene_plasma, scene_julia,
             scene_kaleido, scene_life, scene_moon, scene_orbit, scene_groovy, scene_scifi,
             scene_computers, scene_hypno, scene_balls, scene_retro,
             scene_screen;

int main(int argc, char **argv)
{
    Scene *const scenes[] = { &scene_deathstar, &scene_planet, &scene_plasma,
                              &scene_julia, &scene_kaleido, &scene_life,
                              &scene_moon, &scene_orbit, &scene_groovy,
                              &scene_scifi, &scene_computers,
                              &scene_hypno, &scene_balls, &scene_retro,
                              &scene_screen };
    return dome_run(argc, argv, "Hemisphere", scenes,
                    (int)(sizeof scenes / sizeof scenes[0]));
}
