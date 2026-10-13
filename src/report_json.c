#include "report.h"
#include <stdlib.h>
#include <string.h>
#include "json.h"

static void json_pid(Json *j, int pid)
{
    if (pid >= 0)
        json_int(j, pid);
    else
        json_null(j);
}

static void json_members(Json *j, const Members *m)
{
    json_key(j, "pids");
    json_arr(j);
    for (size_t i = 0; i < m->len; i++)
        json_int(j, m->items[i].pid);
    json_end(j);
}

void report_json(Arena *a, const World *w)
{
    Json j = json_new(stdout);
    json_obj(&j);

    json_key(&j, "apps");
    json_arr(&j);
    for (size_t i = 0; i < w->apps.len; i++) {
        const App *app = &w->apps.items[i];
        json_obj(&j);
        json_key(&j, "name");      json_str(&j, app->name);
        json_key(&j, "pid");       json_int(&j, app->pid);
        json_key(&j, "mem_kb");    json_int(&j, app->mem_kb);
        json_key(&j, "uptime_s");  json_int(&j, world_age(app->start, w->uptime));
        json_members(&j, &app->members);
        json_end(&j);
    }
    json_end(&j);

    json_key(&j, "services");
    json_arr(&j);
    for (size_t i = 0; i < w->services.len; i++) {
        const Service *s = &w->services.items[i];
        json_obj(&j);
        json_key(&j, "name");      json_str(&j, s->name);
        json_key(&j, "scope");     json_str(&j, s->scope == SCOPE_SYSTEM ? "system" : "user");
        json_key(&j, "unit");      json_bool(&j, s->unit);
        json_key(&j, "pid");       json_int(&j, s->pid);
        json_key(&j, "mem_kb");    json_int(&j, s->mem_kb);
        json_key(&j, "uptime_s");  json_int(&j, world_age(s->start, w->uptime));
        json_members(&j, &s->members);
        json_end(&j);
    }
    json_end(&j);

    json_key(&j, "dev_servers");
    json_arr(&j);
    for (size_t i = 0; i < w->devservers.len; i++) {
        const DevServer *d = &w->devservers.items[i];
        json_obj(&j);
        json_key(&j, "project");   json_str(&j, d->project);
        json_key(&j, "command");   json_str(&j, d->command);
        json_key(&j, "ports");
        json_arr(&j);
        for (const char *p = d->ports; *p;) {       // "5173, 24678" -> [5173, 24678]
            char *end;
            long port = strtol(p, &end, 10);
            if (end == p)
                break;
            json_int(&j, port);
            p = end + strspn(end, ", ");
        }
        json_end(&j);
        json_key(&j, "pid");       json_int(&j, d->pid);
        json_key(&j, "uptime_s");  json_int(&j, world_age(d->start, w->uptime));
        json_key(&j, "orphaned");  json_bool(&j, d->orphaned);
        json_members(&j, &d->members);
        json_end(&j);
    }
    json_end(&j);

    json_key(&j, "databases");
    json_arr(&j);
    for (size_t i = 0; i < w->databases.len; i++) {
        const Database *d = &w->databases.items[i];
        json_obj(&j);
        json_key(&j, "engine");    json_str(&j, d->engine);
        json_key(&j, "guessed");   json_bool(&j, d->guessed);
        json_key(&j, "pid");       json_pid(&j, d->pid);
        json_key(&j, "port");
        if (d->port)
            json_int(&j, d->port);
        else
            json_null(&j);
        json_key(&j, "port_internal"); json_bool(&j, d->port_internal);
        json_key(&j, "container");
        json_str(&j, d->container >= 0 ? w->containers.items[d->container].name : NULL);
        json_key(&j, "service");   json_str(&j, d->service ? d->service->name : NULL);
        json_end(&j);
    }
    json_end(&j);

    json_key(&j, "containers");
    json_arr(&j);
    for (size_t i = 0; i < w->containers.len; i++) {
        const Container *c = &w->containers.items[i];
        json_obj(&j);
        json_key(&j, "id");        json_str(&j, c->id);
        json_key(&j, "name");      json_str(&j, c->name);
        json_key(&j, "image");     json_str(&j, c->image);
        json_key(&j, "ports");     json_str(&j, c->ports);
        json_key(&j, "status");    json_str(&j, c->status);
        json_end(&j);
    }
    json_end(&j);

    // every listening port, including the system ones the table hides
    json_key(&j, "ports");
    json_arr(&j);
    PortSummary *sums = arena_alloc(a, (w->ports.len + 1) * sizeof *sums);
    size_t n = ports_summarize(&w->ports, sums);
    for (size_t i = 0; i < n; i++) {
        json_obj(&j);
        json_key(&j, "port");      json_int(&j, sums[i].port);
        json_key(&j, "open_to");   json_str(&j, port_reach_label(&sums[i]));
        json_key(&j, "pid");       json_pid(&j, sums[i].pid);
        json_key(&j, "owner");     json_str(&j, world_port_owner(a, w, &sums[i]));
        json_end(&j);
    }
    json_end(&j);

    json_end(&j);
    json_done(&j);
}
