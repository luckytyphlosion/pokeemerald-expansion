#include "global.h"
#include "battle.h"
#include "battle_ai_script_commands.h"
#include "battle_anim.h"
#include "battle_arena.h"
#include "battle_controllers.h"
#include "battle_message.h"
#include "battle_interface.h"
#include "battle_setup.h"
#include "battle_tower.h"
#include "battle_tv.h"
#include "bg.h"
#include "data.h"
#include "frontier_util.h"
#include "item.h"
#include "link.h"
#include "main.h"
#include "m4a.h"
#include "palette.h"
#include "pokeball.h"
#include "pokemon.h"
#include "random.h"
#include "reshow_battle_screen.h"
#include "sound.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "util.h"
#include "window.h"
#include "constants/battle_anim.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/songs.h"
#include "constants/trainers.h"
#include "trainer_hill.h"
#include "malloc.h"
#include "battle_ai/estimate_nash.h"
#include "mgba.h"
#include "printf.h"
#include "decompress.h"

#define NUM_SIDES 2
#define B_SIDE_PLAYER_NZ B_SIDE_PLAYER + 1
#define B_SIDE_OPPONENT_NZ B_SIDE_OPPONENT + 1
#define B_NEITHER_SIDE_NZ B_SIDE_OPPONENT_NZ + 1

static const u8 * const sDebug_SideNames[] = {
    "B_SIDE_NONE",
    "B_SIDE_PLAYER_NZ",
    "B_SIDE_OPPONENT_NZ",
    "B_NEITHER_SIDE_NZ"
};

static const u8 * const sDebug_BSideNames[] = {
    "B_SIDE_PLAYER",
    "B_SIDE_OPPONENT"
};

static void NewAI_CalculateAllDamages(void);
static void NewAI_InitSomeFields(void);
static void NewAI_PopulateAllBattleMons(void);
static void NewAI_PopulateBattleMonsForParty(struct Pokemon * srcParty, struct BattlePokemon * dstParty, u8 * invalidMons, uint monPartyIndex, uint monPos);
static s32 NewAI_CalcDamage(u16 move, u8 battlerAtk, u8 battlerDef);
static void NewAI_CalculateWeights(void);
static uint NewAI_DealDamageUntilOneFaints(u16 * aiMonHp, u16 * playerMonHp, s32 (* damagesForMonsOut)[NUM_SIDES][MAX_MON_MOVES], uint aiFaster);
static s32 NewAI_GetBestDamage(s32 * damages);
static uint NewAI_DealOneTurnOfDamage_ReturnWhichIfOneFainted(u16 * aiMonHp, u16 * playerMonHp, s32 aiDamage, s32 playerDamage, uint aiFaster);
static void NewAI_WriteWeightToMatrix(uint whoWon, s16 * weight, struct BattlePokemon * curAiMon, struct BattlePokemon * curPlayerMon);
static void NewAI_InvalidateBadSwitches(uint monPartyIndex, uint invalidMons, u16 * invalidActions, u8 * numActions, uint whichSide);
static uint NewAI_RemoveInvalidLinesFromWeightsThenSolve(void);
static void NewAI_EmitChosenAction(uint chosenAction);

// actions:
// move1 move2 move3 move4 switch1 switch2 switch3 switch4 switch5
// playerMon1 move1's on aiMon1
// playerMon1 move2's on aiMon1
// playerMon1 move3's on aiMon1
// playerMon1 move4's on aiMon1
// [B_SIDE_PLAYER][0][0][0]

// 
// move1 -> mon1

#define debug_printf mgba_printf
#define SNPRINTF_E(printBufferCursor, format, ...) \
    printBufferCursor += snprintf(gDecompressionBuffer + printBufferCursor, 0x4000 - printBufferCursor, format, __VA_ARGS__); \
    if (printBufferCursor == -1 || printBufferCursor >= 0x4000) { \
        while (1); \
    }

struct AIThinking
{
    u8 invalidPlayerMons;
    u8 invalidAiMons;
    u8 playerLeftPos;
    u8 oppLeftPos;
    u8 numPlayerActions;
    u8 numAiActions;
    u8 aiMonPartyIndex;
    u8 playerMonPartyIndex;
    u16 invalidPlayerActions;
    u16 invalidAiActions;
    struct BattlePokemon savedBattleMons[MAX_BATTLERS_COUNT];
    union {
        struct {
            struct BattlePokemon playerMons[PARTY_SIZE];
            struct BattlePokemon aiMons[PARTY_SIZE];
        } side;
        struct BattlePokemon allMons[PARTY_SIZE * 2];
    } mons;

    struct BattlePokemon allMonsCopy[PARTY_SIZE * 2];

    s32 allDamages[PARTY_SIZE][PARTY_SIZE][NUM_SIDES][MAX_MON_MOVES];
    // row = ai mon actions
    // col = player mon actions
    
    s16 weights[9][9];
};

EWRAM_DATA static struct AIThinking * sAIThinkingPtr = NULL;

void NewAI_Main (void)
{
    uint chosenAction;

    sAIThinkingPtr = AllocZeroed(sizeof(struct AIThinking));
    if (sAIThinkingPtr == NULL) {
        while (1);
    }
    CpuCopy32(gBattleMons, sAIThinkingPtr->savedBattleMons, sizeof(gBattleMons));
    
    NewAI_InitSomeFields();
    NewAI_PopulateAllBattleMons();
    NewAI_CalculateAllDamages();
    NewAI_CalculateWeights();
    CpuCopy32(sAIThinkingPtr->savedBattleMons, gBattleMons, sizeof(gBattleMons));
    chosenAction = NewAI_RemoveInvalidLinesFromWeightsThenSolve();
    NewAI_EmitChosenAction(chosenAction);

    free(sAIThinkingPtr);
}

static void NewAI_InitSomeFields (void)
{
    sAIThinkingPtr->oppLeftPos = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
    sAIThinkingPtr->playerLeftPos = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    sAIThinkingPtr->aiMonPartyIndex = gBattlerPartyIndexes[sAIThinkingPtr->oppLeftPos];
    sAIThinkingPtr->playerMonPartyIndex = gBattlerPartyIndexes[sAIThinkingPtr->playerLeftPos];
}

static void NewAI_CalculateAllDamages (void)
{
    uint aiMonIndex;
    uint playerMonIndex;
    uint whichSide;
    uint curMoveIndex;
    uint playerLeftPos;
    uint oppLeftPos;
    struct BattlePokemon * playerMon;
    struct BattlePokemon * aiMon;
    uint invalidPlayerMons;
    uint invalidAiMons;
    uint printBufferCursor;

    playerLeftPos = sAIThinkingPtr->playerLeftPos;
    oppLeftPos = sAIThinkingPtr->oppLeftPos;

    playerMon = &gBattleMons[playerLeftPos];
    aiMon = &gBattleMons[oppLeftPos];
    invalidPlayerMons = sAIThinkingPtr->invalidPlayerMons;
    invalidAiMons = sAIThinkingPtr->invalidAiMons;

    for (aiMonIndex = 0; aiMonIndex < PARTY_SIZE; aiMonIndex++) {
        if (invalidAiMons & (1 << aiMonIndex)) {
            continue;
        }

        CpuCopy32(&sAIThinkingPtr->mons.side.aiMons[aiMonIndex], aiMon, sizeof(struct BattlePokemon));

        for (playerMonIndex = 0; playerMonIndex < PARTY_SIZE; playerMonIndex++) {
            if (invalidPlayerMons & (1 << playerMonIndex)) {
                continue;
            }

            CpuCopy32(&sAIThinkingPtr->mons.side.playerMons[playerMonIndex], playerMon, sizeof(struct BattlePokemon));

            for (whichSide = 0; whichSide < NUM_SIDES; whichSide++) {
                uint battlerAtk;
                uint battlerDef;
                u16 * moves;
                s32 * allDamagesMovesFree;

                if (whichSide == B_SIDE_PLAYER) {
                    battlerAtk = playerLeftPos;
                    battlerDef = oppLeftPos;
                    moves = playerMon->moves;
                } else {
                    battlerAtk = oppLeftPos;
                    battlerDef = playerLeftPos;
                    moves = aiMon->moves;
                }

                allDamagesMovesFree = sAIThinkingPtr->allDamages[aiMonIndex][playerMonIndex][whichSide];
                // todo: move limitations here
                for (curMoveIndex = 0; curMoveIndex < MAX_MON_MOVES; curMoveIndex++) {
                    uint curMove = *moves;

                    // lazy + this is a PoC anyway
                    if (gBattleMoves[curMove].power > 1) {
                        *allDamagesMovesFree = NewAI_CalcDamage(curMove, battlerAtk, battlerDef);
                    } else {
                        *allDamagesMovesFree = 0;
                    }
                    moves++;
                    allDamagesMovesFree++;
                }
            }
        }
    }

    for (aiMonIndex = 0; aiMonIndex < PARTY_SIZE; aiMonIndex++) {
        debug_printf("aiMon[%d]: %s", aiMonIndex, gSpeciesNamesAscii[sAIThinkingPtr->mons.side.aiMons[aiMonIndex].species]);
        for (playerMonIndex = 0; playerMonIndex < PARTY_SIZE; playerMonIndex++) {
            debug_printf("  playerMon[%d]: %s", playerMonIndex, gSpeciesNamesAscii[sAIThinkingPtr->mons.side.playerMons[playerMonIndex].species]);
            for (whichSide = 0; whichSide < NUM_SIDES; whichSide++) {
                printBufferCursor = 0;
                SNPRINTF_E(printBufferCursor, "    %s: ", sDebug_BSideNames[whichSide]);

                for (curMoveIndex = 0; curMoveIndex < MAX_MON_MOVES; curMoveIndex++) {
                    SNPRINTF_E(printBufferCursor, "%04d ", sAIThinkingPtr->allDamages[aiMonIndex][playerMonIndex][whichSide][curMoveIndex]);
                }
                debug_printf("%s", gDecompressionBuffer);
            }
        }
    }

}

static void NewAI_PopulateAllBattleMons (void)
{
    NewAI_PopulateBattleMonsForParty(gPlayerParty,
        sAIThinkingPtr->mons.side.playerMons,
        &sAIThinkingPtr->invalidPlayerMons,
        sAIThinkingPtr->playerMonPartyIndex,
        sAIThinkingPtr->playerLeftPos
    );
    NewAI_PopulateBattleMonsForParty(gEnemyParty,
        sAIThinkingPtr->mons.side.aiMons,
        &sAIThinkingPtr->invalidAiMons,
        sAIThinkingPtr->aiMonPartyIndex,
        sAIThinkingPtr->oppLeftPos
    );

    CpuCopy32(&sAIThinkingPtr->mons.allMons, &sAIThinkingPtr->allMonsCopy, sizeof(sAIThinkingPtr->allMonsCopy));
}

static void NewAI_PopulateBattleMonsForParty (struct Pokemon * srcParty, struct BattlePokemon * dstParty, u8 * invalidMons, uint monPartyIndex, uint monPos)
{
    uint i;

    *invalidMons = 0;

    // todo, optimize to not convert mons currently out

    for (i = 0; i < PARTY_SIZE; i++) {
        struct Pokemon * srcMon;
        struct BattlePokemon * dstMon;

        dstMon = &dstParty[i];

        if (i == monPartyIndex) {
            CpuCopy32(&gBattleMons[monPos], dstMon, sizeof(struct BattlePokemon));
        } else {
            uint monSpecies;

            srcMon = &srcParty[i];
            monSpecies = GetMonData(srcMon, MON_DATA_SPECIES);

            if (monSpecies == SPECIES_NONE
            || monSpecies == SPECIES_EGG
            || GetMonData(srcMon, MON_DATA_HP) == 0) {
                dstMon->species = SPECIES_NONE;
                *invalidMons |= 1 << i;
                continue;
            }

            PokemonToBattleMon(srcMon, dstMon);
        }
    }
}

static s32 NewAI_CalcDamage (u16 move, u8 battlerAtk, u8 battlerDef)
{
    s32 dmg, moveType;

    gBattleStruct->dynamicMoveType = 0;
    SetTypeBeforeUsingMove(move, battlerAtk);
    GET_MOVE_TYPE(move, moveType);
    dmg = CalculateMoveDamage(move, battlerAtk, battlerDef, moveType, 0, FALSE, FALSE, FALSE);

    return dmg;
}

static void NewAI_CalculateWeights (void)
{
    // 0-3: use move
    // 4-8: switch
    uint aiInitialAction;
    uint playerInitialAction;
    uint aiFaster;
    uint aiMonPartyIndex;
    uint playerMonPartyIndex;

    struct BattlePokemon * aiMons;
    struct BattlePokemon * playerMons;

    struct BattlePokemon * curAiMon;
    struct BattlePokemon * curPlayerMon;

    s32 (* damagesForMonsOut)[NUM_SIDES][MAX_MON_MOVES];
    uint i, j;

    // end declarations

    aiMons = sAIThinkingPtr->mons.side.aiMons;
    playerMons = sAIThinkingPtr->mons.side.playerMons;

    aiMonPartyIndex = sAIThinkingPtr->aiMonPartyIndex;;
    playerMonPartyIndex = sAIThinkingPtr->playerMonPartyIndex;

    curAiMon = &aiMons[aiMonPartyIndex];
    curPlayerMon = &playerMons[playerMonPartyIndex];
    aiFaster = curAiMon->speed > curPlayerMon->speed;

    damagesForMonsOut = &sAIThinkingPtr->allDamages[aiMonPartyIndex][playerMonPartyIndex];

    for (aiInitialAction = 0; aiInitialAction < 4; aiInitialAction++) {
        for (playerInitialAction = 0; playerInitialAction < 4; playerInitialAction++) {
            s32 aiDamage = (*damagesForMonsOut)[B_SIDE_OPPONENT][aiInitialAction];
            s32 playerDamage = (*damagesForMonsOut)[B_SIDE_PLAYER][playerInitialAction];
            uint whoWon;

            whoWon = NewAI_DealOneTurnOfDamage_ReturnWhichIfOneFainted(&curAiMon->hp, &curPlayerMon->hp, aiDamage, playerDamage, aiFaster);
            if (!whoWon) {
                whoWon = NewAI_DealDamageUntilOneFaints(&curAiMon->hp, &curPlayerMon->hp, damagesForMonsOut, aiFaster);
            }

            // do lazy heuristic for now
            NewAI_WriteWeightToMatrix(whoWon, &sAIThinkingPtr->weights[aiInitialAction][playerInitialAction], curAiMon, curPlayerMon);

            // lazy
            CpuCopy32(&sAIThinkingPtr->allMonsCopy, &sAIThinkingPtr->mons.allMons, sizeof(sAIThinkingPtr->allMonsCopy));
        }
    }

    sAIThinkingPtr->invalidPlayerActions = 0;
    sAIThinkingPtr->invalidAiActions = 0;
    sAIThinkingPtr->numPlayerActions = 4;
    sAIThinkingPtr->numAiActions = 4;

    // invalidate bad actions first
    // player
    NewAI_InvalidateBadSwitches(playerMonPartyIndex, sAIThinkingPtr->invalidPlayerMons, &sAIThinkingPtr->invalidPlayerActions, &sAIThinkingPtr->numPlayerActions, B_SIDE_PLAYER);
    NewAI_InvalidateBadSwitches(aiMonPartyIndex, sAIThinkingPtr->invalidAiMons, &sAIThinkingPtr->invalidAiActions, &sAIThinkingPtr->numAiActions, B_SIDE_OPPONENT);

    // now test switches

    // first test player switching on ai's move
    for (playerInitialAction = 4; playerInitialAction < 9; playerInitialAction++) {
        uint playerSwitchIndex;

        if (sAIThinkingPtr->invalidPlayerActions & (1 << playerInitialAction)) {
            continue;
        }

        playerSwitchIndex = playerInitialAction - 4;
        if (playerSwitchIndex >= playerMonPartyIndex) {
            playerSwitchIndex++;
        }

        curPlayerMon = &playerMons[playerSwitchIndex];
        damagesForMonsOut = &sAIThinkingPtr->allDamages[aiMonPartyIndex][playerSwitchIndex];

        aiFaster = curAiMon->speed > curPlayerMon->speed;
        
        for (aiInitialAction = 0; aiInitialAction < 4; aiInitialAction++) {
            uint whoWon = 0;
            s32 aiDamage = (*damagesForMonsOut)[B_SIDE_OPPONENT][aiInitialAction];

            if (curPlayerMon->hp <= aiDamage) {
                curPlayerMon->hp = 0;
                whoWon = B_SIDE_OPPONENT_NZ;
            } else {
                curPlayerMon->hp -= aiDamage;
                whoWon = NewAI_DealDamageUntilOneFaints(&curAiMon->hp, &curPlayerMon->hp, damagesForMonsOut, aiFaster);
            }

            NewAI_WriteWeightToMatrix(whoWon, &sAIThinkingPtr->weights[aiInitialAction][playerInitialAction], curAiMon, curPlayerMon);
            CpuCopy32(&sAIThinkingPtr->allMonsCopy, &sAIThinkingPtr->mons.allMons, sizeof(sAIThinkingPtr->allMonsCopy));
        }
    }

    curPlayerMon = &playerMons[playerMonPartyIndex];

    // now test ai switching on player's move
    for (aiInitialAction = 4; aiInitialAction < 9; aiInitialAction++) {
        uint aiSwitchIndex;

        if (sAIThinkingPtr->invalidAiActions & (1 << aiInitialAction)) {
            continue;
        }

        aiSwitchIndex = aiInitialAction - 4;
        if (aiSwitchIndex >= aiMonPartyIndex) {
            aiSwitchIndex++;
        }

        curAiMon = &aiMons[aiSwitchIndex];
        damagesForMonsOut = &sAIThinkingPtr->allDamages[aiSwitchIndex][playerMonPartyIndex];

        aiFaster = curAiMon->speed > curPlayerMon->speed;

        for (playerInitialAction = 0; playerInitialAction < 4; playerInitialAction++) {
            uint whoWon = 0;
            s32 playerDamage = (*damagesForMonsOut)[B_SIDE_PLAYER][playerInitialAction];

            if (curAiMon->hp <= playerDamage) {
                curAiMon->hp = 0;
                whoWon = B_SIDE_PLAYER_NZ;
            } else {
                curAiMon->hp -= playerDamage;
                whoWon = NewAI_DealDamageUntilOneFaints(&curAiMon->hp, &curPlayerMon->hp, damagesForMonsOut, aiFaster);
            }

            NewAI_WriteWeightToMatrix(whoWon, &sAIThinkingPtr->weights[aiInitialAction][playerInitialAction], curAiMon, curPlayerMon);
            CpuCopy32(&sAIThinkingPtr->allMonsCopy, &sAIThinkingPtr->mons.allMons, sizeof(sAIThinkingPtr->allMonsCopy));
        }
    }

    // now test double switches
    for (aiInitialAction = 4; aiInitialAction < 9; aiInitialAction++) {
        uint aiSwitchIndex;
        s32 (* damagesForAiMonOut)[PARTY_SIZE][NUM_SIDES][MAX_MON_MOVES];

        if (sAIThinkingPtr->invalidAiActions & (1 << aiInitialAction)) {
            debug_printf("double switch ai continue");
            continue;
        }

        aiSwitchIndex = aiInitialAction - 4;
        if (aiSwitchIndex >= aiMonPartyIndex) {
            aiSwitchIndex++;
        }

        curAiMon = &aiMons[aiSwitchIndex];
        damagesForAiMonOut = &sAIThinkingPtr->allDamages[aiSwitchIndex];

        for (playerInitialAction = 4; playerInitialAction < 9; playerInitialAction++) {
            uint playerSwitchIndex;
            uint whoWon;

            if (sAIThinkingPtr->invalidPlayerActions & (1 << playerInitialAction)) {
                debug_printf("double switch player continue");
                continue;
            }

            playerSwitchIndex = playerInitialAction - 4;
            if (playerSwitchIndex >= playerMonPartyIndex) {
                playerSwitchIndex++;
            }

            curPlayerMon = &playerMons[playerSwitchIndex];
            damagesForMonsOut = &((*damagesForAiMonOut)[playerSwitchIndex]);
            //debug_printf("damagesForMonsOut: %p", damagesForMonsOut);
            aiFaster = curAiMon->speed > curPlayerMon->speed;

            whoWon = NewAI_DealDamageUntilOneFaints(&curAiMon->hp, &curPlayerMon->hp, damagesForMonsOut, aiFaster);
            debug_printf("double switch ai %d pl %d whoWon: %s", aiInitialAction, playerInitialAction, sDebug_SideNames[whoWon]);
            NewAI_WriteWeightToMatrix(whoWon, &sAIThinkingPtr->weights[aiInitialAction][playerInitialAction], curAiMon, curPlayerMon);
            CpuCopy32(&sAIThinkingPtr->allMonsCopy, &sAIThinkingPtr->mons.allMons, sizeof(sAIThinkingPtr->allMonsCopy));
        }
    }
}

static uint NewAI_DealDamageUntilOneFaints (u16 * aiMonHp, u16 * playerMonHp, s32 (* damagesForMonsOut)[NUM_SIDES][MAX_MON_MOVES], uint aiFaster)
{
    s32 aiBestDamage;
    s32 playerBestDamage;
    uint whoWon = 0;

    aiBestDamage = NewAI_GetBestDamage((*damagesForMonsOut)[B_SIDE_OPPONENT]);
    playerBestDamage = NewAI_GetBestDamage((*damagesForMonsOut)[B_SIDE_PLAYER]);

    // player gets off 4 hits
    // i.e. numAiHits - 1
    // ai mon has 100 hp, player mon does 16 damage
    // player mon has 200 hp, ai mon does 40 damage
    // ai mon is faster
    if (aiBestDamage != 0 && playerBestDamage != 0) {
        uint numAiHits;
        uint numPlayerHits;

        numAiHits = (*playerMonHp + (aiBestDamage - 1)) / aiBestDamage;
        numPlayerHits = (*aiMonHp + (playerBestDamage - 1)) / playerBestDamage;

        if (aiFaster) {
            if (numAiHits <= numPlayerHits) {
                *playerMonHp = 0;
                *aiMonHp -= playerBestDamage * (numAiHits - 1);
                whoWon = B_SIDE_OPPONENT_NZ;
            } else {
                // (numAiHits > numPlayerHits)
                *aiMonHp = 0;
                *playerMonHp -= aiBestDamage * numPlayerHits;
                whoWon = B_SIDE_PLAYER_NZ;
            }
        } else {
            if (numPlayerHits <= numAiHits) {
                *aiMonHp = 0;
                *playerMonHp -= aiBestDamage * (numPlayerHits - 1);
                whoWon = B_SIDE_PLAYER_NZ;
            } else {
                *playerMonHp = 0;
                *aiMonHp -= playerBestDamage * numAiHits;
                whoWon = B_SIDE_OPPONENT_NZ;
            }
        }
    } else if (aiBestDamage == 0 && playerBestDamage != 0) {
        *aiMonHp = 0;
        whoWon = B_SIDE_PLAYER_NZ;
    } else if (aiBestDamage != 0 && playerBestDamage == 0) {
        *playerMonHp = 0;
        whoWon = B_SIDE_OPPONENT_NZ;
    } else {
        whoWon = B_NEITHER_SIDE_NZ;
    }

    return whoWon;
}

static s32 NewAI_GetBestDamage (s32 * damages)
{
    uint i;
    s32 bestDamage = damages[0];

    for (i = 1; i < 4; i++) {
        if (bestDamage < damages[i]) {
            bestDamage = damages[i];
        }
    }

    return bestDamage;
}

static uint NewAI_DealOneTurnOfDamage_ReturnWhichIfOneFainted (u16 * aiMonHp, u16 * playerMonHp, s32 aiDamage, s32 playerDamage, uint aiFaster)
{
    if (aiFaster) {
        if (*playerMonHp <= aiDamage) {
            *playerMonHp = 0;
            return B_SIDE_OPPONENT_NZ;
        } else {
            *playerMonHp -= aiDamage;
        }

        if (*aiMonHp <= playerDamage) {
            *aiMonHp = 0;
            return B_SIDE_PLAYER_NZ;
        } else {
            *aiMonHp -= playerDamage;
        }

        return 0;
    } else {
        if (*aiMonHp <= playerDamage) {
            *aiMonHp = 0;
            return B_SIDE_PLAYER_NZ;
        } else {
            *aiMonHp -= playerDamage;
        }

        if (*playerMonHp <= aiDamage) {
            *playerMonHp = 0;
            return B_SIDE_OPPONENT_NZ;
        } else {
            *playerMonHp -= aiDamage;
        }

        return 0;
    }
}

static void NewAI_WriteWeightToMatrix (uint whoWon, s16 * weight, struct BattlePokemon * curAiMon, struct BattlePokemon * curPlayerMon)
{
    if (whoWon == B_SIDE_OPPONENT_NZ) {
        *weight = (curAiMon->hp * 256)/curAiMon->maxHP;
    } else if (whoWon == B_SIDE_PLAYER_NZ) {
        *weight = -(curPlayerMon->hp * 256)/curPlayerMon->maxHP;
    } else if (whoWon == B_NEITHER_SIDE_NZ) {
        *weight = 0;
    } else {
        while (1);
    }
}

static void NewAI_InvalidateBadSwitches (uint monPartyIndex, uint invalidMons, u16 * invalidActions, u8 * numActions, uint whichSide)
{
    uint i, j;

    for (i = 4; i < 9; i++) {
        uint switchIndex = i - 4;

        if (switchIndex >= monPartyIndex) {
            switchIndex++;
        }

        if (invalidMons & (1 << switchIndex)) {
            *invalidActions |= (1 << i);

            // 0x8000 = temporary magic value
            if (whichSide == B_SIDE_PLAYER) {
                for (j = 0; j < 9; j++) {
                    sAIThinkingPtr->weights[j][i] = -0x8000;
                }
            } else {
                for (j = 0; j < 9; j++) {
                    sAIThinkingPtr->weights[i][j] = -0x8000;
                }
            }
        } else {
            (*numActions)++;
        }
    }
}

static uint NewAI_RemoveInvalidLinesFromWeightsThenSolve (void)
{
    s16 matrix[81];
    s16 (* weights)[9][9];
    uint i, j, k;
    int chosenAction;
    uint invalidAiActions;
    uint curBit;
    struct BattlePokemon * playerMon;
    struct BattlePokemon * aiMon;
    const u8 * aiMonName;
    const u8 * playerMonName;
    int printBufferCursor = 0;
    //u8 * printBuffer;

    //uint oppLeftPos, playerLeftPos;

    k = 0;
    weights = &sAIThinkingPtr->weights;
    aiMon = &gBattleMons[sAIThinkingPtr->oppLeftPos];
    playerMon = &gBattleMons[sAIThinkingPtr->playerLeftPos];
    aiMonName = gSpeciesNamesAscii[aiMon->species];
    playerMonName = gSpeciesNamesAscii[playerMon->species];

    debug_printf("ai's attack vs player's attack");

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            s16 weight;
            const u8 * aiMonMoveName = gMoveNamesAscii[aiMon->moves[i]];
            const u8 * playerMonMoveName = gMoveNamesAscii[playerMon->moves[j]];
            
            weight = (*weights)[i][j];
            // ai's attack vs player's attack
            debug_printf("%s's \"%s\" vs %s's \"%s\": %d", aiMonName, aiMonMoveName, playerMonName, playerMonMoveName, weight);
        }
    }

    debug_printf("\nai's attack vs player's switch");

    for (j = 4; j < 9; j++) {
        uint playerSwitchIndex;

        playerSwitchIndex = j - 4;
        if (playerSwitchIndex >= sAIThinkingPtr->playerMonPartyIndex) {
            playerSwitchIndex++;
        }

        playerMonName = gSpeciesNamesAscii[sAIThinkingPtr->mons.side.playerMons[playerSwitchIndex].species];

        for (i = 0; i < 4; i++) {
            const u8 * aiMonMoveName;
            
            s16 weight;

            aiMonMoveName = gMoveNamesAscii[aiMon->moves[i]];
            weight = (*weights)[i][j];

            // ai's attack vs player switch
            debug_printf("%s's \"%s\" vs switch %s: %d", aiMonName, aiMonMoveName, playerMonName, weight);
        }
    }

    debug_printf("\nai's switch vs player's attack");

    playerMonName = gSpeciesNamesAscii[playerMon->species];

    for (i = 4; i < 9; i++) {
        uint aiSwitchIndex;

        aiSwitchIndex = i - 4;
        if (aiSwitchIndex >= sAIThinkingPtr->aiMonPartyIndex) {
            aiSwitchIndex++;
        }
        aiMonName = gSpeciesNamesAscii[sAIThinkingPtr->mons.side.aiMons[aiSwitchIndex].species];

        for (j = 0; j < 4; j++) {
            const u8 * aiMonMoveName;
            s16 weight;
            const u8 * playerMonMoveName;

            playerMonMoveName = gMoveNamesAscii[playerMon->moves[j]];
            weight = (*weights)[i][j];

            // ai switch vs player's attack
            debug_printf("switch %s vs %s's \"%s\": %d", aiMonName, playerMonName, playerMonMoveName, weight);
        }
    }

    debug_printf("\nai's switch vs player's switch");

    for (i = 4; i < 9; i++) {
        uint aiSwitchIndex;

        aiSwitchIndex = i - 4;
        if (aiSwitchIndex >= sAIThinkingPtr->aiMonPartyIndex) {
            aiSwitchIndex++;
        }
        aiMonName = gSpeciesNamesAscii[sAIThinkingPtr->mons.side.aiMons[aiSwitchIndex].species];

        for (j = 4; j < 9; j++) {
            uint playerSwitchIndex;
            s16 weight;

            playerSwitchIndex = j - 4;
            if (playerSwitchIndex >= sAIThinkingPtr->playerMonPartyIndex) {
                playerSwitchIndex++;
            }
            playerMonName = gSpeciesNamesAscii[sAIThinkingPtr->mons.side.playerMons[playerSwitchIndex].species];
            weight = (*weights)[i][j];

            // ai's switch vs player's switch
            debug_printf("switch %s vs switch %s: %d", aiMonName, playerMonName, weight);            
        }
    }

    for (i = 0; i < 9; i++) {
        printBufferCursor = 0;
        for (j = 0; j < 9; j++) {
            s16 weight = (*weights)[i][j];

            printBufferCursor += snprintf(gDecompressionBuffer + printBufferCursor, 0x4000 - printBufferCursor, "%04d ", weight);
            if (printBufferCursor == -1 || printBufferCursor >= 0x4000) {
                while (1);
            }

            if (weight != -0x8000) {
                matrix[k++] = weight;
            }
        }
        debug_printf("%s", gDecompressionBuffer);
    }

    chosenAction = estimate_payoff_matrix(matrix, sAIThinkingPtr->numAiActions, sAIThinkingPtr->numPlayerActions);
    invalidAiActions = sAIThinkingPtr->invalidAiActions;

    // chosenAction = 5
    // invalidAiActions = %101010000
    curBit = 0;
    // please rewrite this
    while (TRUE) {
        if ((invalidAiActions & 1) == 0) {
            chosenAction--;
        }
        invalidAiActions >>= 1;
        if (chosenAction == -1) {
            break;
        }
        curBit++;
    }

    debug_printf("action chose: %d\n", curBit);

    return curBit;
}

static void NewAI_EmitChosenAction (uint chosenAction)
{
    uint oppLeftPos = sAIThinkingPtr->oppLeftPos;
    if (chosenAction < 4) {
        const struct BattleMove * chosenMoveInfo;
        u16 chosenMove;

        chosenMove = gBattleMons[oppLeftPos].moves[chosenAction];
        chosenMoveInfo = &gBattleMoves[chosenMove];

        if (chosenMoveInfo->target & (MOVE_TARGET_USER_OR_SELECTED | MOVE_TARGET_USER)) {
            gBattlerTarget = gActiveBattler;
        } else if (chosenMoveInfo->target & MOVE_TARGET_BOTH) {
            gBattlerTarget = sAIThinkingPtr->playerLeftPos;
            if (gAbsentBattlerFlags & (1 << gBattlerTarget)) {
                gBattlerTarget = GetBattlerAtPosition(B_POSITION_PLAYER_RIGHT);
            }
        } else {
            gBattlerTarget = sAIThinkingPtr->playerLeftPos;
        }

        BtlController_EmitTwoReturnValues(1, B_ACTION_USE_MOVE, (chosenAction) | (gBattlerTarget << 8));
    } else {
        uint aiSwitchIndex;

        aiSwitchIndex = chosenAction - 4;
        if (aiSwitchIndex >= gBattlerPartyIndexes[oppLeftPos]) {
            aiSwitchIndex++;
        }

        *(gBattleStruct->monToSwitchIntoId + gActiveBattler) = aiSwitchIndex;
        BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
    }
}

//void NewAI_GetMostSuitableMonToSwitchInto




