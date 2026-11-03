// SPDX-License-Identifier: GPL-2.0-or-later
#include "ledgerwriter.h"
#include <mymoneyfile.h>
#include <mymoneyaccount.h>
#include <mymoneysecurity.h>
#include <mymoneyprice.h>
#include <mymoneyenums.h>
#include <securitiesmodel.h>
#include <QtTest>
#include <QUuid>
using namespace EastMoney;

class LedgerTests : public QObject {
    Q_OBJECT
    QString m_stock, m_etf;
    QDate m_date{2026, 11, 2};
    QVector<Quote> batch() const { return {{{m_stock, "1.600519", "CNY"}, decimal("1480"), m_date, "test"}, {{m_etf, "0.159915", "CNY"}, decimal("2.403"), m_date, "test"}}; }
private Q_SLOTS:
    void init()
    {
        auto file = MyMoneyFile::instance();
        file->unload();
        MyMoneyFileTransaction transaction;
        file->addCurrency(MyMoneySecurity("CNY", "Chinese Yuan", "CNY"));
        file->addCurrency(MyMoneySecurity("USD", "US Dollar", "USD"));
        file->setBaseCurrency(file->currency("CNY"));
        file->setValue("kmm-id", QUuid::createUuid().toString());
        MyMoneySecurity stock, etf;
        stock.setName("Mainland stock"); stock.setTradingSymbol("600519"); stock.setTradingCurrency("CNY"); stock.setSecurityType(eMyMoney::Security::Type::Stock);
        etf.setName("Mainland ETF"); etf.setTradingSymbol("159915"); etf.setTradingCurrency("CNY"); etf.setSecurityType(eMyMoney::Security::Type::MutualFund);
        file->addSecurity(stock); file->addSecurity(etf);
        m_stock = stock.id(); m_etf = etf.id();
        transaction.commit();
        file->setDirty(false);
    }
    void successfulPricesOnly()
    {
        auto file = MyMoneyFile::instance();
        const auto securities = file->securityList();
        const auto currencies = file->currencyList();
        const auto transactions = file->transactionCount();
        writePricesAtomically(file, batch());
        QCOMPARE(file->price(m_stock, "CNY", m_date, true).rate("CNY"), decimal("1480"));
        QCOMPARE(file->price(m_etf, "CNY", m_date, true).rate("CNY"), decimal("2.403"));
        QCOMPARE(file->securityList(), securities);
        QCOMPARE(file->currencyList(), currencies);
        QCOMPARE(file->transactionCount(), transactions);
    }
    void unownedSecuritiesNotifyAfterIndependentCommit()
    {
        auto file = MyMoneyFile::instance();
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        bool notifiedInsideTransaction = false;
        QObject context;
        connect(file, &MyMoneyFile::dataChanged, &context, [&] {
            notifiedInsideTransaction |= file->hasTransaction();
        });
        writePricesAtomically(file, batch());
        QVERIFY(file->dirty());
        QCOMPARE(changed.size(), 1);
        QVERIFY(!notifiedInsideTransaction);
        QVERIFY(!file->hasTransaction());
    }
    void refusesExistingTransactionBeforeAnyWrite()
    {
        auto file = MyMoneyFile::instance();
        const auto before = file->priceList();
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        int writerCalls = 0;
        MyMoneyFileTransaction outer;
        QVERIFY_EXCEPTION_THROWN(writePricesAtomically(file, batch(), [&](const MyMoneyPrice& price) {
            ++writerCalls;
            file->addPrice(price);
        }), Error);
        QCOMPARE(writerCalls, 0);
        QCOMPARE(file->priceList(), before);
        QVERIFY(file->hasTransaction());
        outer.rollback();
        QCOMPARE(file->priceList(), before);
        QCOMPARE(changed.size(), 0);
    }
    void associatedAccountDoesNotDuplicateCommitNotification()
    {
        auto file = MyMoneyFile::instance();
        {
            MyMoneyFileTransaction transaction;
            auto asset = file->asset();
            MyMoneyAccount investment;
            investment.setName("Test investment");
            investment.setAccountType(eMyMoney::Account::Type::Investment);
            investment.setCurrencyId("CNY");
            file->addAccount(investment, asset);
            MyMoneyAccount stock;
            stock.setName("Test mainland holding");
            stock.setAccountType(eMyMoney::Account::Type::Stock);
            stock.setCurrencyId(m_stock);
            file->addAccount(stock, investment);
            transaction.commit();
        }
        file->setDirty(false);
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        writePricesAtomically(file, batch());
        QCOMPARE(changed.size(), 1);
        QVERIFY(!file->hasTransaction());
    }
    void compensatesPartialInsertion()
    {
        auto file = MyMoneyFile::instance();
        {
            MyMoneyFileTransaction transaction;
            file->addPrice(MyMoneyPrice(m_stock, "CNY", m_date, decimal("1400"), "old-source"));
            file->addPrice(MyMoneyPrice("CNY", m_etf, m_date, decimal("0.5"), "reverse-source"));
            transaction.commit();
        }
        const auto before = file->priceList();
        QSignalSpy changed(file, &MyMoneyFile::dataChanged);
        int calls = 0;
        QVERIFY_EXCEPTION_THROWN(writePricesAtomically(file, batch(), [&](const MyMoneyPrice& price) {
            file->addPrice(price);
            if (++calls == 2) throw Error("injected writer failure");
        }), Error);
        QCOMPARE(calls, 2);
        QCOMPARE(file->priceList(), before);
        QCOMPARE(file->price(m_stock, "CNY", m_date, true).source(), QStringLiteral("old-source"));
        QVERIFY(!file->hasTransaction());
        QCOMPARE(changed.size(), 0);
    }
    void rejectsInvalidBatchBeforeAnyWrite()
    {
        auto file = MyMoneyFile::instance();
        auto quotes = batch();
        quotes[1].rate = MyMoneyMoney();
        QVERIFY_EXCEPTION_THROWN(writePricesAtomically(file, quotes), Error);
        QVERIFY(file->priceList().isEmpty());
        QVERIFY(!file->hasTransaction());
    }
    void realEngineUnloadInvalidatesSameUuid()
    {
        auto file = MyMoneyFile::instance();
        const auto id = file->storageId().toString();
        SessionGuard guard;
        guard.open(id);
        const auto old = guard.ticket();
        QObject context;
        connect(file->securitiesModel(), &QAbstractItemModel::modelAboutToBeReset, &context, [&] { guard.invalidate(); });
        file->unload();
        guard.open(id);
        QVERIFY(!guard.accepts(old, true, id));
    }
    void cleanup() { MyMoneyFile::instance()->unload(); }
};
QTEST_MAIN(LedgerTests)
#include "test_ledger.moc"
