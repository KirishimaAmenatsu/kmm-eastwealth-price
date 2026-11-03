// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTimeZone>
#include <memory>
#include <algorithm>

namespace EastMoney {
QuoteClient::QuoteClient(QObject* parent, QNetworkAccessManager* manager)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this))
{
    m_totalTimer.setSingleShot(true);
    m_requestTimer.setSingleShot(true);
    m_spacingTimer.setSingleShot(true);
    connect(&m_spacingTimer, &QTimer::timeout, this, &QuoteClient::next);
    connect(&m_totalTimer, &QTimer::timeout, this, [this] { fail(QStringLiteral("Quote batch total deadline exceeded")); });
    connect(&m_requestTimer, &QTimer::timeout, this, [this] { fail(QStringLiteral("HTTPS request timed out")); });
}
void QuoteClient::setLimits(Limits limits)
{
    if (!m_busy && limits.requestMs > 0 && limits.totalMs > 0 && limits.bodyBytes > 0
        && limits.bodyBytes <= 1024 * 1024 && limits.spacingMs >= 0) m_limits = limits;
}
void QuoteClient::cancel()
{
    ++m_epoch;
    m_busy = false;
    m_totalTimer.stop();
    m_requestTimer.stop();
    m_spacingTimer.stop();
    if (m_reply) {
        const auto reply = m_reply;
        m_reply.clear();
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}
void QuoteClient::fail(const QString& message)
{
    if (!m_busy) return;
    cancel();
    Q_EMIT failed(message);
}
void QuoteClient::start(const QVector<Selection>& selections)
{
    if (m_busy) return; // Never overlap two batches.
    m_busy = true;
    ++m_epoch;
    m_prices.clear();
    m_pending.clear();
    m_totalTimer.start(m_limits.totalMs);
    try {
        m_selections = validateSelections(selections);
        for (const auto& selection : m_selections)
            if (!m_pending.contains(selection.asset)) m_pending << selection.asset;
        next();
    } catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
}
void QuoteClient::request(const QUrl& url, std::function<void(const QByteArray&)> done)
{
    if (!m_busy) return;
    if (url.scheme() != "https" || url.host() != "push2.eastmoney.com"
        || url.path() != "/api/qt/stock/get" || !url.userInfo().isEmpty() || url.port() != -1) {
        fail(QStringLiteral("Refusing non-approved HTTPS endpoint"));
        return;
    }
    const auto now = QDateTime::currentDateTimeUtc();
    if (m_cooldowns.value(url.host()) > now) {
        fail(QStringLiteral("Rate limited by %1; retry after %2").arg(url.host(), m_cooldowns[url.host()].toString(Qt::ISODate)));
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "KMyMoney-EastMoney-Price/0.1");
    // TLS errors are intentionally never ignored. Each request has a wall-clock
    // deadline, not only Qt's inactivity timeout.
    const auto epoch = m_epoch;
    auto reply = m_manager->get(req);
    m_reply = reply;
    reply->setReadBufferSize(m_limits.bodyBytes + 1);
    auto bytes = std::make_shared<QByteArray>();
    m_requestTimer.start(m_limits.requestMs);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, bytes, epoch] {
        if (!m_busy || epoch != m_epoch) return;
        bytes->append(reply->read(m_limits.bodyBytes + 1 - bytes->size()));
        if (bytes->size() > m_limits.bodyBytes) fail(QStringLiteral("HTTPS response exceeds size limit"));
    });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply, epoch] {
        if (!m_busy || epoch != m_epoch) return;
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > m_limits.bodyBytes)
            fail(QStringLiteral("HTTPS Content-Length exceeds size limit"));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, epoch, url, done] {
        if (!m_busy || epoch != m_epoch) return;
        m_requestTimer.stop();
        m_reply.clear();
        reply->deleteLater();
        bytes->append(reply->read(m_limits.bodyBytes + 1 - bytes->size()));
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 418) {
            const auto now = QDateTime::currentDateTimeUtc();
            const auto raw = reply->rawHeader("Retry-After").trimmed();
            bool ok = false;
            const auto seconds = raw.toLongLong(&ok);
            // Saturate before addSecs: huge numeric Retry-After must not
            // overflow or turn a very long ban into an immediate retry. Normal
            // valid Retry-After values are honored without shortening them.
            const QDateTime latest(QDate(9999, 12, 31), QTime(23, 59, 59), QTimeZone::UTC);
            const auto maxSeconds = now.secsTo(latest);
            const bool numeric = !raw.isEmpty() && std::all_of(raw.begin(), raw.end(), [](char c) { return c >= '0' && c <= '9'; });
            auto until = numeric ? now.addSecs(ok ? qBound(qint64(0), seconds, maxSeconds) : maxSeconds)
                                 : QDateTime::fromString(QString::fromLatin1(raw), Qt::RFC2822Date);
            if (!until.isValid() || until <= now) until = now.addSecs(300);
            m_cooldowns.insert(url.host(), until);
            fail(QStringLiteral("HTTP %1 from %2; retry after %3 (no automatic retry)").arg(status).arg(url.host(), until.toString(Qt::ISODate)));
            return;
        }
        if (bytes->size() > m_limits.bodyBytes) { fail(QStringLiteral("HTTPS response exceeds size limit")); return; }
        if (status != 200 || reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("HTTPS %1: HTTP %2, %3").arg(url.host()).arg(status).arg(reply->errorString()));
            return;
        }
        try { done(*bytes); }
        catch (const std::exception& e) { fail(QString::fromUtf8(e.what())); }
    });
}
void QuoteClient::next()
{
    if (!m_busy) return;
    if (m_pending.isEmpty()) { finish(); return; }
    const auto secid = m_pending.takeFirst();
    QUrl url(QStringLiteral("https://push2.eastmoney.com/api/qt/stock/get"));
    QUrlQuery query;
    query.addQueryItem("secid", secid);
    query.addQueryItem("fields", "f43,f57,f58,f59,f86,f107");
    url.setQuery(query);
    request(url, [this, secid](const QByteArray& body) {
        m_prices.insert(secid, parseQuote(body, secid, QDateTime::currentDateTimeUtc()));
        if (m_pending.isEmpty()) finish();
        else m_spacingTimer.start(m_limits.spacingMs);
    });
}
void QuoteClient::finish()
{
    QVector<Quote> quotes;
    for (const auto& selection : m_selections) {
        const auto it = m_prices.constFind(selection.asset);
        if (it == m_prices.cend()) throw Error(QStringLiteral("Missing validated quote"));
        quotes << Quote{selection, it->rate, it->date, QStringLiteral("EastMoney [%1]; CNY; quote update date Asia/Shanghai").arg(selection.asset)};
    }
    cancel();
    Q_EMIT succeeded(quotes);
}
}
