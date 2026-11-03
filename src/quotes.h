// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDateTime>
#include <QJsonDocument>
#include <QVector>
#include <mymoneymoney.h>
#include <stdexcept>

namespace EastMoney {
class Error : public std::runtime_error {
public:
    explicit Error(const QString& message) : std::runtime_error(message.toStdString()) {}
};
struct Selection {
    QString securityId;
    QString asset; // Explicit market code; normalized to canonical secid.
    QString fiat;
    bool operator==(const Selection& o) const { return securityId == o.securityId && asset == o.asset && fiat == o.fiat; }
};
struct Quote { Selection selection; MyMoneyMoney rate; QDate spotDate; QString source; };
struct Price { MyMoneyMoney rate; QDate date; };
QString canonicalSecid(const QString& input);
bool validAsset(const QString& input);
bool supportedFiat(const QString& fiat);
QVector<Selection> validateSelections(const QVector<Selection>& selections);
MyMoneyMoney decimal(const QString& text);
QJsonDocument exactJson(const QByteArray& bytes);
Price parseQuote(const QByteArray& bytes, const QString& secid, QDateTime now);

// Generation also distinguishes close/reopen of the very same UUID.
class SessionGuard {
public:
    struct Ticket { QString storage; quint64 generation; };
    void invalidate() { ++m_generation; m_storage.clear(); }
    void open(const QString& storage) { invalidate(); m_storage = storage; }
    Ticket ticket() const { return {m_storage, m_generation}; }
    bool accepts(const Ticket& ticket, bool fileOpen, const QString& currentStorage) const {
        return fileOpen && !m_storage.isEmpty() && ticket.generation == m_generation
            && ticket.storage == m_storage && ticket.storage == currentStorage;
    }
private:
    quint64 m_generation = 0;
    QString m_storage;
};
}
Q_DECLARE_METATYPE(EastMoney::Quote)
Q_DECLARE_METATYPE(QVector<EastMoney::Quote>)
