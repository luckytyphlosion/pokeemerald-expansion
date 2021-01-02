#include "global.h"
#include "pokemon.h"
#include "random.h"
#include "constants/battle_move_effects.h"
#include "constants/moves.h"
#include "event_data.h"

static void CreateRandomParty(struct Pokemon * party);

static const u16 sAllowedMoveEffectsOrdered[] = {EFFECT_HIT, EFFECT_SLEEP, EFFECT_POISON_HIT, EFFECT_BURN_HIT, EFFECT_FREEZE_HIT, EFFECT_PARALYZE_HIT, EFFECT_FLINCH_HIT, EFFECT_PAY_DAY, EFFECT_TRI_ATTACK, EFFECT_RECOIL_IF_MISS, EFFECT_ATTACK_DOWN_HIT, EFFECT_DEFENSE_DOWN_HIT, EFFECT_SPEED_DOWN_HIT, EFFECT_SPECIAL_ATTACK_DOWN_HIT, EFFECT_SPECIAL_DEFENSE_DOWN_HIT, EFFECT_ACCURACY_DOWN_HIT, EFFECT_EVASION_DOWN_HIT, EFFECT_CONFUSE_HIT, EFFECT_THAW_HIT, EFFECT_RAPID_SPIN, EFFECT_DEFENSE_UP_HIT, EFFECT_ATTACK_UP_HIT, EFFECT_ALL_STATS_UP_HIT, EFFECT_TWISTER, EFFECT_EARTHQUAKE, EFFECT_GUST, EFFECT_FLINCH_MINIMIZE_HIT, EFFECT_THUNDER, EFFECT_FACADE, EFFECT_SMELLINGSALT, EFFECT_BRICK_BREAK, EFFECT_KNOCK_OFF, EFFECT_SECRET_POWER, EFFECT_POISON_FANG, EFFECT_SKY_UPPERCUT, EFFECT_PLEDGE, EFFECT_WAKE_UP_SLAP, EFFECT_HEX, EFFECT_ASSURANCE, EFFECT_ACROBATICS, EFFECT_VENOSHOCK, EFFECT_PSYSHOCK, EFFECT_SPECIAL_DEFENSE_DOWN_HIT_2, EFFECT_SIMPLE_BEAM, EFFECT_FREEZE_DRY, EFFECT_HURRICANE, EFFECT_TWO_TYPED_MOVE, EFFECT_SPEED_UP_HIT, EFFECT_SCALD, EFFECT_FLINCH_STATUS, EFFECT_SMACK_DOWN, EFFECT_FLAME_BURST, EFFECT_SP_ATTACK_UP_HIT, EFFECT_INCINERATE, EFFECT_BUG_BITE, 0xffff};

static EWRAM_DATA u32 sSavedRng3Value = 0;

// https://www.geeksforgeeks.org/binary-search/
static int BinarySearch (const u16 * arr, uint l, uint r, u16 x)
{
    while (l <= r) { 
        uint m = l + (r - l) / 2; 

        // Check if x is present at mid 
        if (arr[m] == x) 
            return m; 

        // If x greater, ignore left half 
        if (arr[m] < x) 
            l = m + 1; 

        // If x is smaller, ignore right half 
        else
            r = m - 1;
    }

    // if we reach here, then element was 
    // not present 
    return -1; 
}

static int IsInArray (const u16 * arr, u16 moveEffect)
{
    while (*arr != 0xffff) {
        if (*arr == moveEffect) {
            return TRUE;
        }
        arr++;
    }

    return FALSE;
}

void GivePlayerRandomParty (void)
{
    ZeroPlayerPartyMons();
    CreateRandomParty(gPlayerParty);
}

void GiveEnemyRandomParty (void)
{
    ZeroEnemyPartyMons();
    if (gSpecialVar_0x8005 == 0) {
        CpuCopy32(gPlayerParty, gEnemyParty, sizeof(gPlayerParty));
    } else {
        if (gSpecialVar_0x8005 == 1) {
            SeedRng3(sSavedRng3Value);
        }
        sSavedRng3Value = GetRng3Seed();
        CreateRandomParty(gEnemyParty);
    }
}

static void CreateRandomParty (struct Pokemon * party)
{
    uint dexNum;
    uint species;
    uint i, j;
    struct Pokemon * curMon;

    for (i = 0; i < PARTY_SIZE; i++) {
        dexNum = (Random3() % NATIONAL_DEX_COUNT) + 1;
        species = NationalPokedexNumToSpecies(dexNum);
        curMon = &party[i];

        CreateMon(
            curMon,
            species,
            50,
            32, FALSE, 0, OT_ID_PLAYER_ID, 0
        );

        for (j = 0; j < MAX_MON_MOVES; j++) {
            u16 randomMove;
            
            while (TRUE) {
                const struct BattleMove * moveInfo;

                randomMove = (Random3() % MOVES_COUNT_ACTUAL) + 1;
                moveInfo = &gBattleMoves[randomMove];
                if (moveInfo->power > 1) {
                    u16 moveEffect = moveInfo->effect;

                    if (BinarySearch(sAllowedMoveEffectsOrdered, 0, ARRAY_COUNT(sAllowedMoveEffectsOrdered) - 1, moveEffect) != -1) {
                    //if (IsInArray(sAllowedMoveEffectsOrdered, moveEffect)) {
                        if (moveInfo->secondaryEffectChance < 50) {
                            break;
                        }
                    }
                }
            }

            SetMonMoveSlot(curMon, randomMove, j);
        }
    }
}
