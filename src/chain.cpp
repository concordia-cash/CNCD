// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "masternode.h"
#include "masternodeman.h"

/**
 * CChain implementation
 */
void CChain::SetTip(CBlockIndex* pindex)
{
    LOCK(cs);

    if (pindex == NULL) {
        vChain.clear();
        return;
    }
    vChain.resize(pindex->nHeight + 1);
    while (pindex && vChain[pindex->nHeight] != pindex) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex* pindex) const
{
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back(pindex->GetBlockHash());
        // Stop when we have added the genesis block.
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block.
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible.
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist.
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex* CChain::FindFork(const CBlockIndex* pindex) const
{
    if (pindex == nullptr)
        return nullptr;
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex::CBlockIndex(const CBlock& block):
        nVersion{block.nVersion},
        hashMerkleRoot{block.hashMerkleRoot},
        nTime{block.nTime},
        nBits{block.nBits},
        nNonce{block.nNonce}
{
    if (block.IsProofOfStake())
        SetProofOfStake();
}

std::string CBlockIndex::ToString() const
{
    return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s)",
        pprev, nHeight,
        hashMerkleRoot.ToString(),
        GetBlockHash().ToString());
}

CDiskBlockPos CBlockIndex::GetBlockPos() const
{
    CDiskBlockPos ret;
    if (nStatus & BLOCK_HAVE_DATA) {
        ret.nFile = nFile;
        ret.nPos = nDataPos;
    }
    return ret;
}

CDiskBlockPos CBlockIndex::GetUndoPos() const
{
    CDiskBlockPos ret;
    if (nStatus & BLOCK_HAVE_UNDO) {
        ret.nFile = nFile;
        ret.nPos = nUndoPos;
    }
    return ret;
}

CBlockHeader CBlockIndex::GetBlockHeader() const
{
    CBlockHeader block;
    block.nVersion = nVersion;
    if (pprev) block.hashPrevBlock = pprev->GetBlockHash();
    block.hashMerkleRoot = hashMerkleRoot;
    block.nTime = nTime;
    block.nBits = nBits;
    block.nNonce = nNonce;
    return block;
}

int64_t CBlockIndex::MaxFutureBlockTime() const
{
    return GetAdjustedTime() + Params().GetConsensus().FutureBlockTimeDrift(nHeight+1);
}

int64_t CBlockIndex::MinPastBlockTime() const
{
    const auto& params = Params(); 
    const auto& consensus = params.GetConsensus();

    // PoW: pindexPrev->MedianTimePast + 1
    if (nHeight < consensus.vUpgrades[Consensus::UPGRADE_POS].nActivationHeight)
        return GetMedianTimePast();

    // on the transition from PoW to PoS
    // pindexPrev->nTime might be in the future (up to the allowed drift)
    // so we allow the time to be at most (180-14) seconds earlier than previous block
    if (nHeight + 1 == consensus.vUpgrades[Consensus::UPGRADE_POS].nActivationHeight)
        return GetBlockTime() - consensus.FutureBlockTimeDrift(nHeight) + consensus.FutureBlockTimeDrift(nHeight + 1);

    // PoS: pindexPrev->nTime
    return GetBlockTime();
}

enum { nMedianTimeSpan = 11 };

int64_t CBlockIndex::GetMedianTimePast() const
{
    int64_t pmedian[nMedianTimeSpan];
    int64_t* pbegin = &pmedian[nMedianTimeSpan];
    int64_t* pend = &pmedian[nMedianTimeSpan];

    const CBlockIndex* pindex = this;
    for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
        *(--pbegin) = pindex->GetBlockTime();

    std::sort(pbegin, pend);
    return pbegin[(pend - pbegin) / 2];
}

// Sets V2 stake modifiers (uint256)
void CBlockIndex::SetStakeModifier(const uint256& nStakeModifier)
{
    vStakeModifier.clear();
    vStakeModifier.insert(vStakeModifier.begin(), nStakeModifier.begin(), nStakeModifier.end());
}

// Generates and sets the stake modifier
void CBlockIndex::SetNewStakeModifier(const uint256& prevoutId, const uint32_t& prevoutN)
{
    if (!pprev) throw std::runtime_error(strprintf("%s : ERROR: null pprev", __func__));

    // Generate Hash(prevoutId | prevoutN | prevModifier)
    CHashWriter ss(SER_GETHASH, 0);
    ss << prevoutId;
    ss << prevoutN;
    ss << pprev->GetStakeModifier();
    SetStakeModifier(ss.GetHash());
}

// Returns stake modifier (uint256)
uint256 CBlockIndex::GetStakeModifier() const
{
    if (vStakeModifier.empty())
        return UINT256_ZERO;
    uint256 nStakeModifier;
    std::memcpy(nStakeModifier.begin(), vStakeModifier.data(), vStakeModifier.size());
    return nStakeModifier;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);

CScript CBlockIndex::GetPaidPayee() const
{
    CBlock block;
    if (nHeight <= chainActive.Height() && ReadBlockFromDisk(block, this)) {
        auto amount = CMasternode::GetMasternodePayment(nHeight);
        auto paidPayee = block.GetPaidPayee(amount);
        return paidPayee;
    }

    return CScript();
}

//! Check whether this block index entry is valid up to the passed validity level.
bool CBlockIndex::IsValid(enum BlockStatus nUpTo) const
{
    assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
}

//! Raise the validity level of this block index entry.
//! Returns true if the validity was changed.
bool CBlockIndex::RaiseValidity(enum BlockStatus nUpTo)
{
    assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
    if (nStatus & BLOCK_FAILED_MASK)
        return false;
    if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
        nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
        return true;
    }
    return false;
}



