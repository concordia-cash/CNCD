// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "main.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

#include <math.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pIndexLast)
{
    // Retrieve the parameters and consensus rules
    const auto& params = Params();
    const auto& consensus = params.GetConsensus();

    if (params.IsRegTestNet()) return pIndexLast->nBits;

    // Get the current block height
    const auto nPrevHeight = pIndexLast->nHeight;
    const auto nHeight = nPrevHeight + 1;

    // Get additional parameters
    const auto powLimit = consensus.powLimit;
 
    // Fetch the target block spacing time and timespan
    int64_t nTargetSpacing = consensus.nTargetSpacing;

    const int nTargetBlocksPerDay = DAY_IN_SECONDS / nTargetSpacing;
    const int nTargetBlocksPerWeek = WEEK_IN_SECONDS / nTargetSpacing;
    const int nTargetBlocksPerMonth = MONTH_IN_SECONDS / nTargetSpacing;

    // Calculates the actual spacing using the previous block and the block before it
    int64_t nActualSpacing = nHeight > 1 ? pIndexLast->GetBlockTime() - pIndexLast->pprev->GetBlockTime() : nTargetSpacing;

    int64_t nDayAccumulatedTargetSpacing = DAY_IN_SECONDS;
    int64_t nDayAccumulatedSpacing = nHeight > nTargetBlocksPerDay ?
        pIndexLast->GetBlockTime() - chainActive[nPrevHeight - nTargetBlocksPerDay]->GetBlockTime() :
        nDayAccumulatedTargetSpacing;

    int64_t nWeekAccumulatedTargetSpacing = WEEK_IN_SECONDS;
    int64_t nWeekAccumulatedSpacing = nHeight > nTargetBlocksPerWeek ?
        pIndexLast->GetBlockTime() - chainActive[nPrevHeight - nTargetBlocksPerWeek]->GetBlockTime() :
        nWeekAccumulatedTargetSpacing;

    int64_t nBiWeekAccumulatedTargetSpacing = 2 * WEEK_IN_SECONDS;
    int64_t nBiWeekAccumulatedSpacing = nHeight > 2 * nTargetBlocksPerWeek ?
        pIndexLast->GetBlockTime() - chainActive[nPrevHeight - 2 * nTargetBlocksPerWeek]->GetBlockTime() :
        nBiWeekAccumulatedTargetSpacing;

    int64_t nMonthAccumulatedTargetSpacing = MONTH_IN_SECONDS;
    int64_t nMonthAccumulatedSpacing = nHeight > nTargetBlocksPerMonth ?
        pIndexLast->GetBlockTime() - chainActive[nPrevHeight - nTargetBlocksPerMonth]->GetBlockTime() :
        nMonthAccumulatedTargetSpacing;

    int64_t nMultiplier = 1000; // increase the adjustemnt resolution to the millisecond level

    nActualSpacing = nActualSpacing * nMultiplier;

    // 24h target adjustment
    int64_t nDayTargetSpacing = (nDayAccumulatedTargetSpacing * nTargetSpacing * nMultiplier) / nDayAccumulatedSpacing; // ^1
    nDayTargetSpacing = nDayAccumulatedTargetSpacing * nDayTargetSpacing / nDayAccumulatedSpacing;                      // ^2
    nDayTargetSpacing = nDayAccumulatedTargetSpacing * nDayTargetSpacing / nDayAccumulatedSpacing;                      // ^3
    nDayTargetSpacing = nDayAccumulatedTargetSpacing * nDayTargetSpacing / nDayAccumulatedSpacing;                      // ^4

    // 7 days target adjustment
    int64_t nWeekTargetSpacing = (nWeekAccumulatedTargetSpacing * nTargetSpacing * nMultiplier) / nWeekAccumulatedSpacing;  // ^1
    nWeekTargetSpacing = nWeekAccumulatedTargetSpacing * nWeekTargetSpacing / nWeekAccumulatedSpacing;                      // ^2
    nWeekTargetSpacing = nWeekAccumulatedTargetSpacing * nWeekTargetSpacing / nWeekAccumulatedSpacing;                      // ^3
    nWeekTargetSpacing = nWeekAccumulatedTargetSpacing * nWeekTargetSpacing / nWeekAccumulatedSpacing;                      // ^4

    // 14 days target adjustment
    int64_t nBiWeekTargetSpacing = (nBiWeekAccumulatedTargetSpacing * nTargetSpacing * nMultiplier) / nBiWeekAccumulatedSpacing;    // ^1
    nBiWeekTargetSpacing = nBiWeekAccumulatedTargetSpacing * nBiWeekTargetSpacing / nBiWeekAccumulatedSpacing;                      // ^2
    nBiWeekTargetSpacing = nBiWeekAccumulatedTargetSpacing * nBiWeekTargetSpacing / nBiWeekAccumulatedSpacing;                      // ^3
    nBiWeekTargetSpacing = nBiWeekAccumulatedTargetSpacing * nBiWeekTargetSpacing / nBiWeekAccumulatedSpacing;                      // ^4

    // 30 days target adjustment
    int64_t nMonthTargetSpacing = (nMonthAccumulatedTargetSpacing * nTargetSpacing * nMultiplier) / nMonthAccumulatedSpacing;  // ^1
    nMonthTargetSpacing = nMonthAccumulatedTargetSpacing * nMonthTargetSpacing / nMonthAccumulatedSpacing;                      // ^2
    nMonthTargetSpacing = nMonthAccumulatedTargetSpacing * nMonthTargetSpacing / nMonthAccumulatedSpacing;                      // ^3
    nMonthTargetSpacing = nMonthAccumulatedTargetSpacing * nMonthTargetSpacing / nMonthAccumulatedSpacing;                      // ^4

    int64_t nK = 60; // adjustments in 1/60th steps from the target

    int64_t nKd  = 60; // 60/40 rule from level to level
    int64_t nKw  = 24; // 40 * 60%
    int64_t nK2w = 10; // 16 * 60% rounded
    int64_t nKm  =  6; // remainder

    int64_t nFinalTargetSpacing = 
        (
            (nDayTargetSpacing * nKd) + 
            (nWeekTargetSpacing * nKw) + 
            (nBiWeekTargetSpacing * nK2w) + 
            (nMonthTargetSpacing * nKm)
        ) / 100;
    int64_t nMaxTargetSpacing = (nTargetSpacing * nMultiplier * 110) / 100;
    int64_t nMinTargetSpacing = (nTargetSpacing * nMultiplier * 90) / 100;

    // limit nFinalTargetSpacing to +-10% of the nTargetSpacing
    nFinalTargetSpacing = std::min(nFinalTargetSpacing, nMaxTargetSpacing);
    nFinalTargetSpacing = std::max(nFinalTargetSpacing, nMinTargetSpacing);

    uint256 bnNew;
    bnNew.SetCompact(pIndexLast->nBits);

    bnNew *= nActualSpacing + ((nK - 1) * nFinalTargetSpacing);
    bnNew /= nK * nFinalTargetSpacing;

    // Ensure the new difficulty does not exceed the minimum allowed by consensus
    if (bnNew > powLimit)
        bnNew = powLimit;

    // Return the new difficulty in compact format
    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits)
{
    bool fNegative;
    bool fOverflow;
    uint256 bnTarget;

    if (Params().IsRegTestNet()) return true;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget.IsNull() || fOverflow || bnTarget > Params().GetConsensus().powLimit)
        return error("CheckProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget)
        return error("CheckProofOfWork() : hash doesn't match nBits");

    return true;
}

uint256 GetBlockProof(const CBlockIndex& block)
{
    uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget.IsNull())
        return UINT256_ZERO;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}
