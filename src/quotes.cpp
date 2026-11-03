// SPDX-License-Identifier: GPL-2.0-or-later
#include "quotes.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

namespace EastMoney {
QString canonicalSecid(const QString& input)
{
    const auto text = input.trimmed().toUpper();
    QString market, code, exchange;
    static const QRegularExpression canonical("^([01])\\.([0-9]{6})$");
    static const QRegularExpression prefix("^(SH|SZ|BJ)[.:]?([0-9]{6})$");
    static const QRegularExpression suffix("^([0-9]{6})[.:]?(SH|SZ|BJ)$");
    auto match = canonical.match(text);
    if (match.hasMatch()) { market = match.captured(1); code = match.captured(2); }
    else if ((match = prefix.match(text)).hasMatch()) { exchange = match.captured(1); code = match.captured(2); }
    else if ((match = suffix.match(text)).hasMatch()) { code = match.captured(1); exchange = match.captured(2); }
    else throw Error(QStringLiteral("必须显式指定市场，例如 1.600519、1.000001、0.000001、0.159915、0.920047；不接受裸六位代码"));
    if (!exchange.isEmpty()) market = exchange == "SH" ? "1" : "0";
    // Conservative mainland-only whitelist: A shares, current BJ codes,
    // major SH/SZ index families and exchange-traded ETF/LOF. No B shares,
    // bonds, off-exchange NAV funds, legacy BJ codes, HK/US or FX guessing.
    static const QRegularExpression sh("^(?:60[0135][0-9]{3}|688[0-9]{3}|000[0-9]{3}|51[0-9]{4}|56[0-9]{4}|58[0-9]{4})$");
    static const QRegularExpression sz("^(?:00[0-3][0-9]{3}|30[01][0-9]{3}|399[0-9]{3}|159[0-9]{3}|16[0-9]{4}|920[0-9]{3})$");
    if (!(market == "1" ? sh : sz).match(code).hasMatch()
        || (exchange == "BJ" && !code.startsWith("920"))
        || (exchange == "SZ" && code.startsWith("920")))
        throw Error(QStringLiteral("首版不支持该市场/代码：%1").arg(input));
    return market + '.' + code;
}
bool validAsset(const QString& input) { try { canonicalSecid(input); return true; } catch (const Error&) { return false; } }
bool supportedFiat(const QString& fiat) { return fiat == "CNY"; }
QVector<Selection> validateSelections(const QVector<Selection>& selections)
{
    if (selections.isEmpty() || selections.size() > 100) throw Error(QStringLiteral("请选择 1 至 100 只证券"));
    QVector<Selection> result;
    QSet<QString> seen;
    for (auto s : selections) {
        if (s.securityId.isEmpty() || seen.contains(s.securityId) || !supportedFiat(s.fiat))
            throw Error(QStringLiteral("重复/无效证券或非 CNY 交易货币"));
        s.asset = canonicalSecid(s.asset);
        seen.insert(s.securityId);
        result << s;
    }
    return result;
}
MyMoneyMoney decimal(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^(0|[1-9][0-9]*)(?:\\.([0-9]+))?(?:[eE]([+-]?[0-9]+))?$"));
    const auto match = re.match(text);
    if (text.size() > 64 || !match.hasMatch()) throw Error(QStringLiteral("Invalid positive decimal"));
    bool ok = true;
    const int exponent = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt(&ok);
    if (!ok || qAbs(qint64(exponent)) > 30 || match.captured(1).size() + match.captured(2).size() > 40)
        throw Error(QStringLiteral("Decimal exceeds precision limits"));
    QString numerator = match.captured(1) + match.captured(2);
    // Prevent GMP leading-zero octal interpretation.
    while (numerator.startsWith('0')) numerator.remove(0, 1);
    if (numerator.isEmpty()) throw Error(QStringLiteral("Zero price"));
    const int scale = match.captured(2).size() - exponent;
    QString denominator = QStringLiteral("1");
    if (scale < 0) numerator += QString(-scale, '0');
    else denominator += QString(scale, '0');
    const MyMoneyMoney result(numerator + '/' + denominator);
    if (!result.isPositive()) throw Error(QStringLiteral("Nonpositive price"));
    return result;
}
QJsonDocument exactJson(const QByteArray& bytes)
{
    // Preserve original number lexemes; never route price arithmetic via double.
    QJsonParseError error;
    QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) throw Error(QStringLiteral("Malformed JSON: %1").arg(error.errorString()));
    QByteArray exact;
    exact.reserve(bytes.size());
    bool inString = false, escaped = false;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        if (inString) {
            exact += c;
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
        } else if (c == '"') { inString = true; exact += c; }
        else if ((c >= '0' && c <= '9') || c == '-') {
            exact += '"';
            while (i < bytes.size() && QByteArray("0123456789.eE+-").contains(bytes[i])) exact += bytes[i++];
            exact += '"'; --i;
        } else exact += c;
    }
    const auto result = QJsonDocument::fromJson(exact, &error);
    if (error.error != QJsonParseError::NoError || result.isNull()) throw Error(QStringLiteral("Invalid JSON document"));
    return result;
}
static QString integer(const QJsonObject& object, const QString& key)
{
    static const QRegularExpression re("^(0|[1-9][0-9]{0,19})$");
    const auto value = object.value(key);
    if (!value.isString() || !re.match(value.toString()).hasMatch()) throw Error(QStringLiteral("Missing/invalid integer field %1").arg(key));
    return value.toString();
}
Price parseQuote(const QByteArray& bytes, const QString& input, QDateTime now)
{
    const auto secid = canonicalSecid(input);
    const auto wire = QJsonDocument::fromJson(bytes).object();
    const auto wireData = wire.value("data").toObject();
    // The provider's integer fields must actually be JSON numbers, not strings
    // that merely happen to contain digits. This DOM is used only for types.
    if (!wire.value("rc").isDouble()) throw Error(QStringLiteral("Invalid rc type"));
    for (const auto& key : {"f43", "f59", "f86", "f107"})
        if (!wireData.value(key).isDouble()) throw Error(QStringLiteral("Invalid numeric field type: %1").arg(key));
    if (!wireData.value("f57").isString()) throw Error(QStringLiteral("Invalid code type"));
    const auto doc = exactJson(bytes);
    if (!doc.isObject()) throw Error(QStringLiteral("Quote is not an object"));
    const auto root = doc.object();
    if (integer(root, "rc") != "0" || !root.value("data").isObject()) throw Error(QStringLiteral("Unsuccessful or missing quote data"));
    const auto data = root.value("data").toObject();
    if (data.value("f57").toString() != secid.mid(2) || integer(data, "f107") != secid.left(1))
        throw Error(QStringLiteral("Quote identity mismatch: %1").arg(secid));
    const auto scaleText = integer(data, "f59");
    bool ok = false;
    const int scale = scaleText.toInt(&ok);
    if (!ok || scale > 8) throw Error(QStringLiteral("Unsupported price scale"));
    const auto price = integer(data, "f43");
    const auto rate = decimal(price + "e-" + QString::number(scale));
    const qint64 seconds = integer(data, "f86").toLongLong(&ok);
    // Bound before Qt conversion: avoid overflow, pre-epoch and implausible dates.
    if (!ok || seconds <= 0 || seconds > 253402300799LL || !now.isValid()) throw Error(QStringLiteral("Invalid quote timestamp"));
    const auto timestamp = QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC);
    const auto zone = QTimeZone::fromSecondsAheadOfUtc(8 * 3600);
    const auto date = timestamp.toTimeZone(zone).date();
    const auto today = now.toTimeZone(zone).date();
    if (!timestamp.isValid() || timestamp > now.addSecs(300) || date.daysTo(today) > 14)
        throw Error(QStringLiteral("Quote timestamp is future or older than 14 calendar days"));
    return {rate, date};
}
}
