// SPDX-License-Identifier: GPL-2.0-or-later
#include "quoteclient.h"
#include <QCoreApplication>
#include <QTextStream>
using namespace EastMoney;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    auto args = app.arguments(); args.removeFirst();
    if (args.isEmpty()) { QTextStream(stderr) << "Usage: quote-smoke 1.600519 1.000001 0.159915 0.920047\n"; return 2; }
    QVector<Selection> selections;
    for (int i = 0; i < args.size(); ++i) selections << Selection{QString::number(i), args[i], "CNY"};
    QuoteClient client;
    QObject::connect(&client, &QuoteClient::succeeded, &app, [&](const QVector<Quote>& quotes) {
        QTextStream out(stdout);
        for (const auto& quote : quotes) out << quote.selection.asset << " CNY " << quote.rate.toString()
            << " date=" << quote.spotDate.toString(Qt::ISODate) << " source=" << quote.source << '\n';
        app.exit(0);
    });
    QObject::connect(&client, &QuoteClient::failed, &app, [&](const QString& error) {
        QTextStream(stderr) << "FAILED: " << error << '\n'; app.exit(1);
    });
    QTimer::singleShot(0, &app, [&] { client.start(selections); });
    return app.exec();
}
