#pragma once
#include "grpp/frontend.hpp"
#include "original_frontend.hpp"
#include "text_observation.hpp"
#include <sstream>
namespace frontend_check {
inline const std::array<const grpp::MenuSpec *, 10> menus = {
    {&grpp::original::mainMenu, &grpp::original::aboutMenu, &grpp::original::playersMenu,
     &grpp::original::slotMenu, &grpp::original::clearMenu, &grpp::original::levelMenu,
     &grpp::original::nameMenu, &grpp::original::readerMenu, &grpp::original::continueMenu,
     &grpp::original::exitMenu}};
inline uint64_t wordHash(uint64_t hash, uint32_t word) {
    for (int b = 0; b < 4; ++b)
        hash = (hash ^ uint8_t(word >> (b * 8))) * 1099511628211ull;
    return hash;
}
inline uint64_t cellsHash(const grpp::TileMap &map) {
    uint64_t h = 14695981039346656037ull;
    for (int y = 0; y < map.rows(); ++y)
        for (int x = 0; x < map.columns(); ++x)
            h = wordHash(h, map.get(x, y));
    return h;
}
inline uint64_t scoresHash(const std::array<grpp::Score, 10> &table) {
    uint64_t h = 14695981039346656037ull;
    for (const auto &s : table) {
        h = wordHash(h, s.points);
        for (size_t i = 0; i < 20; ++i)
            h = (h ^ (i < s.name.size() ? uint8_t(s.name[i]) : uint8_t(0))) * 1099511628211ull;
    }
    return h;
}
inline std::array<std::array<int32_t, 3>, 1000> stars() {
    grpp::GameRandom r(1982);
    std::array<std::array<int32_t, 3>, 1000> result;
    for (auto &s : result) {
        s[0] = grpp::mul32(r.below(4095) - 2047, 0x100000);
        s[1] = grpp::mul32(r.below(4095) - 2047, 0x100000);
        s[2] = grpp::fixed16(r.below(4095));
    }
    return result;
}
inline const std::array<uint32_t, 9> points = {
    {0u, 1u, 28000u, 50000u, 100000u, 0x7fffffffu, 0x80000000u, 0xffffffffu, 48000u}};
inline std::string nativeEvidence(const grpp::Assets &assets) {
    using namespace grpp;
    std::ostringstream out;
    for (int kind = 0; kind < 3; ++kind)
        for (uint32_t seed : {1u, 1982u, 0xdeadbeefu}) {
            EntityPool pool;
            for (size_t i = 0; i < EntityPool::Capacity; ++i) {
                auto &e = pool.at(i);
                e.x = 1;
                e.y = 2;
                e.vx = 3;
                e.vy = 4;
                e.sprite = 7;
                e.behavior = 19;
                e.drawing = 16;
                e.animation = 1;
                e.animationBase = 9;
            }
            GameRandom random(seed);
            seedMenuParticles(pool, random, kind == 0 ? 32 : 20, MenuPopulation(kind));
            uint64_t h = 14695981039346656037ull;
            for (size_t i = 0; i < EntityPool::Capacity; ++i) {
                const auto &e = pool.at(i);
                for (int v : {e.x, e.y, e.vx, e.vy, e.sprite, e.behavior, e.drawing, e.animation,
                              e.animationBase})
                    h = wordHash(h, uint32_t(v));
            }
            out << "population " << kind << ' ' << seed << ' ' << random.state() << ' ' << h
                << '\n';
        }
    for (size_t menu = 0; menu < menus.size(); ++menu)
        for (int pad : {0, 8}) {
            TileMap map(240, 320);
            auto bounds = layoutMenu(map, *menus[menu], pad);
            out << "menu " << menu << ' ' << pad << ' ' << cellsHash(map);
            for (const auto &r : bounds) {
                for (auto v : r)
                    out << ' ' << v;
                for (int x : {r[0], r[0] + 1, r[2] - 1, r[2]})
                    for (int y : {r[1], r[1] + 1, r[3] - 1, r[3]})
                        out << ' ' << menuHit(bounds, x, y);
            }
            out << '\n';
        }
    for (unsigned slot = 0; slot < 5; ++slot)
        for (auto score : points) {
            auto s = original::defaultScores();
            auto name = slot < 4 ? s.players[slot].name : s.guest.name;
            int rank = s.insertNormal({name, score});
            out << "rank " << slot << ' ' << score << ' ' << rank << ' '
                << (rank == 10 ? 0u : score) << ' ' << scoresHash(s.normal) << '\n';
        }
    for (unsigned stage = 0; stage < 50; ++stage)
        for (auto score : points) {
            auto s = original::defaultScores();
            s.underworld[stage] = {"PRIOR", 1000};
            s.recordStage(stage, signedBits(score), s.players[0].name);
            out << "record " << stage << ' ' << score << ' ' << s.underworld[stage].points << ' '
                << s.underworld[stage].name << '\n';
        }
    for (unsigned slot = 0; slot < 5; ++slot)
        for (int savedLives : {0, 3})
            for (unsigned savedLevel : {0u, 4u, 50u})
                for (int lives : {-1, 0, 2, 3, 4, 11})
                    for (unsigned level : {0u, 3u, 4u, 49u, 50u}) {
                        auto s = original::defaultScores();
                        auto &p = slot < 4 ? s.players[slot] : s.guest;
                        p.lives = uint32_t(savedLives);
                        p.level = savedLevel;
                        p.score = 777;
                        p.difficulty = 6;
                        s.recordCheckpoint(slot, lives, level, 123456, 2);
                        out << "checkpoint " << slot << ' ' << savedLives << ' ' << savedLevel
                            << ' ' << lives << ' ' << level << ' ' << p.lives << ' ' << p.level
                            << ' ' << p.score << ' ' << p.difficulty << '\n';
                    }
    const auto starfield = stars();
    for (int frame : {0, 1, 2, 31, 63, 64, 100, 1023, 65535}) {
        Canvas canvas;
        canvas.clear(0x1234);
        drawMenuStars(canvas, starfield, frame);
        Raster565 raster;
        raster.render(canvas, assets);
        out << "stars " << frame << ' ' << text_check::pixelHash(raster.pixels()) << '\n';
    }
    for (size_t doc = 0; doc < 4; ++doc) {
        auto pages = paginateInformation(original::information[doc]);
        for (size_t p = 0; p < pages.size(); ++p)
            out << "reader " << doc << ' ' << p << ' ' << cellsHash(pages[p]) << '\n';
        size_t page = 0;
        for (int step = 0; step < 12; ++step) {
            out << "reader-back " << doc << ' ' << step << ' ' << cellsHash(pages[page]) << '\n';
            if (step == 1)
                --page;
            else if (++page == pages.size())
                break;
        }
    }
    return out.str();
}
} // namespace frontend_check
