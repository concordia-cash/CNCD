// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMASTERNODE_H
#define ACTIVEMASTERNODE_H

#include "activemasternodeconfig.h"
#include "init.h"
#include "key.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "wallet/wallet.h"

#define ACTIVE_MASTERNODE_INITIAL 0 // initial state
#define ACTIVE_MASTERNODE_SYNC_IN_PROCESS 1
#define ACTIVE_MASTERNODE_NOT_CAPABLE 3
#define ACTIVE_MASTERNODE_STARTED 4

// Responsible for activating the Masternode and pinging the network
class CActiveMasternode
{
private:
    int status;
    std::string notCapableReason;

public:

    CActiveMasternode()
    {
        vin = nullopt;
        status = ACTIVE_MASTERNODE_INITIAL;
    }

    std::string strAlias {""};

    // Initialized by init.cpp
    // Keys for the main Masternode
    CPubKey pubKeyMasternode;

    std::string strMasterNodePrivKey {""};

    // Initialized while registering Masternode
    Optional<CTxIn> vin;
    CService service;

    /// Manage status of main Masternode
    void ManageStatus();
    void ResetStatus();
    std::string GetStatusMessage() const;
    int GetStatus() const { return status; }

    /// Enable cold wallet mode (run a Masternode with no funds)
    bool EnableHotColdMasterNode(CTxIn& vin, CService& addr);

    /// Ping Masternode
    bool SendMasternodePing(std::string& errorMessage);
};

#endif //ACTIVEMASTERNODE_H
