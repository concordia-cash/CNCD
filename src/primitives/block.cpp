// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "script/standard.h"
#include "script/sign.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "util.h"

// TODO: Change X11KVS algorithm call to whatever the coin being adapted is used.
uint256 CBlockHeader::GetHash() const
{
     if (nVersion < 4)  { // nVersion = 1, 2, 3
#if defined(WORDS_BIGENDIAN)
        uint8_t data[80];
        WriteLE32(&data[0], nVersion);
        memcpy(&data[4], hashPrevBlock.begin(), hashPrevBlock.size());
        memcpy(&data[36], hashMerkleRoot.begin(), hashMerkleRoot.size());
        WriteLE32(&data[68], nTime);
        WriteLE32(&data[72], nBits);
        WriteLE32(&data[76], nNonce);

        return HashX11KVS(data, data + 80);
#else // Can take shortcut for little endian
        return HashX11KVS(BEGIN(nVersion), END(nNonce));
#endif
    }
	
    return SerializeHash(*this); // nVersion >= 4
}

CScript CBlock::GetPaidPayee(CAmount nAmount) const
{
    const auto& tx = vtx[IsProofOfWork() ? 0 : 1];

    for (auto it = tx.vout.rbegin(); it != tx.vout.rend(); ++it)
    {
        if (it->nValue == nAmount) return it->scriptPubKey;
    }
    
    for (auto it = tx.vout.rbegin(); it != tx.vout.rend(); ++it)
    {
        return it->scriptPubKey;
    }

    return CScript();
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    return s.str();
}

void CBlock::print() const
{
    LogPrintf("%s", ToString());
}
