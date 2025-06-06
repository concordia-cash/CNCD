// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_SYNC_H
#define MASTERNODE_SYNC_H

#include <atomic>

#define MASTERNODE_SYNC_INITIAL 0
#define MASTERNODE_SYNC_SPORKS 1
#define MASTERNODE_SYNC_LIST 2
#define MASTERNODE_SYNC_FAILED 998
#define MASTERNODE_SYNC_FINISHED 999

#define MASTERNODE_SYNC_TIMEOUT 10
#define MASTERNODE_SYNC_THRESHOLD 2

class CMasternodeSync;
extern CMasternodeSync masternodeSync;

//
// CMasternodeSync : Sync masternode assets in stages
//

class CMasternodeSync
{
public:
    std::map<uint256, int> mapSeenSyncMNB;

    int64_t lastMasternodeList;
    int64_t lastFailure;
    int nCountFailures;

    std::atomic<int64_t> lastProcess;
    std::atomic<bool> fBlockchainSynced;

    // sum of all counts
    int sumMasternodeList;
    // peers that reported counts
    int countMasternodeList;

    // Count peers we've requested the list from
    int RequestedMasternodeAssets;
    int RequestedMasternodeAttempt;

    // Time when current masternode asset sync started
    int64_t nAssetSyncStarted;

    CMasternodeSync();

    void AddedMasternodeList(const uint256& hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void Reset();
    void Process();
    /*
     * Process sync with a single node.
     * If it returns false, the Process() step is complete.
     * Otherwise Process() calls it again for a different node.
     */
    bool SyncWithNode(CNode* pnode, bool isRegTestNet);
    bool IsSynced();
    bool NotCompleted();
    bool IsSporkListSynced();
    bool IsMasternodeListSynced();
    bool IsBlockchainSynced();
    void ClearFulfilledRequest();
};

#endif
