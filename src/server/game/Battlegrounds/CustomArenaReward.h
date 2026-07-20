#ifndef CUSTOM_ARENA_REWARD_H
#define CUSTOM_ARENA_REWARD_H

#include "Define.h"

class Battleground;

namespace CustomArenaReward
{
    void RewardArenaEnd(Battleground* bg, uint32 winner);
}

#endif