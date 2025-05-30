// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2025 The Concordia Cash Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletmodel.h"

#include "addresstablemodel.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "recentrequeststablemodel.h"
#include "transactiontablemodel.h"

#include "base58.h"
#include "db.h"
#include "keystore.h"
#include "main.h"
#include "spork.h"
#include "sync.h"
#include "guiinterface.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h" // for BackupWallet
#include <stdint.h>
#include <iostream>

#include <QDebug>
#include <QSet>
#include <QTimer>
#include <masternode.h>

using namespace boost::placeholders;

WalletModel::WalletModel(CWallet* wallet, OptionsModel* optionsModel, QObject* parent) : QObject(parent), wallet(wallet), walletWrapper(*wallet),
                                                                                         optionsModel(optionsModel),
                                                                                         cachedEncryptionStatus(Unencrypted),
                                                                                         cachedNumBlocks(0)
{
    fHaveWatchOnly = wallet->HaveWatchOnly();
    fForceCheckBalanceChanged = false;

    addressTableModel = new AddressTableModel(wallet, this);
    transactionTableModel = new TransactionTableModel(wallet, this);
    recentRequestsTableModel = new RecentRequestsTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &WalletModel::pollBalanceChanged);
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

bool WalletModel::isTestNetwork() const
{
    return Params().NetworkID() == CBaseChainParams::TESTNET;
}

bool WalletModel::isRegTestNetwork() const
{
    return Params().IsRegTestNet();
}

bool WalletModel::isStakingStatusActive() const
{
    return wallet->pStakerStatus->IsActive();
}

bool WalletModel::isHDEnabled() const
{
    return wallet->IsHDEnabled();
}

bool WalletModel::upgradeWallet(std::string& upgradeError)
{
    WalletFeature features = FEATURE_PRE_SPLIT_KEYPOOL;

    // This action must be performed in a separate thread and not the main one.
    LOCK2(cs_main, wallet->cs_wallet);

    // Get version
    int prev_version = wallet->GetVersion();
    // Upgrade wallet's version
    wallet->SetMinVersion(features);
    wallet->SetMaxVersion(features);

    // Upgrade to HD
    return wallet->Upgrade(upgradeError, prev_version);
}

CAmount WalletModel::getBalance(const CCoinControl* coinControl, bool fUnlockedOnly) const
{
    if (coinControl) {
        CAmount nBalance = 0;
        std::vector<COutput> vCoins;
        wallet->AvailableCoins(&vCoins, coinControl);
        for (const COutput& out : vCoins) {
            bool fSkip = fUnlockedOnly && isLockedCoin(out.tx->GetHash(), out.i);
            if (out.fSpendable && !fSkip)
                nBalance += out.tx->vout[out.i].nValue;
        }

        return nBalance;
    }

    return wallet->GetAvailableBalance() - (fUnlockedOnly ? wallet->GetLockedCoins() : CAmount(0));
}

CAmount WalletModel::getUnlockedBalance(const CCoinControl* coinControl) const
{
    return getBalance(coinControl, true);
}

CAmount WalletModel::getLockedBalance() const
{
    return wallet->GetLockedCoins();
}

bool WalletModel::haveWatchOnly() const
{
    return fHaveWatchOnly;
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if (cachedEncryptionStatus != newEncryptionStatus)
        Q_EMIT encryptionStatusChanged(newEncryptionStatus);
}

bool WalletModel::isWalletUnlocked() const
{
    EncryptionStatus status = getEncryptionStatus();
    return (status == Unencrypted || status == Unlocked);
}

bool WalletModel::isWalletLocked(bool fFullUnlocked) const
{
    EncryptionStatus status = getEncryptionStatus();
    return (status == Locked || (!fFullUnlocked && status == UnlockedForStaking));
}

bool IsImportingOrReindexing()
{
    return fImporting || fReindex;
}

void WalletModel::pollBalanceChanged()
{
    // Wait a little bit more when the wallet is reindexing and/or importing, no need to lock cs_main so often.
    if (IsImportingOrReindexing()) {
        static uint8_t waitLonger = 0;
        waitLonger++;
        if (waitLonger < 10) // 10 seconds
            return;
        waitLonger = 0;
    }

    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if (!lockWallet)
        return;

    // Don't continue processing if the chain tip time is less than the first
    // key creation time as there is no need to iterate over the transaction
    // table model in this case.
    auto tip = chainActive.Tip();
    if (tip->GetBlockTime() < getCreationTime())
        return;

    int chainHeight = tip->nHeight;
    if (fForceCheckBalanceChanged || chainHeight != cachedNumBlocks) {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        cachedNumBlocks = chainHeight;

        checkBalanceChanged(walletWrapper.getBalances());
        if (transactionTableModel) {
            transactionTableModel->updateConfirmations();
        }

        // Address in receive tab may have been used
        Q_EMIT notifyReceiveAddressChanged();
    }
}

void WalletModel::emitBalanceChanged()
{
    // Force update of UI elements even when no values have changed
   Q_EMIT balanceChanged(walletWrapper.getBalances());
}

void WalletModel::checkBalanceChanged(const interfaces::WalletBalances& newBalance)
{
    if (newBalance.balanceChanged(m_cached_balances)) {
        m_cached_balances = newBalance;
        Q_EMIT balanceChanged(m_cached_balances);
    }
}

void WalletModel::setWalletDefaultFee(CAmount fee)
{
    payTxFee = CFeeRate(fee);
}

bool WalletModel::hasWalletCustomFee()
{
    if (!optionsModel) return false;
    return optionsModel->data(optionsModel->index(OptionsModel::fUseCustomFee), Qt::EditRole).toBool();
}

bool WalletModel::getWalletCustomFee(CAmount& nFeeRet)
{
    nFeeRet = static_cast<CAmount>(optionsModel->data(optionsModel->index(OptionsModel::nCustomFee), Qt::EditRole).toLongLong());
    return hasWalletCustomFee();
}

void WalletModel::setWalletCustomFee(bool fUseCustomFee, const CAmount& nFee)
{
    if (!optionsModel) return;
    optionsModel->setData(optionsModel->index(OptionsModel::fUseCustomFee), fUseCustomFee);
    // do not update custom fee value when fUseCustomFee is set to false
    if (fUseCustomFee) {
        optionsModel->setData(optionsModel->index(OptionsModel::nCustomFee), static_cast<qlonglong>(nFee));
    }
}

void WalletModel::updateTransaction()
{
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString& address, const QString& label, bool isMine, const QString& purpose, int status)
{
    try {
        if (addressTableModel)
            addressTableModel->updateEntry(address, label, isMine, purpose, status);
    } catch (...) {
        std::cout << "Exception updateAddressBook" << std::endl;
    }
}

void WalletModel::updateWatchOnlyFlag(bool fHaveWatchonly)
{
    fHaveWatchOnly = fHaveWatchonly;
    Q_EMIT notifyWatchonlyChanged(fHaveWatchonly);
}

bool WalletModel::validateAddress(const QString& address)
{
    // Only regular base58 addresses accepted here
    return IsValidDestinationString(address.toStdString());
}

bool WalletModel::updateAddressBookLabels(const CTxDestination& dest, const std::string& strName, const std::string& strPurpose)
{
    LOCK(wallet->cs_wallet);

    std::map<CTxDestination, AddressBook::CAddressBookData>::iterator mi = wallet->mapAddressBook.find(dest);

    // Check if we have a new address or an updated label
    if (mi == wallet->mapAddressBook.end()) {
        return wallet->SetAddressBook(dest, strName, strPurpose);
    } else if (mi->second.name != strName) {
        return wallet->SetAddressBook(dest, strName, ""); // "" means don't change purpose
    }
    return false;
}

WalletModel::SendCoinsReturn WalletModel::prepareTransaction(WalletModelTransaction& transaction, const CCoinControl* coinControl)
{
    CAmount total = 0;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<CRecipient> vecSend;

    if (recipients.empty()) {
        return OK;
    }

    if (isStakingOnlyUnlocked()) {
        return StakingOnlyUnlocked;
    }

    QSet<QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;

    // Pre-check input data for validity
    Q_FOREACH (const SendCoinsRecipient& rcp, recipients) {
        // User-entered CNCD address / amount:
        if (!validateAddress(rcp.address)) {
            return InvalidAddress;
        }
        if (rcp.amount <= 0) {
            return InvalidAmount;
        }
        setAddress.insert(rcp.address);
        ++nAddresses;

        CScript scriptPubKey;
        CTxDestination out = DecodeDestination(rcp.address.toStdString());

        // Regular P2PK or P2PKH
        scriptPubKey = GetScriptForDestination(out);

        vecSend.push_back(CRecipient{scriptPubKey, rcp.amount, false});

        total += rcp.amount;
    }
    
    if (setAddress.size() != nAddresses) {
        return DuplicateAddress;
    }

    CAmount nSpendableBalance = getUnlockedBalance(coinControl);

    if (total > nSpendableBalance) {
        return AmountExceedsBalance;
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        transaction.newPossibleKeyChange(wallet);
        CAmount nFeeRequired = 0;
        int nChangePosInOut = -1;
        std::string strFailReason;

        CWalletTx* newTx = transaction.getTransaction();
        CReserveKey* keyChange = transaction.getPossibleKeyChange();

        bool fCreated = wallet->CreateTransaction(vecSend,
                                                  *newTx,
                                                  *keyChange,
                                                  nFeeRequired,
                                                  nChangePosInOut,
                                                  strFailReason,
                                                  coinControl,
                                                  recipients[0].inputType,
                                                  true,
                                                  0);
        transaction.setTransactionFee(nFeeRequired);

        if (!fCreated) {
            if ((total + nFeeRequired) > nSpendableBalance) {
                return SendCoinsReturn(AmountWithFeeExceedsBalance);
            }

            Q_EMIT message(tr("Send Coins"), tr("Transaction creation failed!\n%1").arg(
                    strFailReason == "Transaction too large" ?
                            tr("The size of the transaction is too big.\nSelect fewer inputs with coin control.") :
                            QString::fromStdString(strFailReason)),
                    CClientUIInterface::MSG_ERROR);
            return TransactionCreationFailed;
        }

        // reject insane fee
        if (nFeeRequired > ::minRelayTxFee.GetFee(transaction.getTransactionSize()) * 10000)
            return InsaneFee;
    }

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(WalletModelTransaction& transaction)
{
    QByteArray transaction_array; /* store serialized transaction */

    if (isStakingOnlyUnlocked()) {
        return StakingOnlyUnlocked;
    }

    // Double check tx before do anything
    CValidationState state;
    if (!CheckTransaction(*transaction.getTransaction(), state)) {
        return TransactionCheckFailed;
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);
        CWalletTx* newTx = transaction.getTransaction();
        /*
        QList<SendCoinsRecipient> recipients = transaction.getRecipients();

        // Store PaymentRequests in wtx.vOrderForm in wallet.
        Q_FOREACH (const SendCoinsRecipient& rcp, recipients) {
            if (rcp.paymentRequest.IsInitialized()) {
                std::string key("PaymentRequest");
                std::string value;
                rcp.paymentRequest.SerializeToString(&value);
                newTx->vOrderForm.push_back(std::make_pair(key, value));
            } else if (!rcp.message.isEmpty()) // Message from normal concordia:URI (concordia:XyZ...?message=example)
            {
                newTx->vOrderForm.push_back(std::make_pair("Message", rcp.message.toStdString()));
            }
        }
        */
        CReserveKey* keyChange = transaction.getPossibleKeyChange();
        const CWallet::CommitResult& res = wallet->CommitTransaction(*newTx, *keyChange, g_connman.get());
        if (res.status != CWallet::CommitStatus::OK) {
            return SendCoinsReturn(res);
        }

        CTransaction* t = (CTransaction*)newTx;
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << *t;
        transaction_array.append(&(ssTx[0]), ssTx.size());
    }

    // Add addresses / update labels that we've sent to to the address book,
    // and emit coinsSent signal for each recipient
    Q_FOREACH (const SendCoinsRecipient& rcp, transaction.getRecipients()) {
        // Don't touch the address book when we have a payment request
        //if (!rcp.paymentRequest.IsInitialized()) {
            CTxDestination address = DecodeDestination(rcp.address.toStdString());
            std::string purpose = AddressBook::AddressBookPurpose::SEND;
            std::string strLabel = rcp.label.toStdString();
            updateAddressBookLabels(address, strLabel, purpose);
        //}
        Q_EMIT coinsSent(wallet, rcp, transaction_array);
    }
    emitBalanceChanged(); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK);
}

const CWalletTx* WalletModel::getTx(uint256 id)
{
    return wallet->GetWalletTx(id);
}

OptionsModel* WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel* WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel* WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

RecentRequestsTableModel* WalletModel::getRecentRequestsTableModel()
{
    return recentRequestsTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if (!wallet->IsCrypted()) {
        return Unencrypted;
    } else if (wallet->fWalletUnlockStaking) {
        return UnlockedForStaking;
    } else if (wallet->IsLocked()) {
        return Locked;
    } else {
        return Unlocked;
    }

}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString& passphrase)
{
    if (encrypted) {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    } else {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString& passPhrase, bool stakingOnly)
{
    if (locked) {
        // Lock
        wallet->fWalletUnlockStaking = false;
        return wallet->Lock();
    } else {
        // Unlock
        return wallet->Unlock(passPhrase, stakingOnly);
    }
}

bool WalletModel::lockForStakingOnly(const SecureString& passPhrase)
{
    if (!wallet->IsLocked()) {
        wallet->fWalletUnlockStaking = true;
        return true;
    } else {
        setWalletLocked(false, passPhrase, true);
    }
    return false;
}

bool WalletModel::isStakingOnlyUnlocked()
{
    return wallet->fWalletUnlockStaking;
}

bool WalletModel::changePassphrase(const SecureString& oldPass, const SecureString& newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString& filename)
{
    //attempt regular backup
    if (!BackupWallet(*wallet, filename.toLocal8Bit().data())) {
        return error("ERROR: Failed to backup wallet!");
    }

    return true;
}


// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel* walletmodel, CCryptoKeyStore* wallet)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel* walletmodel, CWallet* wallet, const CTxDestination& address, const std::string& label, bool isMine, const std::string& purpose, ChangeType status)
{
    QString strAddress = QString::fromStdString(pwalletMain->ParseIntoAddress(address, purpose));
    QString strLabel = QString::fromStdString(label);
    QString strPurpose = QString::fromStdString(purpose);

    qDebug() << "NotifyAddressBookChanged : " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " purpose=" + strPurpose + " status=" + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
        Q_ARG(QString, strAddress),
        Q_ARG(QString, strLabel),
        Q_ARG(bool, isMine),
        Q_ARG(QString, strPurpose),
        Q_ARG(int, status));
}

// queue notifications to show a non freezing progress dialog e.g. for rescan
static bool fQueueNotifications = false;
static std::vector<std::pair<uint256, ChangeType> > vQueueNotifications;
static void NotifyTransactionChanged(WalletModel* walletmodel, CWallet* wallet, const uint256& hash, ChangeType status)
{
    if (fQueueNotifications) {
        vQueueNotifications.push_back(std::make_pair(hash, status));
        return;
    }

    QString strHash = QString::fromStdString(hash.GetHex());

    qDebug() << "NotifyTransactionChanged : " + strHash + " status= " + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection /*,
                              Q_ARG(QString, strHash),
                              Q_ARG(int, status)*/);
}

static void ShowProgress(WalletModel* walletmodel, const std::string& title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(walletmodel, "showProgress", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(title)),
        Q_ARG(int, nProgress));
}

static void NotifyWatchonlyChanged(WalletModel* walletmodel, bool fHaveWatchonly)
{
    QMetaObject::invokeMethod(walletmodel, "updateWatchOnlyFlag", Qt::QueuedConnection,
        Q_ARG(bool, fHaveWatchonly));
}

static void NotifyWalletBacked(WalletModel* model, const bool& fSuccess, const std::string& filename)
{
    std::string message;
    std::string title = "Backup ";
    CClientUIInterface::MessageBoxFlags method;

    if (fSuccess) {
        message = "The wallet data was successfully saved to ";
        title += "Successful: ";
        method = CClientUIInterface::MessageBoxFlags::MSG_INFORMATION;
    } else {
        message = "There was an error trying to save the wallet data to ";
        title += "Failed: ";
        method = CClientUIInterface::MessageBoxFlags::MSG_ERROR;
    }

    message += _(filename.data());

    QMetaObject::invokeMethod(model, "message", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(QString, QString::fromStdString(message)),
                              Q_ARG(unsigned int, (unsigned int)method));
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyStatusChanged.connect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    wallet->NotifyWatchonlyChanged.connect(boost::bind(NotifyWatchonlyChanged, this, _1));
    wallet->NotifyWalletBacked.connect(boost::bind(NotifyWalletBacked, this, _1, _2));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyStatusChanged.disconnect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    wallet->NotifyWatchonlyChanged.disconnect(boost::bind(NotifyWatchonlyChanged, this, _1));
    wallet->NotifyWalletBacked.disconnect(boost::bind(NotifyWalletBacked, this, _1, _2));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    const WalletModel::EncryptionStatus status_before = getEncryptionStatus();
    if (status_before == Locked || status_before == UnlockedForStaking)
    {
        // Request UI to unlock wallet
        Q_EMIT requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = isWalletUnlocked();

    return UnlockContext(this, valid, status_before);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *_wallet, bool _valid, const WalletModel::EncryptionStatus& status_before):
        wallet(_wallet),
        valid(_valid),
        was_status(status_before),
        relock(status_before == Locked || status_before == UnlockedForStaking)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if (valid && relock && wallet) {
        if (was_status == Locked) wallet->setWalletLocked(true);
        else if (was_status == UnlockedForStaking) wallet->lockForStakingOnly();
        wallet->updateStatus();
    }
}

void WalletModel::UnlockContext::CopyFrom(UnlockContext&& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID& address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);
}

int64_t WalletModel::getCreationTime() const {
    return wallet->nTimeFirstKey;
}

int64_t WalletModel::getKeyCreationTime(const CPubKey& key)
{
    return pwalletMain->GetKeyCreationTime(key);
}

int64_t WalletModel::getKeyCreationTime(const CTxDestination& address)
{
    if (this->isMine(address)) {
        return pwalletMain->GetKeyCreationTime(address);
    }
    return 0;
}

PairResult WalletModel::getNewAddress(Destination& ret, std::string label) const
{
    CTxDestination dest;
    PairResult res = wallet->getNewAddress(dest, label);
    if (res.result) ret = Destination(dest);
    return res;
}

bool WalletModel::updateAddressBookPurpose(const QString &addressStr, const std::string& purpose)
{
    CTxDestination address = DecodeDestination(addressStr.toStdString());
    CKeyID keyID;
    if (!getKeyId(address, keyID))
        return false;
    return pwalletMain->SetAddressBook(keyID, getLabelForAddress(address), purpose);
}

bool WalletModel::getKeyId(const CTxDestination& address, CKeyID& keyID)
{
    if (!IsValidDestination(address))
        return error("Invalid CNCD address");

    const CKeyID* inKeyID = boost::get<CKeyID>(&address);
    if (!inKeyID)
        return error("Unable to get KeyID from CNCD address");

    keyID = *inKeyID;
    return true;
}

std::string WalletModel::getLabelForAddress(const CTxDestination& address)
{
    std::string label = "";
    {
        LOCK(wallet->cs_wallet);
        std::map<CTxDestination, AddressBook::CAddressBookData>::iterator mi = wallet->mapAddressBook.find(address);
        if (mi != wallet->mapAddressBook.end()) {
            label = mi->second.name;
        }
    }
    return label;
}

// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    for (const COutPoint& outpoint : vOutpoints) {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        bool fConflicted;
        const int nDepth = wallet->mapWallet[outpoint.hash].GetDepthAndMempool(fConflicted);
        if (nDepth < 0 || fConflicted) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true, true);
        vOutputs.push_back(out);
    }
}

// returns a COutPoint of collateral amount if found
bool WalletModel::getMNCollateralCandidate(COutPoint& outPoint)
{
    const auto nCollateralValue = CMasternode::GetNextWeekMasternodeCollateral();
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(&vCoins, nullptr, ONLY_10000);
    for (const COutput& out : vCoins) {
        // skip locked collaterals
        if (!isLockedCoin(out.tx->GetHash(), out.i) && out.Value() == nCollateralValue) {
            outPoint = COutPoint(out.tx->GetHash(), out.i);
            return true;
        }
    }
    return false;
}

bool WalletModel::isSpent(const COutPoint& outpoint) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsSpent(outpoint.hash, outpoint.n);
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address)
void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
{
    std::vector<COutput> vCoins;
    wallet->AvailableCoins(&vCoins);

    LOCK2(cs_main, wallet->cs_wallet); // ListLockedCoins, mapWallet
    std::vector<COutPoint> vLockedCoins;
    wallet->ListLockedCoins(vLockedCoins);

    // add locked coins
    for (const COutPoint& outpoint : vLockedCoins) {
        if (!wallet->mapWallet.count(outpoint.hash)) continue;
        bool fConflicted;
        int nDepth = wallet->mapWallet[outpoint.hash].GetDepthAndMempool(fConflicted);
        if (nDepth < 0 || fConflicted) continue;
        COutput out(&wallet->mapWallet[outpoint.hash], outpoint.n, nDepth, true, true);
        if (outpoint.n < out.tx->vout.size() &&
                (wallet->IsMine(out.tx->vout[outpoint.n]) & ISMINE_SPENDABLE_ALL) != ISMINE_NO)
            vCoins.push_back(out);
    }

    for (const COutput& out : vCoins) {
        COutput cout = out;

        while (wallet->IsChange(cout.tx->vout[cout.i]) && cout.tx->vin.size() > 0 && wallet->IsMine(cout.tx->vin[0])) {
            if (!wallet->mapWallet.count(cout.tx->vin[0].prevout.hash)) break;
            cout = COutput(&wallet->mapWallet[cout.tx->vin[0].prevout.hash], cout.tx->vin[0].prevout.n, 0, true, true);
        }

        CTxDestination address;
        if (!out.fSpendable || !ExtractDestination(cout.tx->vout[cout.i].scriptPubKey, address))
            continue;
        mapCoins[QString::fromStdString(EncodeDestination(address))].push_back(out);
    }
}

bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsLockedCoin(hash, n);
}

void WalletModel::lockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->LockCoin(output);
}

void WalletModel::unlockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->UnlockCoin(output);
}

void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->ListLockedCoins(vOutpts);
}

void WalletModel::loadReceiveRequests(std::vector<std::string>& vReceiveRequests)
{
    LOCK(wallet->cs_wallet);
    for (const PAIRTYPE(CTxDestination, AddressBook::CAddressBookData) & item : wallet->mapAddressBook)
        for (const PAIRTYPE(std::string, std::string) & item2 : item.second.destdata)
            if (item2.first.size() > 2 && item2.first.substr(0, 2) == "rr") // receive request
                vReceiveRequests.push_back(item2.second);
}

bool WalletModel::saveReceiveRequest(const std::string& sAddress, const int64_t nId, const std::string& sRequest)
{
    CTxDestination dest = DecodeDestination(sAddress);

    std::stringstream ss;
    ss << nId;
    std::string key = "rr" + ss.str(); // "rr" prefix = "receive request" in destdata

    LOCK(wallet->cs_wallet);
    if (sRequest.empty())
        return wallet->EraseDestData(dest, key);
    else
        return wallet->AddDestData(dest, key, sRequest);
}

bool WalletModel::isMine(const CTxDestination& address)
{
    return IsMine(*wallet, address);
}

bool WalletModel::isMine(const QString& addressStr)
{
    return IsMine(*wallet, DecodeDestination(addressStr.toStdString()));
}

bool WalletModel::isUsed(CTxDestination address)
{
    return wallet->IsUsed(address);
}
