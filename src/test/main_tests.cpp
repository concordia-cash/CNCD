// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/sign.h"
#include "test_pivx.h"
#include <boost/test/unit_test.hpp>
#include "masternode.h"
#include "rewards.h"

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

enum BlockSignatureType{
    P2PK,
    P2PKH
};

CScript GetScriptForType(CPubKey pubKey, BlockSignatureType type)
{
    switch(type){
        case P2PK:
            return CScript() << pubKey << OP_CHECKSIG;
        default:
            return GetScriptForDestination(pubKey.GetID());
    }
}

std::vector<unsigned char> CreateDummyScriptSigWithKey(CPubKey pubKey)
{
    std::vector<unsigned char> vchSig;
    const CScript scriptCode;
    DummySignatureCreator(nullptr).CreateSig(vchSig, pubKey.GetID(), scriptCode, SIGVERSION_BASE);
    return vchSig;
}

CScript GetDummyScriptSigByType(CPubKey pubKey, bool isP2PK)
{
    CScript script = CScript() << CreateDummyScriptSigWithKey(pubKey);
    if (!isP2PK)
        script << ToByteVector(pubKey);
    return script;
}

CBlock CreateDummyBlockWithSignature(CKey stakingKey, BlockSignatureType type, bool useInputP2PK)
{
    CMutableTransaction txCoinStake;
    // Dummy input
    CTxIn input(uint256(), 0);
    // P2PKH input
    input.scriptSig = GetDummyScriptSigByType(stakingKey.GetPubKey(), useInputP2PK);
    // Add dummy input
    txCoinStake.vin.emplace_back(input);
    // Empty first output
    txCoinStake.vout.emplace_back(CTxOut(0, CScript()));
    // P2PK staking output
    CScript scriptPubKey = GetScriptForType(stakingKey.GetPubKey(), type);
    txCoinStake.vout.emplace_back(CTxOut(0, scriptPubKey));

    // Now the block.
    CBlock block;
    block.vtx.emplace_back(CTransaction()); // dummy first tx
    block.vtx.emplace_back(txCoinStake);
    SignBlockWithKey(block, stakingKey);

    return block;
}

bool TestBlockSignaturePreEnforcementV5(const CBlock& block)
{
    return CheckBlockSignature(block, false);
}

bool TestBlockSignaturePostEnforcementV5(const CBlock& block)
{
    return CheckBlockSignature(block, true);
}

BOOST_AUTO_TEST_CASE(block_signature_test)
{
    for (int i = 0; i < 20; ++i) {
        CKey stakingKey;
        stakingKey.MakeNewKey(true);
        bool useInputP2PK = i % 2 == 0;

        // Test P2PK block signature pre enforcement.
        CBlock block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PK, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePreEnforcementV5(block));

        // Test P2PK block signature post enforcement
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PK, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));

        // Test P2PKH block signature pre enforcement ---> must fail.
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PKH, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePreEnforcementV5(block));

        // Test P2PKH block signature post enforcement
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PKH, useInputP2PK);
        if (useInputP2PK) {
            // If it's using a P2PK scriptsig as input and a P2PKH output
            // The block doesn't contain the public key to verify the sig anywhere.
            // Must fail.
            BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));
        } else {
            BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));
        }
    }
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight <= 1; nHeight += 1) {
        /* premine in block 1 (30000000 CNCD) */
        CAmount nSubsidy = CRewards::GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy <= 30000000 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 2; nHeight <= 100000; nHeight += 1) {
        /* PoW Phase One */
        CAmount nSubsidy = CRewards::GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy == 100 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 100001; nHeight <= 200000; nHeight += 1) {
        /* PoW Phase Two */
        CAmount nSubsidy = CRewards::GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy == 125 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 200001; nHeight <= 300000; nHeight += 1) {
        /* PoW Phase Two */
        CAmount nSubsidy = CRewards::GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy == 150 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 300001; nHeight <= 400000; nHeight += 1) {
        /* PoW Phase Two */
        CAmount nSubsidy = CRewards::GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy == 125 * COIN);
        nSum += nSubsidy;
    }

    //TODO: If you have a limited supply, use MaxSupply check here.
    // printf("\n\r ----------- nSum = %li ---------------- \n\r", nSum);
    // BOOST_CHECK(uint8_t(nSum) == uint8_t(4109975100000000ULL));
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool(), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}

BOOST_AUTO_TEST_SUITE_END()
