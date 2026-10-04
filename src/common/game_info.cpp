#include "app/app.h"

const app::GameInfo app::kGame = {
    .title = L"Black - XBE Recomp",
    .short_title = L"Black",
    .name = L"Black",
    .log = "black",
    .settings = "black.toml",
    .splash = L"BlackLogo",
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with GlobData.bin and the "
               L"chars, data, export, language, levels, sound and videos folders beside it.",
    .images = {{"default.xbe", "Black"}},
    .options = {},
    .load = nullptr,
    .save = nullptr,
    .init = nullptr,
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = nullptr,
    .pad = nullptr,
    .rumble = nullptr,
};
