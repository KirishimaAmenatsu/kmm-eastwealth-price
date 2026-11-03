// SPDX-License-Identifier: GPL-2.0-or-later
#include "quotes.h"
#include <QJsonObject>
#include <QtTest>
using namespace EastMoney;
class QuoteTests : public QObject {
    Q_OBJECT
    const QDateTime now = QDateTime::fromString("2026-11-03T10:00:00Z", Qt::ISODate);
    QByteArray body(QByteArray price = "2403", QByteArray scale = "3", qint64 timestamp = 1793692800LL) const {
        return "{\"rc\":0,\"data\":{\"f43\":" + price + ",\"f57\":\"159915\",\"f59\":" + scale
            + ",\"f86\":" + QByteArray::number(timestamp) + ",\"f107\":0}}";
    }
private Q_SLOTS:
    void codes_data() {
        QTest::addColumn<QString>("input"); QTest::addColumn<QString>("output");
        for (const auto& code : {"1.600519", "0.000001", "1.000001", "0.159915", "1.510300", "0.920047", "1.688001", "0.399001", "0.161725"})
            QTest::newRow(code) << QString(code) << QString(code);
        QTest::newRow("SH") << QString("SH600519") << QString("1.600519");
        QTest::newRow("suffix") << QString("159915.SZ") << QString("0.159915");
        QTest::newRow("BJ") << QString("bj920047") << QString("0.920047");
    }
    void codes() { QFETCH(QString, input); QFETCH(QString, output); QCOMPARE(canonicalSecid(input), output); }
    void rejectedCodes() {
        for (const auto& code : {"600519", "000001", "1.900901", "0.200001", "0.430047", "BJ430047", "SZ920047", "SH920047", "BJ600519", "1.00700", "NASDAQ:AAPL", "0.110001", "1.113001", "0.0030000", "0.000001&x=1", "1.159915", "0.510300", "0.000001\nfoo"})
            QVERIFY2(!validAsset(code), code);
        QVERIFY_EXCEPTION_THROWN(validateSelections({{"S1", "0.000001", "USD"}}), Error);
        QVERIFY_EXCEPTION_THROWN(validateSelections({{"S1", "0.000001", "CNY"}, {"S1", "1.000001", "CNY"}}), Error);
        QVERIFY_EXCEPTION_THROWN(validateSelections({}), Error);
        QVector<Selection> many;
        for (int i = 0; i < 101; ++i) many << Selection{QString::number(i), "0.000001", "CNY"};
        QVERIFY_EXCEPTION_THROWN(validateSelections(many), Error);
        QCOMPARE(validateSelections({{"S1", "SH600519", "CNY"}})[0].asset, QString("1.600519"));
    }
    void exactScaling() {
        QCOMPARE(parseQuote(body(), "0.159915", now).rate, MyMoneyMoney("2403/1000"));
        QCOMPARE(parseQuote(body("9007199254740993", "8"), "0.159915", now).rate, MyMoneyMoney("9007199254740993/100000000"));
        QCOMPARE(parseQuote(body("8", "8"), "0.159915", now).rate, MyMoneyMoney("1/12500000"));
        QCOMPARE(parseQuote(body("12", "0"), "0.159915", now).rate, MyMoneyMoney(12));
    }
    void rejectFields() {
        for (auto price : {"0", "-1", "null", "\"-\"", "\"2403\"", "1.2", "1e2", "true"})
            QVERIFY_EXCEPTION_THROWN(parseQuote(body(price), "0.159915", now), Error);
        for (auto scale : {"9", "-1", "null", "2.0", "99999999999999999999"})
            QVERIFY_EXCEPTION_THROWN(parseQuote(body("100", scale), "0.159915", now), Error);
        for (auto bytes : {QByteArray("{}"), QByteArray("{broken"), QByteArray("[]"), QByteArray("{\"rc\":0,\"data\":null}"), QByteArray("{\"rc\":1,\"data\":{}}")})
            QVERIFY_EXCEPTION_THROWN(parseQuote(bytes, "0.159915", now), Error);
        for (auto key : {"f43", "f59", "f86", "f57", "f107"}) {
            auto doc = QJsonDocument::fromJson(body()); auto data = doc.object().value("data").toObject(); data.remove(key);
            auto root = doc.object(); root["data"] = data;
            QVERIFY_EXCEPTION_THROWN(parseQuote(QJsonDocument(root).toJson(), "0.159915", now), Error);
        }
        QVERIFY_EXCEPTION_THROWN(parseQuote(body(), "1.510300", now), Error);
        auto wrongMarket = body(); wrongMarket.replace("\"f107\":0", "\"f107\":1");
        QVERIFY_EXCEPTION_THROWN(parseQuote(wrongMarket, "0.159915", now), Error);
        auto ambiguousCode = body(); ambiguousCode.replace("159915", "000001");
        QCOMPARE(parseQuote(ambiguousCode, "0.000001", now).rate, decimal("2.403"));
        QVERIFY_EXCEPTION_THROWN(parseQuote(ambiguousCode, "1.000001", now), Error);
        ambiguousCode.replace("\"f107\":0", "\"f107\":1");
        QCOMPARE(parseQuote(ambiguousCode, "1.000001", now).rate, decimal("2.403"));
        QVERIFY_EXCEPTION_THROWN(parseQuote(ambiguousCode, "0.000001", now), Error);
    }
    void shanghaiDateAndFreshness() {
        const auto midnight = QDateTime::fromString("2026-11-02T16:00:01Z", Qt::ISODate);
        QCOMPARE(parseQuote(body("100", "2", midnight.toSecsSinceEpoch()), "0.159915", now).date, QDate(2026, 11, 3));
        QVERIFY_EXCEPTION_THROWN(parseQuote(body("100", "2", now.addSecs(301).toSecsSinceEpoch()), "0.159915", now), Error);
        QVERIFY_EXCEPTION_THROWN(parseQuote(body("100", "2", now.addDays(-15).toSecsSinceEpoch()), "0.159915", now), Error);
        QCOMPARE(parseQuote(body("100", "2", now.addDays(-14).toSecsSinceEpoch()), "0.159915", now).date, QDate(2026, 10, 20));
        QVERIFY_EXCEPTION_THROWN(parseQuote(body("100", "2", 0), "0.159915", now), Error);
        QVERIFY_EXCEPTION_THROWN(parseQuote(body("100", "2", 9223372036854775807LL), "0.159915", now), Error);
    }
    void session() {
        SessionGuard guard; guard.open("A"); const auto old = guard.ticket();
        QVERIFY(guard.accepts(old, true, "A")); QVERIFY(!guard.accepts(old, false, "A"));
        guard.open("A"); QVERIFY(!guard.accepts(old, true, "A"));
        const auto current = guard.ticket(); guard.invalidate(); QVERIFY(!guard.accepts(current, true, "A"));
        guard.open("B"); QVERIFY(!guard.accepts(old, true, "B"));
    }
};
QTEST_GUILESS_MAIN(QuoteTests)
#include "test_quotes.moc"
