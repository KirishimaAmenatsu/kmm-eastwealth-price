// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include <QNetworkReply>
#include <QUrlQuery>
#include <QtTest>
#include <cstring>
using namespace EastMoney;

struct Response { QByteArray body; int status = 200; int delayMs = 0; QByteArray retry; qint64 declaredSize = -1; QNetworkReply::NetworkError error = QNetworkReply::NoError; };
class FakeReply : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest& request, Response response, QObject* parent) : QNetworkReply(parent), m_response(response)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(response.delayMs, this, [this] {
            if (isFinished()) return;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
            if (m_response.error != NoError) setError(m_response.error, "injected transport/TLS failure");
            if (!m_response.retry.isEmpty()) setRawHeader("Retry-After", m_response.retry);
            if (m_response.declaredSize >= 0) setHeader(QNetworkRequest::ContentLengthHeader, m_response.declaredSize);
            Q_EMIT metaDataChanged();
            if (isFinished()) return;
            m_available = true;
            Q_EMIT readyRead();
            if (isFinished()) return;
            setFinished(true);
            Q_EMIT finished();
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        Q_EMIT finished();
    }
    qint64 bytesAvailable() const override { return (m_available ? m_response.body.size() - m_offset : 0) + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (!m_available) return 0;
        const auto count = qMin(maxSize, m_response.body.size() - m_offset);
        if (count <= 0) return -1;
        std::memcpy(data, m_response.body.constData() + m_offset, count);
        m_offset += count;
        return count;
    }
private:
    Response m_response;
    qint64 m_offset = 0;
    bool m_available = false;
};
class FakeManager : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QUrl> urls;
    QList<qint64> times;
    bool secureRequests = true;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        urls << request.url();
        times << QDateTime::currentMSecsSinceEpoch();
        secureRequests &= request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy
            && request.rawHeader("Accept") == "application/json";
        const auto response = responses.isEmpty() ? Response{"unexpected request", 500} : responses.takeFirst();
        return new FakeReply(request, response, this);
    }
};
static QByteArray quote(QByteArray code = "600519", QByteArray market = "1", QByteArray price = "148000", QByteArray scale = "2")
{
    return "{\"rc\":0,\"data\":{\"f43\":" + price + ",\"f57\":\"" + code + "\",\"f59\":" + scale
        + ",\"f107\":" + market + ",\"f86\":" + QByteArray::number(QDateTime::currentSecsSinceEpoch()) + "}}";
}
class NetworkTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void successfulBatch()
    {
        FakeManager manager;
        manager.responses = {{quote()}, {quote("159915", "0", "2403", "3")}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "1.600519", "CNY"}, {"S2", "0.159915", "CNY"}, {"S3", "SH600519", "CNY"}});
        client.start({{"ignored", "0.000001", "CNY"}}); // Reentrancy is ignored.
        QTRY_COMPARE(success.size(), 1);
        QCOMPARE(failure.size(), 0);
        QCOMPARE(manager.urls.size(), 2); // Same secid deduplicated, all selections retained.
        QVERIFY(manager.secureRequests);
        QVERIFY(manager.times[1] - manager.times[0] >= 80); // 100ms pacing (timer tolerance).
        QCOMPARE(QUrlQuery(manager.urls[0]).queryItemValue("secid"), QString("1.600519"));
        QCOMPARE(QUrlQuery(manager.urls[1]).queryItemValue("secid"), QString("0.159915"));
        for (const auto& url : manager.urls) {
            QCOMPARE(url.scheme(), QString("https")); QCOMPARE(url.host(), QString("push2.eastmoney.com"));
            QCOMPARE(url.path(), QString("/api/qt/stock/get"));
        }
        const auto quotes = qvariant_cast<QVector<Quote>>(success[0][0]);
        QCOMPARE(quotes.size(), 3);
        QCOMPARE(quotes[0].rate, decimal("1480"));
        QCOMPARE(quotes[1].rate, decimal("2.403"));
        QCOMPARE(quotes[2].rate, quotes[0].rate);
        QCOMPARE(quotes[2].selection.securityId, QString("S3"));
        QVERIFY(!client.busy());
    }
    void partialBatchNeverSucceeds()
    {
        FakeManager manager;
        manager.responses = {{quote()}, {quote("000001", "1")}}; // Code is right, market wrong.
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "1.600519", "CNY"}, {"S2", "0.000001", "CNY"}});
        QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(success.size(), 0);
        QCOMPARE(manager.urls.size(), 2);
    }
    void httpFailures_data()
    {
        QTest::addColumn<int>("status");
        for (int status : {301, 403, 418, 429, 500}) QTest::newRow(qPrintable(QString::number(status))) << status;
    }
    void httpFailures()
    {
        QFETCH(int, status);
        FakeManager manager;
        manager.responses = {{"{}", status, 0, "120"}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "1.600519", "CNY"}});
        QTRY_COMPARE(failure.size(), 1);
        QCOMPARE(success.size(), 0);
        if (status == 429 || status == 418) {
            QVERIFY(client.retryAfter("push2.eastmoney.com") > QDateTime::currentDateTimeUtc().addSecs(100));
            client.start({{"S1", "1.600519", "CNY"}});
            QCOMPARE(failure.size(), 2);
            QCOMPARE(manager.urls.size(), 1); // No request during Retry-After.
        }
    }
    void timeoutAndTotalDeadline()
    {
        for (bool total : {false, true}) {
            FakeManager manager;
            manager.responses = {{quote(), 200, 1000}};
            QuoteClient client(nullptr, &manager);
            client.setLimits({total ? 1000 : 20, total ? 20 : 2000, 1024});
            QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
            client.start({{"S1", "1.600519", "CNY"}});
            QTRY_COMPARE(failure.size(), 1);
            QCOMPARE(success.size(), 0);
            QVERIFY(!client.busy());
        }
    }
    void retryAfterHttpDateAndFallback()
    {
        const auto future = QDateTime::currentDateTimeUtc().addSecs(600);
        for (const QByteArray retry : {QLocale::c().toString(future, "ddd, dd MMM yyyy HH:mm:ss 'GMT'").toLatin1(), QByteArray("not-a-date"), QByteArray(), QByteArray("9223372036854775807"), QByteArray("99999999999999999999999999999"), QByteArray("-1"), QByteArray("0"), QByteArray("Mon, 01 Jan 1990 00:00:00 GMT"), QByteArray("172800")}) {
            FakeManager manager;
            manager.responses = {{"{}", 429, 0, retry}};
            QuoteClient client(nullptr, &manager);
            QSignalSpy failure(&client, &QuoteClient::failed);
            client.start({{"S1", "1.600519", "CNY"}});
            QTRY_COMPARE(failure.size(), 1);
            const auto until = client.retryAfter("push2.eastmoney.com");
            QVERIFY(until.isValid());
            QVERIFY(until > QDateTime::currentDateTimeUtc().addSecs(290));
            QVERIFY(until.date() <= QDate(9999, 12, 31));
            if (retry == "172800") QVERIFY(until > QDateTime::currentDateTimeUtc().addSecs(172790));
            if (retry.startsWith("9")) QCOMPARE(until.date(), QDate(9999, 12, 31));
        }
    }
    void cancelAndRestart()
    {
        FakeManager manager;
        manager.responses = {{quote(), 200, 1000}, {quote()}};
        QuoteClient client(nullptr, &manager);
        QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
        client.start({{"S1", "1.600519", "CNY"}});
        client.cancel();
        QVERIFY(!client.busy());
        client.start({{"S2", "1.600519", "CNY"}});
        QTRY_COMPARE(success.size(), 1);
        QCOMPARE(qvariant_cast<QVector<Quote>>(success[0][0])[0].selection.securityId, QStringLiteral("S2"));
        QCOMPARE(failure.size(), 0);
    }
    void responseSizeLimits()
    {
        for (qint64 declared : {-1, 10000}) {
            FakeManager manager;
            manager.responses = {{QByteArray(1025, ' '), 200, 0, {}, declared}};
            QuoteClient client(nullptr, &manager);
            client.setLimits({1000, 2000, 1024});
            QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
            client.start({{"S1", "1.600519", "CNY"}});
            QTRY_COMPARE(failure.size(), 1);
            QCOMPARE(success.size(), 0);
        }
    }
    void destructionCancels()
    {
        FakeManager manager;
        manager.responses = {{quote(), 200, 1000}};
        auto client = new QuoteClient(nullptr, &manager);
        client->start({{"S1", "1.600519", "CNY"}});
        delete client;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(manager.findChildren<QNetworkReply*>().size(), 0);
    }
    void malformedAndTransportFailures() {
        for (const auto& response : {Response{"not JSON"}, Response{"{\"rc\":0,\"data\":null}"}, Response{quote("600519", "1", "0")},
                                    Response{quote(), 200, 0, {}, -1, QNetworkReply::SslHandshakeFailedError},
                                    Response{quote(), 200, 0, {}, -1, QNetworkReply::RemoteHostClosedError}}) {
            FakeManager manager; manager.responses = {response};
            QuoteClient client(nullptr, &manager);
            QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
            client.start({{"S1", "1.600519", "CNY"}});
            QTRY_COMPARE(failure.size(), 1); QCOMPARE(success.size(), 0); QCOMPARE(manager.urls.size(), 1);
        }
    }
    void invalidInputMakesNoRequest() {
        FakeManager manager; QuoteClient client(nullptr, &manager);
        QSignalSpy failure(&client, &QuoteClient::failed);
        client.start({{"S1", "600519", "CNY"}});
        QCOMPARE(failure.size(), 1); QVERIFY(manager.urls.isEmpty());
        client.start({{"S1", "1.600519", "HKD"}});
        QCOMPARE(failure.size(), 2); QVERIFY(manager.urls.isEmpty());
    }
    void cancelAndDeadlineDuringPacing() {
        for (bool cancel : {true, false}) {
            FakeManager manager; manager.responses = {{quote()}, {quote("159915", "0")}};
            QuoteClient client(nullptr, &manager);
            client.setLimits({1000, cancel ? 2000 : 100, 1024, 300});
            QSignalSpy success(&client, &QuoteClient::succeeded), failure(&client, &QuoteClient::failed);
            client.start({{"S1", "1.600519", "CNY"}, {"S2", "0.159915", "CNY"}});
            QTest::qWait(40);
            QCOMPARE(manager.urls.size(), 1);
            if (cancel) client.cancel();
            QTest::qWait(350);
            QCOMPARE(manager.urls.size(), 1); QCOMPARE(success.size(), 0);
            QCOMPARE(failure.size(), cancel ? 0 : 1);
            QVERIFY(!client.busy());
        }
    }
};
QTEST_GUILESS_MAIN(NetworkTests)
#include "test_network.moc"
