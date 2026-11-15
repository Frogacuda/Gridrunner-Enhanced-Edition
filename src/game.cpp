#include "grpp/game.hpp"
#include "aof_levels.hpp"
#include "grpp/background.hpp"
#include "original_levels.hpp"
#include "original_tables.hpp"
#include "original_visuals.hpp"
#include <algorithm>
#include <cstdio>

namespace grpp {
Game::Game(Assets &assets)
    : assets_(assets), effects_(original::effectRandomSeed),
      background_(original::initialBackground) {}
void Game::stageBackgrounds(bool enabled) {
    if (enabled == stageBackgrounds_)
        return;
    stageBackgrounds_ = enabled;
    if (enabled) {
        background_ = stageBackground(stage_);
        nativeVisualBackground_ = background_;
    }
}
void Game::start(unsigned level) {
    if (level >= original::stages.size())
        throw std::out_of_range("Original stage index");
    pool_ = {};
    player_ = {};
    combatState_ = {};
    combatState_.shotColorPhase = .1f;
    grid_ = {};
    worldState_ = {};
    census_ = {};
    player_.arse = arse_;
    player_.endurance = endurance_;
    player_.shotInterval = arse_ ? 1 : 2;
    player_.alternateCraft = arse_ ? 1 : 0;
    arseStarted_ = false;
    canvas_.dimensions(player_.width(), player_.height());
    nativePhase_ = 0;
    pendingPressed_ = pendingHeld_ = 0;
    pool_.nativeMotion = native_.get();
    if (native_)
        native_->reset();
    stage_ = level;
    lives_ = endurance_ ? 0 : 3;
    frame_ = 0;
    colorPhase_ = 0;
    frameClearColor_ = 0;
    stagesCleared_ = deaths_ = 0;
    backgroundAdvance_ = 0;
    phase_ = GamePhase::Playing;
    beginPending_ = false;
    nativeVisualBackground_ = background_;
    formations_.resetEmissionCount();
    sounds_.clear();
    beginAttempt();
}
void Game::warpIn() {
    auto &p = pool_.at(89);
    sounds_.push_back(13);
    p.transform.noiseSeed = uint32_t(asr32(mul32(random_.below(512), 0x31415927), 9));
    p.transform.tileWidth = p.transform.tileHeight = 2;
    p.transform.jitterX = 240;
    p.transform.jitterY = 64;
    p.transform.axisXX = p.transform.axisYY = 32;
    p.transform.axisXY = p.transform.axisYX = 0;
    p.transform.pivotX = p.transform.pivotY = 0;
    p.flags = 0;
    p.transform.flags = 2;
    player_.transitionTicks = -50;
    p.drawing = 9;
}
void Game::beginAttempt() {
    if (stageBackgrounds_)
        background_ = stageBackground(stage_);
    if (native_) {
        native_->reset();
        nativePhase_ = 0;
        pendingPressed_ = pendingHeld_ = 0;
    }
    const bool preserve = endurance_ && arseStarted_;
    canvas_.dimensions(player_.width(), player_.height());
    canvas_.clear();
    announcement_ = TileMap(player_.width(), player_.height());
    assets_.arse(arse_);
    assets_.load(assets_.root());
    const auto &stage = (arse_ ? aof::stages : original::stages).at(stage_);
    for (size_t family = 0; family < stage.orientationSources.size(); ++family)
        assets_.makeOrientations(size_t(stage.orientationSources[family]), family);
    ++assetRevision_;
    const int rewind = sub32(formations_.emitted(), add32(combatState_.travellingAtDeath, 2));
    formations_.prepareLevel(stage, stage_, combatState_.difficulty, rewind);
    combatState_.stageScore = 0;
    combatState_.travellingAtDeath = 0;
    if (!preserve)
        player_.sheepPower = 1;
    player_.transitionTicks = 0;
    player_.reverseTransition = false;
    player_.beamMode = 0;
    player_.bombTicks = 0;
    player_.scoreClock = 0;
    if (!preserve) {
        for (size_t slot = 0; slot < 116; ++slot)
            pool_.at(slot).behavior = 0;
        grid_ = {};
    }
    introTicks_ = 60;
    announcement_.clear();
    char title[48];
    std::snprintf(title, sizeof title, "LEVEL %u", stage_ + 1);
    announcement_.centeredText(title, 18, 0x44ff00);
    announcement_.centeredText(stage.caption, 22, 0x41ff00);
    auto &p = pool_.at(89);
    if (!preserve) {
        p.x = fixed16(player_.width() / 2);
        p.y = fixed16(arse_ ? 400 : 240);
        p.targetX = p.x;
        p.targetY = p.y;
        p.vx = p.vy = 0;
        p.drawing = 0;
        p.animation = -1;
        p.sprite = player_.selectedPlayer + 358 + player_.alternateCraft * 5;
        p.flags = 1;
        p.behavior = 1;
        for (auto &point : p.history) {
            point.x = p.x;
            point.y = p.y;
        }
        warpIn();
        pool_.at(88).behavior = 0;
    }
    player_.reverseTransition = false;
    auto &anchor = pool_.at(0);
    if (!preserve) {
        anchor.x = fixed16(player_.width());
        anchor.y = fixed16(player_.height());
        anchor.depth = 8192;
    }
    const auto &t = formations_.tuning();
    anchor.vx = mul32(t.anchorSpeedX, arse_ ? 2 : 1);
    anchor.vy = mul32(t.anchorSpeedY, arse_ ? 2 : 1);
    anchor.speed = mul32(t.anchorDepthStep, arse_ ? 2 : 1);
    if (!preserve) {
        anchor.animation = -1;
        anchor.colorCycle = 7;
        anchor.drawing = 8;
        anchor.effectMode = 2;
        for (auto &point : anchor.history) {
            point.x = anchor.x;
            point.y = anchor.y;
            point.scale = anchor.depth;
        }
        anchor.historyCursor = 0;
        anchor.glyphWidth() = anchor.glyphHeight() = 0x80000;
        anchor.flags = 0;
        anchor.behavior = 8;
    }
    formations_.seedInitialSpawners();
    nativeVisualBackground_ = background_;
    arseStarted_ = true;
}
void Game::gameOver() {
    sounds_.push_back(arse_ && endurance_ ? 35 : 16);
    combatState_.frameFlashTicks = 12;
    player_.transitionTicks = 0;
    pool_.at(0).behavior = 0;
    pool_.at(88).behavior = 0;
    auto &p = pool_.at(89);
    p.behavior = 14;
    p.targetX = fixed16(player_.width() / 2);
    p.targetY = fixed16(player_.height() / 2);
    p.depth = 4096;
    p.glyphWidth() = p.glyphHeight() = 0x60000;
    p.drawing = 14;
    p.effectMode = 5;
    p.colorCycle = 7;
    p.oscillatorX() = 0;
    p.oscillatorXStep() = 5;
    p.oscillatorY() = 64;
    p.oscillatorYStep = 7;
    const auto &anchor = pool_.at(0);
    for (auto &point : p.history) {
        point.x = anchor.x;
        point.y = anchor.y;
        point.scale = anchor.depth;
    }
    pool_.at(0).historyCursor = 0;
    phase_ = GamePhase::GameOver;
}
void Game::hud(SourceVisuals &v, const ChainFrame &f) {
    const auto scoreFirst = canvas_.draws().size();
    char score[40];
    std::snprintf(score, sizeof score, "%d", combatState_.score);
    int x = 10;
    float offset = 0;
    for (const char *c = score; *c; ++c) {
        v.sprite(int(*c) + 46, x, canvas_.height() - 15, packOriginalColor(v.color(7, offset)));
        offset += .1f;
        x += 2 + assets_.sprite(size_t(int(*c) + 46)).width;
    }
    canvas_.anchorSince(scoreFirst, HudAnchor::BottomLeft);
    const auto multiplierFirst = canvas_.draws().size();
    const int overlap = x < 107 ? 0 : x - 106;
    v.sprite(106, overlap + canvas_.width() / 2 - 12, canvas_.height() - 15,
             packOriginalColor(v.color(3, offset)));
    offset += .2f;
    v.sprite(player_.sheepPower + 94, overlap + canvas_.width() / 2 + 3, canvas_.height() - 15,
             packOriginalColor(v.color(3, offset)));
    canvas_.anchorSince(multiplierFirst, HudAnchor::BottomCenter);
    const auto livesFirst = canvas_.draws().size();
    if (lives_ < 11)
        for (int life = 0; life < lives_; ++life)
            v.sprite(player_.selectedPlayer + 358 + player_.alternateCraft * 5,
                     canvas_.width() - 10 - std::min(lives_, 10) * 4 + life * 4,
                     canvas_.height() - 15);
    else {
        v.sprite(115, canvas_.width() - 18, canvas_.height() - 15);
        v.sprite(114, canvas_.width() - 18, canvas_.height() - 7, packOriginalColor(v.color(5)));
    }
    canvas_.anchorSince(livesFirst, HudAnchor::BottomRight);
    if (player_.bombActive == 1) {
        --player_.bombTicks;
        if (player_.beamMode == 0) {
            int slot = player_.beamTarget;
            for (; slot < 89; ++slot) {
                const auto &e = pool_.at(size_t(slot));
                if ((e.behavior != 3 && e.behavior != 4 && e.behavior != 16 && e.behavior != 17) ||
                    !(e.flags & 1))
                    continue;
                int px = pixel(e.x), py = pixel(e.y);
                const auto &sprite = assets_.sprite(size_t(e.sprite));
                bool visible = (px < f.minimumX ? f.minimumX - px <= sprite.width
                                                : px - f.maximumX <= sprite.width) &&
                               (py < f.minimumY ? f.minimumY - py <= sprite.height
                                                : py - f.maximumY <= sprite.height);
                if ((e.behavior == 3 || e.behavior == 4 || e.behavior == 16 || e.behavior == 17) &&
                    (e.flags & 1) && visible)
                    break;
            }
            if (slot == 89) {
                if (player_.bombTicks < 0)
                    player_.bombActive = -1;
                player_.beamTarget = 0;
                player_.beamMode = 0;
            } else {
                player_.beamTarget = slot;
                const auto &target = pool_.at(size_t(slot));
                beamX_ = sub32(target.x, player_.worldX);
                beamY_ = sub32(target.y, player_.worldY);
                const auto &p = pool_.at(89);
                v.lightning(p.x, sub32(p.y, 0x80000), beamX_, beamY_, 20,
                            packOriginalColor(v.color(7)));
                player_.beamMode = 4;
            }
        } else {
            v.sprite(player_.beamMode + 197, pixel(beamX_), pixel(beamY_),
                     packOriginalColor(v.color(7)));
            const auto &p = pool_.at(89);
            v.lightning(p.x, sub32(p.y, 0x80000), beamX_, beamY_, 20,
                        packOriginalColor(v.color(5)));
            --player_.beamMode;
        }
    }
    if (player_.reverseTransition && player_.transitionTicks < 0)
        v.sprite(248, canvas_.width() / 2, canvas_.height() + 20 + player_.transitionTicks * 4);
    if (introTicks_ > 0) {
        announcement_.draw(canvas_, textPalette_);
        --introTicks_;
    }
}
void Game::stageFinished() {
    if (endurance_) {
        ++stagesCleared_;
        sounds_.push_back(18);
        sounds_.push_back(30);
        combatState_.frameFlashTicks = 12;
        player_.bombActive = 0;
        player_.scoreStep = 16;
        if (++stage_ == 50) {
            stage_ = 0;
            combatState_.difficulty += 2;
            player_.alternateCraftUnlocked = true;
            campaignNotice_ = true;
            if (records_)
                records_->alternateCraftUnlocked = 1;
        }
        beginPending_ = true;
        return;
    }
    ++stagesCleared_;
    ++lives_;
    sounds_.push_back(18);
    sounds_.push_back(30);
    combatState_.frameFlashTicks = 12;
    phase_ = GamePhase::StageClear;
    clearTicks_ = 100;
    introTicks_ = 80;
    player_.bombActive = 0;
    player_.scoreStep = 16;
    announcement_.clear();
    announcement_.centeredText("SHEEPIEZAPPER RECHARGE!", 19, 0x41ff00);
    if (records_ && records_->recordStage(stage_, combatState_.stageScore, playerName_))
        announcement_.centeredText("NEW LEVEL HI SCORE!", 23, 0x44ff00);
}
void Game::cacheHud(size_t first) {
    // Only the suffix is ever replayed. Copying the background and all actors
    // into this cache turned a small HUD update into a multi-megabyte copy.
    nativeHud_.dimensions(canvas_.width(), canvas_.height());
    nativeHud_.clear(canvas_.clearColor());
    nativeHud_.append(canvas_, first);
    nativeHudFirst_ = 0;
}
void Game::sourceStep(const LogicalInput &input) {
    sounds_.clear();
    if (beginPending_) {
        beginPending_ = false;
        beginAttempt();
    }
    canvas_.clear(advanceFrameClear(combatState_.frameFlashTicks, frameClearColor_));
    player_.beamHitConsumed = false;
    // 0041685e establishes world/camera state BEFORE entity updates.
    player_.worldX = pool_.at(89).x;
    player_.worldY = pool_.at(89).y;
    SourceVisuals visual(canvas_, assets_, player_, random_, effects_);
    visual.frame(frame_, colorPhase_);
    nativeVisualFrame_ = frame_;
    nativeVisualPhase_ = colorPhase_;
    nativeVisualBackground_ = background_;
    // 004169da's shake countdown (479718) is separate from the flash timer
    // (4796d8). No recovered game path activates it; do not consume rand here.
    advanceTextPalette(textPalette_, colorPhase_);
    visual.background(backgroundAdvance_, background_, 0,
                      phase_ == GamePhase::StageClear ? player_.scoreStep * 2. : 0.);
    const ChainFrame frame{pixel(player_.worldX) + 16,
                           pixel(player_.worldX) + player_.width() - 16,
                           pixel(player_.worldY) + 16,
                           pixel(player_.worldY) + player_.height() - 16,
                           .1f,
                           frame_};
    if (phase_ == GamePhase::StageClear) {
        const auto hudFirst = canvas_.draws().size();
        hud(visual, frame);
        cacheHud(hudFirst);
        ++player_.scoreStep;
        backgroundAdvance_ = add32(backgroundAdvance_, player_.scoreStep * 2);
        if (--clearTicks_ == 0) {
            for (int i = 0; i < 4; ++i)
                background_[size_t(i)] = random_.below(4) + 3;
            for (int i = 4; i < 6; ++i)
                background_[size_t(i)] = random_.below(3) + 1;
            ++stage_;
            if (records_)
                records_->recordCheckpoint(unsigned(player_.selectedPlayer), lives_, stage_,
                                           combatState_.score, combatState_.difficulty);
            if (stage_ == 50) {
                stage_ = 0;
                combatState_.difficulty += 2;
                player_.alternateCraftUnlocked = true;
                campaignNotice_ = true;
            }
            if (records_)
                records_->alternateCraftUnlocked = player_.alternateCraftUnlocked;
            phase_ = GamePhase::Playing;
            beginPending_ = true;
        }
    } else {
        const int previousActive = census_.activeEnemies;
        census_ = {};
        const SpawnFrame spawn{frame.minimumX, frame.maximumX, frame.minimumY,
                               frame.maximumY, previousActive, true};
        for (size_t slot = 0; slot < 116; ++slot) {
            auto &e = pool_.at(slot);
            if (!e.behavior)
                continue;
            pool_.animate(e, frame_);
            if (e.behavior == 1)
                updatePlayer(pool_, player_, input, sounds_);
            else if (e.behavior == 8)
                world_.anchor(e, frame);
            else if (e.behavior == 14)
                world_.respawn(e);
            else if (e.behavior == 10 || e.behavior == 11)
                formations_.updateSpawner(e, spawn, census_);
            else if (e.behavior == 19)
                updateMenuParticle(e, assets_);
            else
                combat_.update(slot, frame, census_);
            if (native_)
                native_->record(pool_, slot);
        }
        census_.nearestHead = -1;
        census_.nearestX = census_.nearestY = 0x0fff0000;
        const size_t drawEnd = phase_ == GamePhase::GameOver ? 101 : 116;
        *nativeDrawBase_ = pool_;
        nativeVisualRandom_ = random_.state();
        nativeVisualEffects_ = effects_.state();
        for (size_t slot = 0; slot < drawEnd; ++slot)
            if (pool_.at(slot).behavior)
                visual.entity(pool_.at(slot));
        world_.grid(visual, frame, background_[5]);
        if (phase_ != GamePhase::GameOver)
            world_.lasers(visual, frame);
        const auto hudFirst = canvas_.draws().size();
        hud(visual, frame);
        cacheHud(hudFirst);
        if (phase_ == GamePhase::Playing) {
            if (formations_.programEnded() && census_.activeEnemies + census_.waiting == 0 &&
                !player_.reverseTransition && player_.transitionTicks == 0) {
                player_.transitionTicks = endurance_ ? -100 : -1;
                player_.reverseTransition = true;
                sounds_.push_back(17);
                combatState_.frameFlashTicks = 12;
                if (!endurance_)
                    pool_.at(89).drawing = 9;
                formations_.resetEmissionCount();
            }
            if (player_.transitionTicks >= 100 || player_.transitionTicks <= -100) {
                player_.bombActive = 0;
                if (!player_.reverseTransition || player_.transitionTicks > 0) {
                    ++deaths_;
                    if (--lives_ < 0)
                        gameOver();
                    else
                        beginPending_ = true;
                } else
                    stageFinished();
            }
        }
    }
    ++frame_;
    colorPhase_ += .1f;
}
int Game::nativeSubsteps() const { return native_ ? native_->substeps() : 1; }
void Game::setNativeRate(bool enabled, int substeps) {
    if (substeps != 3 && substeps != 6)
        throw std::runtime_error("Native rate must be 100 or 200 Hz");
    if (enabled == bool(native_) && (!native_ || native_->substeps() == substeps))
        return;
    if (enabled) {
        native_ = std::make_unique<NativeMotion>(substeps);
        nativeDrawBase_ = std::make_unique<EntityPool>();
    } else {
        native_.reset();
        if (!nativeDrawBase_)
            nativeDrawBase_ = std::make_unique<EntityPool>();
    }
    pool_.nativeMotion = native_.get();
    nativePhase_ = 0;
    pendingPressed_ = pendingHeld_ = 0;
    nativeHud_.clear();
    nativeHudFirst_ = 0;
}
void Game::step(const LogicalInput &input) {
    if (!native_) {
        sourceStep(input);
        return;
    }
    pendingPressed_ |= input.pressed;
    pendingHeld_ |= input.held & 0x60u;
    if (nativePhase_ == 0) {
        native_->beginInterval();
        combat_.beginNativeInterval();
        auto sourceInput = input;
        sourceInput.pressed = pendingPressed_;
        sourceInput.held |= pendingHeld_;
        pendingPressed_ = pendingHeld_ = 0;
        sourceStep(sourceInput);
    } else {
        sounds_.clear();
        if (phase_ != GamePhase::StageClear) {
            player_.worldX = pool_.at(EntityPool::Player).x;
            player_.worldY = pool_.at(EntityPool::Player).y;
            for (size_t slot = 0; slot < EntityPool::Capacity; ++slot) {
                auto &e = pool_.at(slot);
                if (e.behavior == 1)
                    native_->player(e, player_, input);
                else if (e.behavior == 4)
                    native_->follow(pool_, e);
                else
                    native_->continueMove(pool_, slot);
                native_->record(pool_, slot);
            }
            const ChainFrame f{pixel(player_.worldX) + 16,
                               pixel(player_.worldX) + player_.width() - 16,
                               pixel(player_.worldY) + 16,
                               pixel(player_.worldY) + player_.height() - 16,
                               .1f,
                               frame_ - 1};
            combat_.nativeContacts(f);
            if (phase_ != GamePhase::GameOver)
                world_.nativeContacts();
        }
    }
    nativePhase_ = (nativePhase_ + 1) % native_->substeps();
    if (!deferPresentation_)
        renderNative();
    else {
        // Keep the per-motion decorative camera samples without regenerating
        // thousands of draw commands for frames the host will never present.
        auto grid = canvas_.decorativeGrid();
        const double fraction = ((nativePhase_ + native_->substeps() - 1) % native_->substeps()) /
                                double(native_->substeps());
        grid.frame = nativeVisualFrame_ + fraction;
        grid.advance = backgroundAdvance_ +
                       fraction * (phase_ == GamePhase::StageClear ? player_.scoreStep * 2. : 0.);
        grid.worldX = player_.worldX;
        grid.worldY = player_.worldY;
        canvas_.decorativeGrid(grid);
        canvas_.playfield({true, player_.worldWidth(), player_.worldHeight(),
                           player_.worldX / 65536., player_.worldY / 65536.});
    }
}
void Game::renderNative(bool referenceBackground, bool actors) {
    if (!native_)
        return;
    // The original draw routines mutate their entity and consume two RNGs.
    // Render copies, retaining those mutations exactly once in sourceStep.
    auto player = player_;
    GameRandom random(nativeVisualRandom_);
    EffectRandom effects(nativeVisualEffects_);
    SourceVisuals visual(canvas_, assets_, player, random, effects);
    visual.frame(nativeVisualFrame_, nativeVisualPhase_);
    canvas_.clear(nativeHud_.clearColor());
    visual.background(backgroundAdvance_, nativeVisualBackground_,
                      ((nativePhase_ + native_->substeps() - 1) % native_->substeps()) /
                          double(native_->substeps()),
                      phase_ == GamePhase::StageClear ? player_.scoreStep * 2. : 0.,
                      referenceBackground);
    if (phase_ == GamePhase::StageClear) {
        canvas_.append(nativeHud_, nativeHudFirst_);
        return;
    }
    if (actors) {
        const auto drawEnd = phase_ == GamePhase::GameOver ? 101u : 116u;
        for (size_t slot = 0; slot < drawEnd; ++slot)
            if (pool_.at(slot).behavior) {
                const auto &live = pool_.at(slot);
                const auto &base = nativeDrawBase_->at(slot);
                auto entity = live;
                if (base.behavior == live.behavior && base.drawing == live.drawing) {
                    entity = base;
                    // Freeze source-owned death/announcement/respawn transformations;
                    // all interactive bodies use their genuinely integrated live pose.
                    if (entity.drawing != 9 && entity.drawing != 16 && live.behavior != 14) {
                        entity.x = live.x;
                        entity.y = live.y;
                        entity.depth = live.depth;
                        entity.heading = live.heading;
                        entity.vx = live.vx;
                        entity.vy = live.vy;
                    }
                    if (entity.drawing == 2)
                        entity.history = live.history;
                }
                visual.entity(entity);
            }
        const ChainFrame f{pixel(player_.worldX) + 16,
                           pixel(player_.worldX) + player_.width() - 16,
                           pixel(player_.worldY) + 16,
                           pixel(player_.worldY) + player_.height() - 16,
                           .1f,
                           frame_ - 1};
        world_.grid(visual, f, background_[5], false);
        if (phase_ != GamePhase::GameOver)
            world_.lasers(visual, f, false);
    }
    canvas_.append(nativeHud_, nativeHudFirst_);
}
Canvas Game::expandedCanvas() {
    if (phase_ == GamePhase::StageClear)
        return canvas_;
    constexpr int pad = 1024;
    const int width = canvas_.width(), height = canvas_.height();
    Canvas result;
    result.dimensions(width + 2 * pad, height + 2 * pad);
    result.clear(canvas_.clearColor());
    auto player = player_;
    player.worldX = sub32(player.worldX, fixed16(pad));
    player.worldY = sub32(player.worldY, fixed16(pad));
    GameRandom random(nativeVisualRandom_);
    EffectRandom effects(nativeVisualEffects_);
    SourceVisuals visual(result, assets_, player, random, effects);
    visual.frame(nativeVisualFrame_, nativeVisualPhase_);
    const auto drawEnd = phase_ == GamePhase::GameOver ? 101u : 116u;
    for (size_t slot = 0; slot < drawEnd; ++slot)
        if (pool_.at(slot).behavior) {
            const auto &live = pool_.at(slot);
            const auto &base = nativeDrawBase_->at(slot);
            auto entity = live;
            if (base.behavior == live.behavior && base.drawing == live.drawing) {
                entity = base;
                if (entity.drawing != 9 && entity.drawing != 16 && live.behavior != 14) {
                    entity.x = live.x;
                    entity.y = live.y;
                    entity.depth = live.depth;
                    entity.heading = live.heading;
                    entity.vx = live.vx;
                    entity.vy = live.vy;
                }
                if (entity.drawing == 2)
                    entity.history = live.history;
            }
            if (entity.drawing == 9 || entity.drawing == 16 || entity.drawing == 14) {
                Canvas local;
                local.dimensions(width, height);
                auto localPlayer = player_;
                SourceVisuals localVisual(local, assets_, localPlayer, random, effects);
                localVisual.frame(nativeVisualFrame_, nativeVisualPhase_);
                localVisual.entity(entity);
                local.translate(float(pad), float(pad));
                result.append(local);
                continue;
            }
            if (entity.drawing == 0) {
                entity.x = add32(entity.x, fixed16(pad));
                entity.y = add32(entity.y, fixed16(pad));
                for (auto &p : entity.history) {
                    p.x = add32(p.x, fixed16(pad));
                    p.y = add32(p.y, fixed16(pad));
                }
            }
            visual.entity(entity);
        }
    const ChainFrame f{pixel(player_.worldX) + 16,
                       pixel(player_.worldX) + width - 16,
                       pixel(player_.worldY) + 16,
                       pixel(player_.worldY) + height - 16,
                       .1f,
                       frame_ - 1};
    world_.grid(visual, f, background_[5], false);
    if (phase_ != GamePhase::GameOver)
        world_.lasers(visual, f, false);
    result.translate(float(-pad), float(-pad));
    result.dimensions(width, height);
    // Keep the reference background in its original raster; enhanced mode
    // evaluates the same recipe across the entire physical display.
    Canvas finished;
    finished.dimensions(width, height);
    finished.clear(canvas_.clearColor());
    const auto &grid = canvas_.decorativeGrid();
    if (grid.valid)
        for (size_t n = 0; n < grid.first && n < canvas_.draws().size(); ++n)
            finished.appendDraw(canvas_.draws()[n]);
    finished.append(result);
    finished.append(nativeHud_, nativeHudFirst_);
    finished.decorativeGrid(grid);
    finished.playfield(canvas_.playfield());
    return finished;
}
} // namespace grpp
