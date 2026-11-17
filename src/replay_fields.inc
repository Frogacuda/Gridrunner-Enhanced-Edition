// Version 2 field order is part of the replay schema. Change the rules identifier
// whenever a field order, width or simulation meaning becomes incompatible.
template <class A> static void value(A &a, TrailPoint &v) {
    a(v.x, v.y, v.scale, v.tag, v.heading, v.vx, v.vy, v.color);
}
template <class A> static void value(A &a, SpriteTransform &v) {
    a(v.noiseSeed, v.jitterX, v.jitterY, v.flags, v.axisXX, v.axisXY, v.axisYX, v.axisYY, v.pivotX,
      v.pivotY, v.sheet, v.x, v.y, v.width, v.height, v.tileWidth, v.tileHeight);
}
template <class A> static void value(A &a, Entity &v) {
    a(v.x, v.y, v.vx, v.vy, v.ax, v.ay, v.targetX, v.targetY, v.sprite, v.behavior, v.drawing,
      v.animation, v.animationBase, v.animationShift, v.animationFrames, v.life, v.phase,
      v.attackParameters, v.companionHealth, v.companionMaximumHealth, v.oscillatorYStep, v.scale,
      v.flags, v.historyCursor, v.commandOrHealth, v.depth, v.speed, v.depthAcceleration,
      v.outsideTicks, v.extent, v.next, v.previous, v.chainIndex, v.historySpacing, v.mode,
      v.heading, v.turnRate, v.hitFlash, v.effectMode, v.colorPhase, v.colorCycle, v.trailProfile,
      v.transform, v.history);
}
template <class A> static void value(A &a, EntityCensus &v) {
    a(v.waiting, v.activeEnemies, v.nearestX, v.nearestY, v.nearestHead);
}
template <class A> static void value(A &a, PlayerState &v) {
    a(v.transitionTicks, v.arse, v.endurance, v.fireParity, v.reverseTransition, v.scoreClock,
      v.scoreStep, v.shotCountdown, v.shotInterval, v.sheepPower, v.followerShotIndex, v.worldX,
      v.worldY, v.alternateCraftUnlocked, v.alternateCraft, v.selectedPlayer, v.bombActive,
      v.bombTicks, v.beamTarget, v.beamMode, v.beamHitConsumed);
}
template <class A> static void value(A &a, FormationTuning &v) {
    a(v.headHealth, v.segmentHealth, v.podResistance, v.allowance, v.segmentCount, v.headAttack,
      v.speedX, v.speedY, v.anchorSpeedX, v.anchorSpeedY, v.anchorDepthStep, v.headSprite,
      v.segmentSprite, v.historySpacing, v.splitTransform, v.retargetInterval, v.retargetTurnStep,
      v.beamReach, v.specialTurnChance, v.flyingPlantsGrid, v.flyingTracking,
      v.flyingAlternateSpeed);
}
template <class A> static void value(A &a, CombatState &v) {
    a(v.score, v.stageScore, v.killsUntilDrop, v.pendingRestorePower, v.restorePower, v.difficulty,
      v.frameFlashTicks, v.travellingAtDeath, v.shotColorPhase);
}
template <class A> static void value(A &a, WorldState &v) {
    a(v.rippleTicks, v.rippleDepth, v.forecast, v.laserTicks, v.laserX, v.laserY);
}
template <class A> static void value(A &a, NativeMotion::Axis &v) {
    a(v.remainder, v.expected, v.initialized);
}
template <class A> static void value(A &a, NativeMotion::Track &v) {
    a(v.velocity, v.axes, v.behavior, v.drawing, v.moving, v.history, v.cursor, v.historyBehavior,
      v.particles, v.particleFirst, v.particleCount, v.particlePhase, v.particleX, v.particleY,
      v.particleDx, v.particleDy);
}
template <class A> static void value(A &a, SpawnProgram::Loop &v) {
    uint64_t begin = v.begin;
    a(begin, v.remaining);
    v.begin = size_t(begin);
}
