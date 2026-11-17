#include "grpp/frontend.hpp"
#include "aof_frontend.hpp"
#include "grpp/background.hpp"
#include "original_frontend.hpp"
#include "original_levels.hpp"
#include "original_visuals.hpp"
#include <algorithm>
#include <cstdio>
namespace grpp {
std::vector<std::array<int, 4>> layoutMenu(TileMap &map, const MenuSpec &spec, int padding,
                                           const std::vector<std::string> &labels) {
    map.centeredText(spec.title, spec.row, (uint32_t(spec.titleColor) << 16) | 0xff00u);
    std::vector<std::array<int, 4>> bounds;
    for (size_t i = 0; i < spec.items.size(); ++i) {
        auto rect = spec.items[i].bounds;
        const char *label = i < labels.size() ? labels[i].c_str() : spec.items[i].label;
        if (label) {
            const int color = spec.items[i].color >= 0 ? spec.items[i].color
                                                       : spec.firstColor + int(i) * spec.colorStep;
            auto r = map.centeredText(label, spec.row + spec.gap + int(i) * spec.itemStep,
                                      (uint32_t(color) << 16) | 0xff00u);
            rect = {r.x - padding, r.y - padding, r.x + r.width, r.y + r.height + padding};
        }
        bounds.push_back(rect);
    }
    return bounds;
}
int menuHit(std::vector<std::array<int, 4>> &bounds, int x, int y) {
    for (size_t i = 0; i < bounds.size(); ++i) {
        auto &r = bounds[i];
        if (r[0] < x && x < r[2] && r[1] < y && y < r[3]) {
            if (r[2] >= 0 && r[0] <= 239 && r[3] >= 0 && r[1] <= 319) {
                r[0] = std::max(0, r[0]);
                r[1] = std::max(0, r[1]);
                r[2] = std::min(239, r[2]);
                r[3] = std::min(319, r[3]);
            }
            return int(i);
        }
    }
    return -1;
}
void drawMenuStars(Canvas &canvas, const std::array<std::array<int32_t, 3>, 1000> &stars,
                   int frame) {
    const int travel = mul32(frame, -0x40000);
    for (const auto &star : stars) {
        const int depth = int((uint32_t(add32(travel, star[2])) & 0xffffffu) + 1);
        const int x = add32(star[0] / depth, 120), y = add32(star[1] / depth, 160);
        if (x > 0 && x < 240 && y > 0 && y < 320) {
            const auto first = canvas.draws().size();
            canvas.rawFill({x, y, 1, 1}, 0xffff);
            canvas.referenceOnlySince(first);
        }
    }
    DecorativeStars field;
    field.valid = true;
    field.frame = frame;
    field.stars = stars;
    field.first = canvas.draws().size();
    drawPreciseStars(canvas, field);
    field.last = canvas.draws().size();
    canvas.starField(field);
}
std::vector<TileMap> paginateInformation(const std::vector<std::string> &paragraphs) {
    const int colors[8] = {0, 65, 1, 66, 5, 70, 9, 80};
    std::vector<TileMap> pages;
    size_t paragraph = 0;
    uint32_t color = 0;
    while (paragraph < paragraphs.size()) {
        const auto begin = paragraph;
        TileMap map(240, 320);
        layoutMenu(map, original::readerMenu);
        int row = 3, x = 1;
        size_t cursor = 0;
        while (row < 30 && paragraph < paragraphs.size()) {
            const auto &text = paragraphs[paragraph];
            while (cursor < text.size() && text[cursor] == ' ')
                ++cursor;
            if (cursor == text.size()) {
                ++paragraph;
                cursor = 0;
                continue;
            }
            size_t end = cursor;
            int visible = 0;
            for (; end < text.size() && text[end] != ' '; ++end) {
                if (text[end] == '%') {
                    if (end + 1 < text.size() && text[end + 1] >= '0' && text[end + 1] <= '9')
                        ++end;
                } else
                    ++visible;
            }
            if ((x - 1) + visible + 1 >= 30) {
                ++row;
                x = 1;
                continue;
            }
            bool lineBreak = false;
            const int wordRow = row;
            while (cursor < end) {
                const auto c = text[cursor++];
                if (c == '%') {
                    if (cursor < end && text[cursor] >= '0' && text[cursor] <= '9') {
                        const int control = text[cursor++] - '0';
                        if (control == 9) {
                            ++row;
                            lineBreak = true;
                        } else if (control < 8)
                            color = uint32_t(colors[control]) << 16;
                    } else
                        color = 0;
                } else
                    map.put(x++, wordRow, uint32_t(uint8_t(c)) | 0xff00u | color);
            }
            // %9 consumes its digit without incrementing the source's output
            // loop counter. A trailing literal space therefore becomes a keyed
            // glyph in the next row, rather than the ordinary blank separator.
            if (lineBreak && cursor == end && cursor < text.size() && text[cursor] == ' ') {
                map.put(x++, wordRow, 0xff20u | color);
                ++cursor;
            }
            if (cursor == text.size()) {
                ++paragraph;
                cursor = 0;
            } else
                ++cursor;
            map.put(x++, row, 32);
            if (lineBreak) {
                ++row;
                x = 1;
            }
        }
        pages.push_back(std::move(map));
        if (paragraph == begin)
            throw std::runtime_error("Original information page makes no paragraph progress");
    }
    return pages;
}
Frontend::Frontend(Assets &assets, std::filesystem::path saveFile)
    : assets_(assets), game_(assets), scores_(original::defaultScores()),
      saveFile_(std::move(saveFile)) {
    game_.combatState_.shotColorPhase = .1f;
    if (!saveFile_.empty() && std::filesystem::exists(saveFile_)) {
        try {
            scores_.load(saveFile_);
            freshProfile_ = false;
        } catch (const std::runtime_error &) {
            auto backup = saveFile_;
            backup += ".bak";
            try {
                scores_.load(backup);
                freshProfile_ = false;
                saveError_ = "RECOVERED BACKUP";
            } catch (const std::runtime_error &) {
                saveError_ = "INVALID SAVE - DEFAULTS USED";
            }
        }
    }
    aofScores_ = aof::defaultScores();
    // 0040b044 resets the raw image's initial unlock before reading hst.txt.
    aofScores_.alternateCraftUnlocked = 0;
    aofScores_.extendedFormat = true;
    if (!saveFile_.empty()) {
        aofSaveFile_ = saveFile_.parent_path() / "aof-hst.txt";
        if (std::filesystem::exists(aofSaveFile_)) {
            try {
                aofScores_.load(aofSaveFile_);
            } catch (const std::runtime_error &) {
                try {
                    auto backup = aofSaveFile_;
                    backup += ".bak";
                    aofScores_.load(backup);
                    saveError_ = "RECOVERED AOF BACKUP";
                } catch (const std::runtime_error &) {
                    saveError_ = "INVALID AOF SAVE - DEFAULTS USED";
                }
            }
        }
    }
    aofScores_.extendedFormat = true;
    game_.records_ = &scores_;
    game_.player_.selectedPlayer = int(scores_.selectedPlayer);
    game_.player_.alternateCraftUnlocked = scores_.alternateCraftUnlocked != 0;
    enter(FrontendView::Splash);
}
void Frontend::editionMenu(bool enabled) {
    game_.stageBackgrounds(enabled);
    if (enabled && !editionMenu_ && freshProfile_) {
        scores_.selectedPlayer = 0;
        game_.player_.selectedPlayer = 0;
        dirty_ = true;
        flush();
        freshProfile_ = false;
    }
    editionMenu_ = enabled;
    if (view_ == FrontendView::Main)
        rebuild();
}
SaveSlot &Frontend::playerSlot() {
    return scores_.selectedPlayer < 4 ? scores_.players[scores_.selectedPlayer] : scores_.guest;
}
const std::string &Frontend::playerName() const {
    return scores_.selectedPlayer < 4 ? scores_.players[scores_.selectedPlayer].name
                                      : scores_.guest.name;
}
void Frontend::flush() {
    const auto &activeSave = aofActive_ ? aofSaveFile_ : saveFile_;
    if (!dirty_)
        return;
    if (activeSave.empty()) {
        dirty_ = false;
        return;
    }
    try {
        scores_.saveAtomic(activeSave);
        dirty_ = false;
        saveError_.clear();
    } catch (const std::exception &error) {
        saveError_ = std::string("SAVE FAILED: ") + error.what();
    }
}
bool Frontend::requestQuit() {
    if (replaysEnabled_ && replay_.pending()) {
        quitAfterReplay_ = true;
        return false;
    }
    if (replaysEnabled_ && replay_.active()) {
        quitAfterReplay_ = true;
        endReplay(game_.phase_ == GamePhase::GameOver);
        return !replay_.pending();
    }
    quit_ = true;
    return true;
}
void Frontend::close() {
    if (view_ == FrontendView::Playing)
        finishGameplay();
    flush();
}
void seedMenuParticles(EntityPool &pool, GameRandom &r, int count, MenuPopulation population) {
    // 0041717c (variant 0), 00416f90 (1), 004180ab/004175d5 (2).
    for (int i = 0; i < count; ++i) {
        auto &e = pool.at(size_t(i));
        e.x = fixed16(r.below(240));
        e.y = fixed16(r.below(320));
        e.animation = population == MenuPopulation::Directional ? 32 : -1;
        e.drawing = 0;
        e.vx = e.vy = 0;
        if (population == MenuPopulation::Directional) {
            int v = r.below(64) + 64;
            if (r.below(8) < 4)
                v = -v;
            e.vx = mul32(v, 1024);
            v = r.below(64) + 64;
            if (r.below(8) < 4)
                v = -v;
            e.vy = mul32(v, 1024);
        } else {
            e.vx = mul32(r.below(256) - 128, 1024);
            e.vy = mul32(r.below(256) - 128, 1024);
        }
        e.sprite = 5;
        if (population != MenuPopulation::Profile)
            e.animationBase = 5;
        e.behavior = 19;
    }
}
void Frontend::seedParticles(int count, MenuPopulation population) {
    seedMenuParticles(game_.pool_, game_.random_, count, population);
}
void Frontend::enter(FrontendView view, bool initialize) {
    if (editionMenu_) {
        game_.background_ = stageBackground(0);
        game_.nativeVisualBackground_ = game_.background_;
        game_.backgroundAdvance_ = 0;
    }
    view_ = view;
    selected_ = 0;
    pressed_ = -1;
    previousPointerHeld_ = false;
    switch (view) {
    case FrontendView::Splash:
        for (auto &star : stars_) {
            star[0] = mul32(game_.random_.below(4095) - 2047, 0x100000);
            star[1] = mul32(game_.random_.below(4095) - 2047, 0x100000);
            star[2] = fixed16(game_.random_.below(4095));
        }
        slogan_ = game_.random_.below(27);
        game_.sounds_.push_back(7);
        break;
    case FrontendView::Main:
    case FrontendView::About:
        seedParticles(32, MenuPopulation::Directional);
        break;
    case FrontendView::Players:
        if (initialize) {
            game_.lives_ = 0;
            seedParticles(20, MenuPopulation::Profile);
        }
        break;
    case FrontendView::Slot:
    case FrontendView::Clear:
        seedParticles(20, MenuPopulation::Drifting);
        break;
    case FrontendView::Name:
        playerSlot().name = "A";
        nameLetter_ = nameCursor_ = 0;
        seedParticles(10, MenuPopulation::Profile);
        dirty_ = true;
        break;
    case FrontendView::Level:
        level_ = 0;
        if (editionMenu_)
            game_.background_ = stageBackground(0);
        game_.stage_ = 0;
        game_.lives_ = 3;
        game_.combatState_.score = 0;
        game_.combatState_.difficulty = 0;
        game_.frame_ = 0;
        for (int i = 0; i < 20; ++i)
            game_.pool_.at(size_t(i)).behavior = 0;
        break;
    case FrontendView::Reader:
        pages_ = paginateInformation(original::information.at(size_t(document_)));
        page_ = 0;
        break;
    case FrontendView::Campaign: {
        assets_.loadCampaignImage();
        ++game_.assetRevision_;
        pages_.clear();
        size_t i = 0;
        while (i < original::ending.size()) {
            TileMap map(240, 320);
            layoutMenu(map, original::continueMenu, 0);
            int row = 5;
            while (i < original::ending.size() && row < 35) {
                int c = (i == 0 || i + 1 == original::ending.size()) ? 65 : int(i) + 66;
                map.centeredText(original::ending[i].c_str(), row, (uint32_t(c) << 16) | 0xff00u);
                row += i == 0 ? 4 : 2;
                ++i;
            }
            pages_.push_back(std::move(map));
        }
        page_ = 0;
        break;
    }
    case FrontendView::HighScores:
        game_.frame_ = 0;
        // 00417313's distinct five-draw, axis-aligned particle initializer.
        for (size_t i = 0; i < 40; ++i) {
            auto &e = game_.pool_.at(i);
            auto &r = game_.random_;
            e.x = fixed16(r.below(240));
            e.y = fixed16(r.below(320));
            e.animation = 5;
            e.animationBase = 120;
            e.drawing = 0;
            e.vx = mul32(r.below(256) + 128, 512);
            e.vy = 0;
            if (r.below(8) < 4)
                e.vx = sub32(0, e.vx);
            if (r.below(8) < 4) {
                e.vy = e.vx;
                e.vx = 0;
            }
            e.sprite = 5;
            e.behavior = 19;
        }
        break;
    case FrontendView::Farewell:
        seedParticles(10, MenuPopulation::Profile);
        for (size_t i = 0; i < 10; ++i)
            game_.pool_.at(i).sprite = 4;
        game_.frame_ = 0;
        break;
    default:
        break;
    }
    rebuild();
}
void Frontend::rebuild() {
    map_.clear();
    bounds_.clear();
    switch (view_) {
    case FrontendView::ReplayPrompt:
    case FrontendView::ReplayOverwrite: {
        MenuSpec spec{view_ == FrontendView::ReplayPrompt ? "SAVE REPLAY?" : "OVERWRITE REPLAY?",
                      10,
                      78,
                      6,
                      66,
                      2,
                      5,
                      {{"Yes", {}}, {"No", {}}}};
        bounds_ = layoutMenu(map_, spec);
        break;
    }
    case FrontendView::ReplaySlots:
    case FrontendView::ReplayBrowse: {
        MenuSpec spec{view_ == FrontendView::ReplayBrowse ? "WATCH: MAX STAGE / SCORE"
                                                          : "SAVE: MAX STAGE / SCORE",
                      7,
                      78,
                      3,
                      66,
                      0,
                      2,
                      {}};
        std::vector<std::string> labels(replaySlotLabels_.begin(), replaySlotLabels_.end());
        labels.push_back(view_ == FrontendView::ReplayBrowse ? "Exit" : "Cancel");
        spec.items.resize(labels.size());
        bounds_ = layoutMenu(map_, spec, 2, labels);
        break;
    }
    case FrontendView::Splash:
        map_.centeredText(original::titleTop, 1, 0x17f00);
        map_.centeredText(original::titleSub, 10, 0x17f00);
        map_.centeredText(aofActive_ ? aof::titleCredit
                                     : (editionMenu_ ? "Enhanced Edition" : original::titleCredit),
                          3, 0x1cf00);
        map_.centeredText(aofActive_ ? aof::slogans[size_t(slogan_)]
                                     : original::slogans[size_t(slogan_)],
                          29, 0x17f00);
        map_.centeredText(original::titleCopyright, 34, 0x27f00);
        map_.centeredText(original::titleWeb, 36, 0x27f00);
        bounds_ = layoutMenu(map_, original::continueMenu, 0);
        break;
    case FrontendView::Main:
        if (editionMenu_) {
            auto spec = original::mainMenu;
            spec.title = "";
            spec.row = 8;
            spec.gap = 4;
            spec.itemStep = 4;
            spec.items = {
                {"Play Gridrunner Now!", {}},
                {aofActive_ ? "Arse on Fire Special Edition" : "Gridrunner ++ (Standard)", {}},
                {"Restart Best", {}},
                {"Information", {}},
                {"Save Slots/Replays", {}},
                {"Exit", {}}};
            bounds_ = layoutMenu(map_, spec);
        } else
            bounds_ = layoutMenu(map_, original::mainMenu);
        if (editionMenu_) {
            map_.centeredText(aofActive_
                                  ? aof::titleCredit
                                  : (editionMenu_ ? "Enhanced Edition" : original::titleCredit),
                              6, 0x42ff00);
            map_.centeredText(aofActive_ ? aof::slogans[size_t(slogan_)]
                                         : original::slogans[size_t(slogan_)],
                              36, 0x41ff00);
        }
        break;
    case FrontendView::About:
        bounds_ = layoutMenu(map_, original::aboutMenu);
        break;
    case FrontendView::Players: {
        std::vector<std::string> names;
        for (auto &p : scores_.players)
            names.push_back(p.name);
        names.push_back("GUEST");
        names.push_back("EXIT");
        bounds_ = layoutMenu(map_, original::playersMenu, 8, names);
        break;
    }
    case FrontendView::Slot:
    case FrontendView::Clear:
        map_.centeredText(("PLAYER: " + playerName()).c_str(), 8, 0x42ff00);
        {
            auto spec = view_ == FrontendView::Slot ? original::slotMenu : original::clearMenu;
            if (view_ == FrontendView::Slot && replaysEnabled_) {
                // Adding an action must not move Exit into a different palette cycle.
                spec.items.back().color = spec.firstColor + 4 * spec.colorStep;
                spec.items.insert(spec.items.end() - 1,
                                  {"Watch Saved Replays", {}, spec.firstColor});
            }
            bounds_ = layoutMenu(map_, spec);
        }
        break;
    case FrontendView::Name:
        map_.centeredText(original::alphabet, 18, 0x41ff00);
        map_.centeredText(("PLAYER: " + playerName()).c_str(), 8, 0x42ff00);
        bounds_ = layoutMenu(map_, original::nameMenu);
        break;
    case FrontendView::Level: {
        char text[100];
        std::snprintf(text, sizeof text, "LEVEL %d", level_ + 1);
        map_.centeredText(text, 17, 0x1ff00);
        map_.centeredText(("PLAYER: " + playerName()).c_str(), 8, 0x42ff00);
        map_.centeredText(original::stages[size_t(level_)].caption, 14, 0x41ff00);
        map_.centeredText(original::underworldTitle, editionMenu_ ? 30 : 28, 0x41ff00);
        const auto &s = scores_.underworld[size_t(level_)];
        std::snprintf(text, sizeof text, original::recordFormat, s.name.c_str(),
                      signedBits(s.points));
        map_.centeredText(text, editionMenu_ ? 32 : 30, 0x42ff00);
        bounds_ = layoutMenu(map_, original::levelMenu);
        if (editionMenu_) {
            // Rebuild from clean text: layoutMenu above includes the old Start row.
            map_.clear();
            map_.centeredText(original::levelMenu.title, 10, 0x41ff00);
            std::snprintf(text, sizeof text, "LEVEL %d", level_ + 1);
            map_.centeredText(text, 17, 0x1ff00);
            map_.centeredText(("PLAYER: " + playerName()).c_str(), 8, 0x42ff00);
            map_.centeredText(original::stages[size_t(level_)].caption, 14, 0x41ff00);
            map_.centeredText(original::underworldTitle, 30, 0x41ff00);
            std::snprintf(text, sizeof text, original::recordFormat, s.name.c_str(),
                          signedBits(s.points));
            map_.centeredText(text, 32, 0x42ff00);
            bounds_.resize(2);
            auto action = [&](const char *label, int row) {
                auto r = map_.centeredText(label, row, 0x41ff00);
                bounds_.push_back({r.x - 8, r.y - 8, r.x + r.width, r.y + r.height + 8});
            };
            action("Start Game", 22);
            if (level_ == 0)
                action("Start Endurance Game", 26);
        }
        game_.stage_ = unsigned(level_);
        game_.announcement_ = map_;
        break;
    }
    case FrontendView::Reader:
        map_ = pages_.at(size_t(page_));
        bounds_ = layoutMenu(map_, original::readerMenu);
        break;
    case FrontendView::Campaign:
        map_ = pages_.at(size_t(page_));
        bounds_ = layoutMenu(map_, original::continueMenu, 0);
        break;
    case FrontendView::HighScores:
        if (aofActive_)
            map_.centeredText("ARSE ON FIRE", 4, 0x44ff00);
        map_.centeredText(original::highScoreTitle, 8, 0x42ff00);
        map_.centeredText(original::highScoreFooter, editionMenu_ ? 38 : 36, 0x42ff00);
        for (size_t i = 0; i < 10; ++i) {
            char text[80];
            std::snprintf(text, sizeof text, original::scoreFormat,
                          signedBits(scores_.normal[i].points), scores_.normal[i].name.c_str());
            int x = int(i >> 1) + 2;
            for (const char *p = text; *p; ++p)
                map_.put(x++, int(i) * 2 + (editionMenu_ ? 10 : 13),
                         uint32_t(uint8_t(*p)) | 0xff00u | uint32_t(i ? i * 4 : 65) << 16);
        }
        if (editionMenu_) {
            char record[80];
            map_.centeredText("ENDURANCE MASTER", 31, 0x44ff00);
            std::snprintf(record, sizeof record, "LEVEL %u SCORE %u", scores_.enduranceLevel + 1,
                          scores_.enduranceMaster.points);
            map_.centeredText(record, 33, 0x41ff00);
            map_.centeredText(scores_.enduranceMaster.name.c_str(), 35, 0x42ff00);
        }
        bounds_ = {{0, 0, 240, 320}};
        break;
    case FrontendView::Farewell:
        for (auto &l : original::farewell)
            map_.centeredText(l.text, l.row, uint32_t(l.color) << 16 | 0xff00u);
        bounds_ = layoutMenu(map_, original::exitMenu, 0);
        break;
    default:
        break;
    }
    selected_ = std::clamp(selected_, 0, std::max(0, int(bounds_.size()) - 1));
}
void Frontend::levelChange(int direction) {

    const bool enduranceChoice = editionMenu_ && level_ == 0 && selected_ == 3;
    const int last = std::min(49, int(playerSlot().bestLevel));
    level_ = (level_ + direction + last + 1) % (last + 1);
    if (editionMenu_) {
        game_.background_ = stageBackground(unsigned(level_));
        game_.nativeVisualBackground_ = game_.background_;
        game_.backgroundAdvance_ = 0;
    }
    rebuild();
    if (enduranceChoice && level_ != 0)
        selected_ = 2;
}
void Frontend::editName(int action) {
    auto &name = playerSlot().name;
    if (action == 0)
        nameLetter_ = (nameLetter_ + 26) % 27;
    if (action == 1)
        nameLetter_ = (nameLetter_ + 1) % 27;
    if (action == 2 && nameCursor_ < 15)
        ++nameCursor_;
    if (action == 3 && nameCursor_ > 0) {
        --nameCursor_;
        nameLetter_ = int(name[size_t(nameCursor_)]) - 'A';
    }
    name.resize(size_t(nameCursor_ + 1), 'A');
    name[size_t(nameCursor_)] = char('A' + nameLetter_);
    dirty_ = true;
    rebuild();
}
void Frontend::switchEdition() {
    flush();
    if (dirty_)
        return;
    const auto activePlayer = scores_.selectedPlayer;
    const auto activeName = playerName();
    const size_t old = aofActive_ ? 1 : 0;
    crafts_[old] = game_.player_.alternateCraft;
    if (aofActive_)
        aofScores_ = scores_;
    else
        originalScores_ = scores_;
    aofActive_ = !aofActive_;
    scores_ = aofActive_ ? aofScores_ : originalScores_;
    scores_.selectedPlayer = activePlayer;
    playerSlot().name = activeName;
    dirty_ = true;
    game_.arse(aofActive_);
    game_.endurance(false);
    game_.player_.selectedPlayer = int(scores_.selectedPlayer);
    game_.player_.alternateCraft = crafts_[aofActive_ ? 1 : 0];
    game_.player_.alternateCraftUnlocked = scores_.alternateCraftUnlocked != 0;
    assets_.load(assets_.root());
    ++game_.assetRevision_;
    audioReset_ = true;
    game_.sounds_.push_back(7);
    slogan_ = game_.random_.below(27);
    pressed_ = -1;
    previousPointerHeld_ = false;
    flush();
    rebuild();
}
void Frontend::startGameplay(bool resume, bool endurance) {
    replayStartPending_ = replaysEnabled_;
    auto &g = game_;
    auto &p = playerSlot();
    g.arse(aofActive_);
    g.endurance(endurance);
    g.arseStarted_ = false;
    g.stage_ = endurance ? 0 : resume ? p.level : unsigned(level_);
    if (editionMenu_)
        g.backgroundAdvance_ = 0;
    g.lives_ = endurance ? 0 : resume ? int(p.lives) : 3;
    g.combatState_.score = resume ? signedBits(p.score) : 0;
    g.combatState_.difficulty = resume ? int(p.difficulty) : 0;
    const bool campaignResume = g.stage_ == 50;
    if (campaignResume) {
        g.stage_ = 0;
        g.combatState_.difficulty += 2;
        g.player_.alternateCraftUnlocked = true;
        scores_.alternateCraftUnlocked = 1;
        dirty_ = true;
    }
    g.player_.selectedPlayer = int(scores_.selectedPlayer);
    g.player_.bombActive = 0;
    g.combatState_.travellingAtDeath = 0;
    g.phase_ = GamePhase::Playing;
    g.stagesCleared_ = g.deaths_ = 0;
    g.beginPending_ = false;
    g.formations_.resetEmissionCount();
    g.playerName_ = playerName();
    if (campaignResume) {
        g.beginPending_ = true;
        g.campaignNotice_ = true;
        enter(FrontendView::Campaign);
        flush();
        return;
    }
    g.beginAttempt();
    view_ = FrontendView::Playing;
    bounds_.clear();
    pressed_ = -1;
    previousPointerHeld_ = false;
}
std::filesystem::path Frontend::replayPath(unsigned slot) const {
    if (slot >= 10)
        throw std::out_of_range("Replay slot");
    return saveFile_.parent_path() / "replays" /
           ("profile-" +
            std::to_string(replay_.pending()
                               ? replay_.info().profile
                               : unsigned(std::min<size_t>(scores_.selectedPlayer, 4)))) /
           ("slot-" + std::to_string(slot + 1) + ".grr");
}
void Frontend::loadReplaySlots() {
    for (unsigned n = 0; n < 10; ++n) {
        replaySlotLabels_[n] = std::to_string(n + 1) + ": Empty";
        std::error_code error;
        if (std::filesystem::exists(replayPath(n), error)) {
            try {
                auto i = ReplayRecording::inspect(replayPath(n));
                replaySlotLabels_[n] = std::to_string(n + 1) + ": " + (i.aof ? "AoF " : "Std ") +
                                       std::to_string(i.maxStage) + " / " + std::to_string(i.score);
            } catch (...) {
                replaySlotLabels_[n] = std::to_string(n + 1) + ": Unreadable";
            }
        }
    }
}
void Frontend::watchReplaySlot(unsigned slot) {
    try {
        auto candidate = std::make_unique<Game>(assets_);
        playback_.load(*candidate, replayPath(slot));
        candidate->renderNative();
        replayGame_ = std::move(candidate);
        playbackClock_ = 0;
        ++playbackAssetRevision_;
        audioReset_ = true;
        saveError_.clear();
        view_ = FrontendView::ReplayWatching;
        bounds_.clear();
        previousPointerHeld_ = false;
    } catch (const std::exception &) {
        assets_.arse(aofActive_);
        assets_.load(assets_.root());
        ++playbackAssetRevision_;
        saveError_ = "Replay unavailable or damaged";
    }
}
void Frontend::stopWatchingReplay() {
    replayGame_.reset();
    playback_ = {};
    assets_.arse(aofActive_);
    assets_.load(assets_.root());
    ++playbackAssetRevision_;
    audioReset_ = true;
    loadReplaySlots();
    enter(FrontendView::ReplayBrowse);
}
void Frontend::endReplay(bool completed) {
    replayStartPending_ = false;
    replay_.finish(game_.combatState_.score, completed);
    finishGameplay();
    if (replay_.pending())
        enter(FrontendView::ReplayPrompt);
}
void Frontend::dismissReplay() {
    replay_.discard();
    if (quitAfterReplay_) {
        quit_ = true;
        return;
    }
    enter(FrontendView::HighScores);
}
void Frontend::saveReplaySlot(unsigned slot) {
    try {
        replay_.save(replayPath(slot));
        saveError_.clear();
        dismissReplay();
    } catch (const std::exception &) {
        saveError_ = "Replay save failed";
    }
}
void Frontend::finishGameplay() {
    auto &p = playerSlot();
    p.bestLevel = std::max(p.bestLevel, game_.stage_);
    if (game_.endurance()) {
        scores_.recordEndurance(game_.stage_, uint32_t(game_.combatState_.score), playerName());
        rank_ = 11;
    } else
        rank_ = scores_.insertNormal({playerName(), uint32_t(game_.combatState_.score)});
    if (rank_ == 10)
        game_.combatState_.score = 0;
    dirty_ = true;
    enter(FrontendView::HighScores);
    flush();
}
void Frontend::activate(int item) {
    if (view_ == FrontendView::ReplayBrowse) {
        if (item == 10) {
            saveError_.clear();
            enter(FrontendView::Slot);
        } else
            watchReplaySlot(unsigned(item));
        return;
    }
    if (view_ == FrontendView::ReplayPrompt) {
        if (item == 0) {
            loadReplaySlots();
            enter(FrontendView::ReplaySlots);
        } else
            dismissReplay();
        return;
    }
    if (view_ == FrontendView::ReplaySlots) {
        if (item == 10) {
            enter(FrontendView::ReplayPrompt);
            return;
        }
        replaySlot_ = unsigned(item);
        std::error_code error;
        if (std::filesystem::exists(replayPath(replaySlot_), error))
            enter(FrontendView::ReplayOverwrite);
        else
            saveReplaySlot(replaySlot_);
        return;
    }
    if (view_ == FrontendView::ReplayOverwrite) {
        if (item == 0)
            saveReplaySlot(replaySlot_);
        else
            enter(FrontendView::ReplaySlots);
        return;
    }
    switch (view_) {
    case FrontendView::Splash:
        audioReset_ = true;
        enter(FrontendView::Main);
        break;
    case FrontendView::Main:
        if (editionMenu_) {
            switch (item) {
            case 0:
                enter(FrontendView::Level);
                break;
            case 1:
                switchEdition();
                break;
            case 2:
                startGameplay(true);
                break;
            case 3:
                enter(FrontendView::About);
                break;
            case 4:
                enter(FrontendView::Players);
                break;
            default:
                enter(FrontendView::Farewell);
                break;
            }
            break;
        }
        if (item == 0)
            enter(FrontendView::About);
        else if (item == 1)
            enter(FrontendView::Players);
        else if (item == 2)
            enter(FrontendView::Level);
        else if (item == 3)
            startGameplay(true);
        else
            enter(FrontendView::Farewell);
        break;
    case FrontendView::About:
        if (item < 4) {
            document_ = item;
            enter(FrontendView::Reader);
        } else
            enter(FrontendView::Main);
        break;
    case FrontendView::Reader:
        if (item == 0) {
            page_ = std::max(0, page_ - 1);
            rebuild();
        } else if (item == 1 && page_ + 1 < int(pages_.size())) {
            ++page_;
            rebuild();
        } else
            enter(FrontendView::About);
        break;
    case FrontendView::Players:
        if (item < 4) {
            scores_.selectedPlayer = unsigned(item);
            game_.player_.selectedPlayer = item;
            dirty_ = true;
            enter(FrontendView::Slot);
        } else if (item == 4) {
            scores_.selectedPlayer = 4;
            game_.player_.selectedPlayer = 4;
            dirty_ = true;
            enter(replaysEnabled_ ? FrontendView::Slot : FrontendView::Level);
        } else
            enter(FrontendView::Main);
        break;
    case FrontendView::Slot:
        if (item == 0)
            enter(FrontendView::Name);
        else if (item == 1)
            enter(FrontendView::Clear);
        else if (item == 2)
            startGameplay(true);
        else if (item == 3)
            enter(FrontendView::Level);
        else if (item == 4 && replaysEnabled_) {
            saveError_.clear();
            loadReplaySlots();
            enter(FrontendView::ReplayBrowse);
        } else
            enter(FrontendView::Main);
        break;
    case FrontendView::Clear:
        if (item == 0) {
            auto &p = playerSlot();
            p = original::defaultScores().players.at(scores_.selectedPlayer);
            p.lives = 0;
            dirty_ = true;
        }
        enter(FrontendView::Players, false);
        flush();
        break;
    case FrontendView::Name:
        if (item < 4)
            editName(item);
        else {
            enter(FrontendView::Players, false);
            flush();
        }
        break;
    case FrontendView::Level:
        if (item < 2)
            levelChange(item ? 1 : -1);
        else if (editionMenu_ && level_ == 0 && item == 3)
            startGameplay(false, true);
        else
            startGameplay(false);
        break;
    case FrontendView::HighScores:
        enter(FrontendView::Splash);
        break;
    case FrontendView::Farewell:
        flush();
        quit_ = true;
        break;
    case FrontendView::Campaign:
        if (++page_ == int(pages_.size())) {
            game_.campaignNotice_ = false;
            view_ = FrontendView::Playing;
            try {
                replay_.checkpoint(game_);
            } catch (const std::exception &) {
                replay_.discard();
                saveError_ = "Replay unavailable";
            }
        } else
            rebuild();
        break;
    default:
        break;
    }
}
void Frontend::drawMenu(const FrontendInput &input) {
    game_.canvas_.dimensions(240, 320);
    auto &g = game_;
    g.canvas_.clear(advanceFrameClear(g.combatState_.frameFlashTicks, g.frameClearColor_));
    advanceTextPalette(g.textPalette_, g.colorPhase_);
    SourceVisuals visual(g.canvas_, assets_, g.player_, g.random_, g.effects_);
    visual.frame(g.frame_, g.colorPhase_);
    if (view_ == FrontendView::Main || view_ == FrontendView::About ||
        view_ == FrontendView::Level) {
        g.player_.worldX = editionMenu_ ? 0 : g.pool_.at(89).x;
        g.player_.worldY = editionMenu_ ? 0 : g.pool_.at(89).y;
        visual.background(g.backgroundAdvance_, g.background_);
    }
    if (view_ == FrontendView::Level)
        advanceTextPalette(g.textPalette_, g.colorPhase_); // selector's second 004167ce call
    if (view_ == FrontendView::Splash) {
        int gradient = 0x7f00;
        for (int y = 0; y < 160; ++y) {
            const auto c = packOriginalColor(uint32_t((gradient >> 8) << 8));
            g.canvas_.line(0, y, 240, 1, c);
            g.canvas_.line(0, 319 - y, 240, 1, c);
            gradient -= 203;
        }
    }
    drawMenuStars(g.canvas_, stars_, g.frame_);
    if (view_ == FrontendView::Campaign)
        visual.sprite(368, 120, 160);
    else if (view_ == FrontendView::Splash)
        g.canvas_.sprite(assets_, 0, 120, 160, {false, 0, 0});
    else if (view_ != FrontendView::Reader && view_ != FrontendView::Level &&
             view_ != FrontendView::Name && view_ != FrontendView::Campaign)
        g.canvas_.sprite(assets_, 1, 120, (assets_.sprite(1).height >> 1) + 10);
    if (view_ == FrontendView::Level) {
        const ChainFrame frame{pixel(g.player_.worldX) + 16,
                               pixel(g.player_.worldX) + 224,
                               pixel(g.player_.worldY) + 16,
                               pixel(g.player_.worldY) + 304,
                               .1f,
                               g.frame_};
        g.hud(visual, frame);
    }
    map_.draw(g.canvas_, g.textPalette_);
    if (view_ == FrontendView::Level || view_ == FrontendView::Name) {
        int y = view_ == FrontendView::Level ? 152 : 200;
        visual.sprite(2, 32, y);
        visual.sprite(3, 208, y);
    }
    if (view_ == FrontendView::Name) {
        const int bounce = std::abs(int(uint32_t(mul32(g.frame_, 64)) & 511u) - 255) - 128;
        visual.sprite(4, nameLetter_ * 8 + 20, asr32(bounce, 5) + 160);
        g.canvas_.image(0, {nameLetter_ * 8 + 8, 16, 8, 8}, 104, 100, 32, 32,
                        {true, packOriginalColor(visual.color(0)), 0});
    }
    int hover =
        input.pointerActive ? menuHit(bounds_, input.pointer.x, input.pointer.y) : selected_;
    if (hover >= 0 && view_ != FrontendView::Splash && view_ != FrontendView::HighScores) {
        const auto &r = bounds_[size_t(hover)];
        auto color = packOriginalColor(g.textPalette_[64]);
        g.canvas_.selection(r, color);
        if (editionMenu_ && view_ == FrontendView::Level && !input.pointerActive && selected_ < 2)
            g.canvas_.selection(bounds_[size_t(hover == 0 ? 1 : 0)], color);
    }
    if (!saveError_.empty())
        g.canvas_.text(saveError_.c_str(), 4, 2, 0xf800);
    ++g.frame_;
    g.colorPhase_ += .1f;
}
void Frontend::step(const FrontendInput &input) {
    game_.sounds_.clear();
    audioReset_ = false;
    if (watchingReplay()) {
        const bool held = (input.pointer.held & 1u) != 0;
        previousPointerHeld_ = held;
        if (input.back) {
            stopWatchingReplay();
            drawMenu({});
            return;
        }
        try {
            auto &g = *replayGame_;
            g.sounds_.clear();
            // Host ticks at 200 Hz; 100 Hz recordings consume alternate ticks.
            playbackClock_ += unsigned(g.nativeSubsteps());
            if (playbackClock_ >= 6) {
                playbackClock_ -= 6;
                const auto revision = g.assetRevision();
                const bool more = playback_.step(g);
                if (revision != g.assetRevision())
                    ++playbackAssetRevision_;
                if (!more) {
                    stopWatchingReplay();
                    drawMenu({});
                }
            }
        } catch (const std::exception &) {
            stopWatchingReplay();
            saveError_ = "Replay unavailable or damaged";
            drawMenu({});
        }
        return;
    }
    if (view_ == FrontendView::Playing) {
        const bool held = (input.pointer.held & 1u) != 0;
        const bool released = input.pointerReleased || (previousPointerHeld_ && !held);
        previousPointerHeld_ = held;
        if (input.back || (game_.phase_ == GamePhase::GameOver && (input.confirm || released))) {
            if (replaysEnabled_)
                endReplay(game_.phase_ == GamePhase::GameOver);
            else
                finishGameplay();
            return;
        }
        const auto previous = game_.stagesCleared_;
        const bool sourceTick = !game_.nativeRate() || game_.nativePhase() == 0;
        if (replayStartPending_) {
            replayStartPending_ = false;
            try {
                replay_.begin(game_, unsigned(std::min<size_t>(scores_.selectedPlayer, 4)));
            } catch (const std::exception &) {
                replay_.discard();
                saveError_ = "Replay unavailable";
            }
        }
        try {
            replay_.append(input.pointer);
        } catch (const std::exception &) {
            replay_.discard();
            saveError_ = "Replay recording full";
        }
        game_.step(input.pointer);
        replay_.reachedStage(game_.stage());
        if (replaysEnabled_ && game_.phase_ == GamePhase::GameOver) {
            endReplay(true);
            return;
        }
        if (previous != game_.stagesCleared_) {
            dirty_ = true;
            flush();
        }
        if (sourceTick && game_.beginPending_ && game_.phase_ == GamePhase::Playing) {
            dirty_ = true;
            flush();
        }
        if (game_.campaignNotice_)
            enter(FrontendView::Campaign);
        return;
    }
    if (input.back) {
        if (view_ == FrontendView::Campaign && replay_.active()) {
            endReplay(false);
            return;
        }
        if (view_ == FrontendView::ReplayBrowse) {
            saveError_.clear();
            enter(FrontendView::Slot);
        } else if (view_ == FrontendView::ReplayPrompt)
            dismissReplay();
        else if (view_ == FrontendView::ReplaySlots)
            enter(FrontendView::ReplayPrompt);
        else if (view_ == FrontendView::ReplayOverwrite)
            enter(FrontendView::ReplaySlots);
        else if (view_ == FrontendView::Splash || view_ == FrontendView::Main)
            enter(FrontendView::Farewell);
        else if (view_ == FrontendView::Reader)
            enter(FrontendView::About);
        else if (view_ == FrontendView::Slot || view_ == FrontendView::Name ||
                 view_ == FrontendView::Clear)
            enter(FrontendView::Players, false);
        else if (view_ == FrontendView::Farewell) {
            flush();
            quit_ = true;
        } else if (view_ == FrontendView::HighScores)
            enter(FrontendView::Splash);
        else
            enter(FrontendView::Main);
    } else {
        if (editionMenu_ && view_ == FrontendView::Level) {
            if (selected_ == 1)
                selected_ = 0;
            if (input.vertical) {
                const int count = int(bounds_.size()) - 1, row = selected_ == 0 ? 0 : selected_ - 1;
                const int next = (row + (input.vertical > 0 ? 1 : count - 1)) % count;
                selected_ = next == 0 ? 0 : next + 1;
            }
        } else if (input.vertical && !bounds_.empty())
            selected_ = (selected_ + input.vertical + int(bounds_.size())) % int(bounds_.size());
        if (input.horizontal) {
            if (view_ == FrontendView::Main && editionMenu_ && selected_ == 1)
                switchEdition();
            else if (view_ == FrontendView::Level && (!editionMenu_ || selected_ < 2))
                levelChange(input.horizontal);
            else if (view_ == FrontendView::Name)
                editName(input.horizontal < 0 ? 0 : 1);
        }
        if (view_ == FrontendView::Name && input.letter >= 'A' && input.letter <= '[') {
            nameLetter_ = input.letter - 'A';
            editName(4);
        }
        if (view_ == FrontendView::Name && input.erase)
            editName(3);
        const bool held = (input.pointer.held & 1u) != 0;
        if (held || input.pointerReleased)
            pressed_ = menuHit(bounds_, input.pointer.x, input.pointer.y);
        const bool release = input.pointerReleased || (previousPointerHeld_ && !held);
        previousPointerHeld_ = held;
        if (input.confirm && !bounds_.empty()) {
            if (editionMenu_ && view_ == FrontendView::Level && selected_ < 2)
                selected_ = 2;
            else
                activate(selected_);
        } else if (release && (view_ == FrontendView::Splash || view_ == FrontendView::HighScores))
            activate(0); // These source screens use the position-independent click latch.
        else if (release && pressed_ >= 0)
            activate(pressed_);
    }
    if (view_ != FrontendView::Playing && !watchingReplay())
        drawMenu(input);
}
} // namespace grpp
