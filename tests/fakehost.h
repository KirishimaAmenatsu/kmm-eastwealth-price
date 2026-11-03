// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "upstream/interfaceloader.h"
#include <appinterface.h>
#include <mymoneyfile.h>
#include <mymoneysecurity.h>
#include <mymoneyenums.h>
#include <QUrl>
#include <QUuid>

// The test executable does not link the real KMyMoneyApp. This friend bridge
// installs/restores only the fake AppInterface in the real plugin registry.
class KMyMoneyApp {
public:
    static KMyMoneyPlugin::AppInterface* exchangeApp(KMyMoneyPlugin::AppInterface* app) {
        auto& interfaces = KMyMoneyPlugin::pluginInterfaces();
        const auto previous = interfaces.appInterface;
        interfaces.appInterface = app;
        return previous;
    }
};
class FakeAppInterface : public KMyMoneyPlugin::AppInterface {
public:
    FakeAppInterface() : AppInterface(nullptr) {}
    bool opened = false;
    bool fileOpen() override { return opened; }
    bool isDatabase() override { return false; }
    bool isNativeFile() override { return true; }
    QUrl filenameURL() const override { return {}; }
    void writeFilenameURL(const QUrl&) override {}
    QUrl lastOpenedURL() override { return {}; }
    void writeLastUsedFile(const QString&) override {}
    void slotFileOpenRecent(const QUrl&) override {}
    void addToRecentFiles(const QUrl&) override {}
    KMyMoneyAppCallback progressCallback() override { return nullptr; }
    void writeLastUsedDir(const QString&) override {}
    QString readLastUsedDir() const override { return {}; }
    void consistencyCheck(bool) override {}
};
class HostInterfaceScope {
public:
    explicit HostInterfaceScope(FakeAppInterface* app) : m_previous(KMyMoneyApp::exchangeApp(app)) {}
    ~HostInterfaceScope() { KMyMoneyApp::exchangeApp(m_previous); }
private:
    KMyMoneyPlugin::AppInterface* m_previous;
};
class InMemoryLedger {
public:
    InMemoryLedger() { MyMoneyFile::instance()->unload(); }
    ~InMemoryLedger() { MyMoneyFile::instance()->unload(); }
    void open(const QUuid& uuid) {
        auto file = MyMoneyFile::instance();
        file->unload();
        {
            MyMoneyFileTransaction transaction;
            file->addCurrency(MyMoneySecurity("CNY", "Chinese Yuan", "CNY"));
            file->setBaseCurrency(file->currency("CNY"));
            file->setValue("kmm-id", uuid.toString());
            MyMoneySecurity stock;
            stock.setName("Test mainland stock");
            stock.setTradingSymbol("600519");
            stock.setTradingCurrency("CNY");
            stock.setSecurityType(eMyMoney::Security::Type::Stock);
            stock.setValue("kmm-online-source", "untouched-existing-source");
            file->addSecurity(stock);
            MyMoneySecurity fund;
            fund.setName("Test exchange ETF");
            fund.setTradingSymbol("159915");
            fund.setTradingCurrency("CNY");
            fund.setSecurityType(eMyMoney::Security::Type::MutualFund);
            fund.setValue("kmm-online-source", "untouched-existing-fund-source");
            file->addSecurity(fund);
            MyMoneySecurity bond;
            bond.setName("Unsupported bond must not be listed");
            bond.setTradingSymbol("113001");
            bond.setTradingCurrency("CNY");
            bond.setSecurityType(eMyMoney::Security::Type::Bond);
            file->addSecurity(bond);
            transaction.commit();
        }
        file->setDirty(false);
        file->modelsReadyToUse();
    }
};
