#include "config.h"
#include "test.h"

void test_config(void)
{
    Arena *a = arena_new(4096);
    Config cfg = config_parse(a,
        "# comment\n"
        "[hide-system]\n"
        "  systemd-*   # trailing comment\n"
        "docker\n"
        "\n"
        "[apps]\n"
        "my-tool\n", "test");

    CHECK(cfg.hide_system.len == 2);
    CHECK(cfg.hide_user.len == 0);
    CHECK(cfg.apps.len == 1);
    CHECK_STR(cfg.hide_system.items[0], "systemd-*");          // trimmed, comment cut
    CHECK(config_match(&cfg.hide_system, "systemd-udevd"));     // wildcard
    CHECK(config_match(&cfg.hide_system, "docker"));
    CHECK(!config_match(&cfg.hide_system, "dockerd"));          // exact without *
    CHECK(!config_match(&cfg.hide_user, "anything"));

    Config def = config_parse(a, config_default_text, "defaults");
    CHECK(config_match(&def.hide_system, "NetworkManager"));
    CHECK(config_match(&def.hide_system, "getty@tty1"));
    CHECK(config_match(&def.hide_user, "xdg-desktop-portal-hyprland"));
    CHECK(!config_match(&def.hide_system, "docker"));
    arena_free(a);
}
