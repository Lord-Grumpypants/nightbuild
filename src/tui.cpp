#include "tui.hpp"

#include <notcurses/notcurses.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void put(
    ncplane *plane,
    int y,
    int x,
    const std::string &text)
{
    ncplane_putstr_yx(
        plane,
        y,
        x,
        text.c_str());
}

std::string repeat_utf8(
    const char *text,
    size_t count)
{
    const size_t length =
        std::strlen(text);

    std::string result;
    result.reserve(length * count);

    for (size_t i = 0; i < count; ++i)
        result += text;

    return result;
}

void draw_box(
    ncplane *plane,
    unsigned y,
    unsigned x,
    unsigned height,
    unsigned width)
{
    if (height < 2 || width < 2)
        return;

    const std::string horizontal =
        repeat_utf8("─", width - 2);

    put(
        plane,
        static_cast<int>(y),
        static_cast<int>(x),
        "┌" + horizontal + "┐");

    for (unsigned row = 1;
         row < height - 1;
         ++row) {

        put(
            plane,
            static_cast<int>(y + row),
            static_cast<int>(x),
            "│");

        put(
            plane,
            static_cast<int>(y + row),
            static_cast<int>(x + width - 1),
            "│");
    }

    put(
        plane,
        static_cast<int>(y + height - 1),
        static_cast<int>(x),
        "└" + horizontal + "┘");
}

void draw_progress_bar(
    ncplane *plane,
    unsigned y,
    unsigned x,
    unsigned width,
    double progress)
{
    if (width < 2)
        return;

    progress =
        std::clamp(
            progress,
            0.0,
            1.0);

    const unsigned filled =
        static_cast<unsigned>(
            progress * width);

    const std::string bar =
        repeat_utf8(
            "█",
            filled) +
        repeat_utf8(
            "░",
            width - filled);

    put(
        plane,
        static_cast<int>(y),
        static_cast<int>(x),
        bar);
}

void draw_header(
    ncplane *plane,
    unsigned cols)
{
    if (cols < 30)
        return;

    const std::string title =
        "NIGHTBUILD";

    const std::string subtitle =
        "Native Content-Addressed Build System";

    put(
        plane,
        1,
        static_cast<int>(
            (cols - title.size()) / 2),
        title);

    put(
        plane,
        2,
        static_cast<int>(
            (cols - subtitle.size()) / 2),
        subtitle);

    put(
        plane,
        3,
        2,
        repeat_utf8("─", cols - 4));
}

void draw_build_screen(
    ncplane *plane,
    unsigned rows,
    unsigned cols)
{
    if (cols < 40 || rows < 15)
        return;

    const unsigned box_width =
        std::min<unsigned>(
            cols - 6,
            82);

    const unsigned box_height =
        13;

    const unsigned box_x =
        (cols - box_width) / 2;

    const unsigned box_y =
        (rows - box_height) / 2;

    draw_header(
        plane,
        cols);

    draw_box(
        plane,
        box_y,
        box_x,
        box_height,
        box_width);

    put(
        plane,
        static_cast<int>(box_y + 2),
        static_cast<int>(box_x + 3),
        "BUILDING");

    put(
        plane,
        static_cast<int>(box_y + 4),
        static_cast<int>(box_x + 3),
        "NightBuild");

    put(
        plane,
        static_cast<int>(box_y + 5),
        static_cast<int>(box_x + 3),
        "Preparing build...");

    draw_progress_bar(
        plane,
        box_y + 7,
        box_x + 3,
        box_width - 6,
        0.25);

    put(
        plane,
        static_cast<int>(box_y + 9),
        static_cast<int>(box_x + 3),
        "Resolving build commands");

    put(
        plane,
        static_cast<int>(box_y + 10),
        static_cast<int>(box_x + 3),
        "Content-addressed incremental build");

    put(
        plane,
        static_cast<int>(box_y + 11),
        static_cast<int>(box_x + 3),
        "Please wait...");
}

void draw_result_screen(
    ncplane *plane,
    unsigned rows,
    unsigned cols,
    int result)
{
    if (cols < 40 || rows < 15)
        return;

    const unsigned box_width =
        std::min<unsigned>(
            cols - 6,
            82);

    const unsigned box_height =
        11;

    const unsigned box_x =
        (cols - box_width) / 2;

    const unsigned box_y =
        (rows - box_height) / 2;

    draw_header(
        plane,
        cols);

    draw_box(
        plane,
        box_y,
        box_x,
        box_height,
        box_width);

    if (result == 0) {
        put(
            plane,
            static_cast<int>(box_y + 2),
            static_cast<int>(box_x + 3),
            "BUILD COMPLETE");

        draw_progress_bar(
            plane,
            box_y + 4,
            box_x + 3,
            box_width - 6,
            1.0);

        put(
            plane,
            static_cast<int>(box_y + 6),
            static_cast<int>(box_x + 3),
            "Build finished successfully.");

        put(
            plane,
            static_cast<int>(box_y + 7),
            static_cast<int>(box_x + 3),
            "All requested targets are up to date.");
    } else {
        put(
            plane,
            static_cast<int>(box_y + 2),
            static_cast<int>(box_x + 3),
            "BUILD FAILED");

        put(
            plane,
            static_cast<int>(box_y + 4),
            static_cast<int>(box_x + 3),
            "NightBuild returned an error.");

        put(
            plane,
            static_cast<int>(box_y + 6),
            static_cast<int>(box_x + 3),
            "Check the build output.");
    }

    put(
        plane,
        static_cast<int>(box_y + box_height - 2),
        static_cast<int>(box_x + 3),
        "Press any key to exit.");
}

} // namespace

int build_tui(
    const fs::path &build_dir,
    int (*build_function)(
        const fs::path &,
        bool))
{
    struct notcurses_options options {};

    options.flags =
        NCOPTION_SUPPRESS_BANNERS;

    notcurses *nc =
        notcurses_init(
            &options,
            nullptr);

    if (!nc)
        return 1;

    ncplane *plane =
        notcurses_stdplane(nc);

    unsigned rows = 0;
    unsigned cols = 0;

    ncplane_dim_yx(
        plane,
        &rows,
        &cols);

    ncplane_erase(plane);

    draw_build_screen(
        plane,
        rows,
        cols);

    notcurses_render(nc);

    const int result =
        build_function(
            build_dir,
            false);

    ncplane_erase(plane);

    ncplane_dim_yx(
        plane,
        &rows,
        &cols);

    draw_result_screen(
        plane,
        rows,
        cols,
        result);

    notcurses_render(nc);

    ncinput input {};

    notcurses_get_blocking(
        nc,
        &input);

    notcurses_stop(nc);

    return result;
}