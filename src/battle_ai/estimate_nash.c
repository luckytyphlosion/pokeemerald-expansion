/*
MIT License

Copyright (c) 2021 luckytyphlosion

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "global.h"
#include "random.h"
#include "mgba.h"

#define MAX_ROWS 9
#define MAX_COLS 9
#define TOTAL_TRIALS 256

/*
[-2, 0, 2],
[1, -1, -2],
[-6, 1, -1]
*/

#define debug_printf(...)

uint estimate_payoff_matrix (s16 * matrix, uint num_rows, uint num_cols)
{
    uint p1_strategy = Random() % num_rows;
    uint p1_initial_strategy = p1_strategy;
    uint p2_strategy;
    uint p1_strategies[MAX_ROWS] = {0};

    s32 p1_value_numerator = -32768;
    s32 p1_value_denominator = 1;
    uint p1_best_play_strategy_counts[MAX_ROWS];
    s32 p1_weights[MAX_COLS] = {0};
    s32 p2_weights[MAX_ROWS] = {0};
    int cur_trial, i;
    s16 * row_or_col;
    s32 p1_payoff, p2_payoff;
    uint random_value;

    for (cur_trial = 1; cur_trial < (TOTAL_TRIALS + 1); cur_trial++) {
        p1_strategies[p1_strategy] += 1;
        row_or_col = matrix + p1_strategy * num_cols;
        p2_payoff = 0x7fffffff;

        for (i = 0; i < num_cols; i++) {
            p1_weights[i] += row_or_col[i];
            if (p1_weights[i] < p2_payoff) {
                p2_payoff = p1_weights[i];
                p2_strategy = i;
            }
        }

        if (p2_payoff == 0x7fffffff) {
            while (1);
        }

        debug_printf("p2_payoff: %d, p1_value_denominator: %d, p1_value_numerator: %d, cur_trial: %d\n", p2_payoff, p1_value_denominator, p1_value_numerator, cur_trial);
        // if ((p2_payoff / cur_trial) > (p1_value.numerator / p1_value.denominator))
        if ((p2_payoff * p1_value_denominator) >= (p1_value_numerator * cur_trial)) {
            p1_value_numerator = p2_payoff;
            p1_value_denominator = cur_trial;
            debug_printf("p1_value_numerator: %d, p1_value_denominator: %d\n", p1_value_numerator, p1_value_denominator);
            CpuCopy32(p1_strategies, p1_best_play_strategy_counts, num_rows * sizeof(uint));
        }

        debug_printf("% 3d: % 4d |", cur_trial, p1_strategy + 1);
        for (i = 0; i < num_cols; i++) {
            debug_printf(" % 4d |", p1_weights[i]);
        }
        debug_printf(" % .4f\n", p2_payoff * 1.0 / cur_trial);

        row_or_col = matrix + p2_strategy;
        p1_payoff = 0x7fffffff;

        for (i = 0; i < num_rows; i++) {
            p2_weights[i] -= *row_or_col;
            row_or_col += num_cols;
            if (p2_weights[i] < p1_payoff) {
                p1_payoff = p2_weights[i];
                p1_strategy = i;
            }
        }
    }

    // remove p1's initial strategy since it could be faulty
    p1_best_play_strategy_counts[p1_initial_strategy]--;
    p1_value_denominator--;

    random_value = (Random() % p1_value_denominator) + 1;

    for (i = 0; i < num_rows; i++) {
        mgba_printf("%d/%d, ", p1_best_play_strategy_counts[i], p1_value_denominator);
    }
    mgba_printf("\n");

    for (i = 0; i < num_rows; i++) {
        if (p1_best_play_strategy_counts[i] < random_value) {
            random_value -= p1_best_play_strategy_counts[i];
        } else {
            break;
        }
    }

    debug_printf("chose: %d\n", i);

    return i;
}