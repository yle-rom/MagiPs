#include "docker.h"

ContainerList docker_list(Arena *a)
{
    (void)a;
    // TODO: popen("docker ps --format ..."), split each line on tabs
    return (ContainerList){ 0 };
}
