#include "YahooPageProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>

static const QByteArray kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/135.0.0.0 Safari/537.36";

YahooPageProvider::YahooPageProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &YahooPageProvider::onReplyFinished);
}

// ── Public API ────────────────────────────────────────────────────────────────

void YahooPageProvider::fetchData(const QString &symbol, const QString &)
{
    // No historical endpoint — page scraping returns the current price only.
    // The single point merges into cached historical data in MainWindow::onDataReady.
    doFetch(symbol);
}

void YahooPageProvider::fetchLatestQuote(const QString &symbol)
{
    doFetch(symbol);
}

void YahooPageProvider::fetchSymbolType(const QString &symbol)
{
    // Indices are identified by their ^ prefix; for everything else the type
    // is extracted from the page JSON during the normal fetchData call and
    // emitted via symbolTypeReady from onReplyFinished.
    if (symbol.startsWith('^'))
        emit symbolTypeReady(symbol, SymbolType::Index);
}

// ── Network ───────────────────────────────────────────────────────────────────

void YahooPageProvider::doFetch(const QString &symbol)
{
    // Try:
    //   https://finance.yahoo.com/quote/{UPPER}/history/
    //   https://finance.yahoo.com/quote/{UPPER}
    //
    //   https://www.barchart.com/stocks/quotes/NFLX/overview

    // ^ must be percent-encoded in the URL path (%5E).
    const QUrl url("https://www.barchart.com/stocks/quotes/" +
                   QString::fromUtf8(QUrl::toPercentEncoding(symbol)) + "/overview");

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent",      kUserAgent);
    request.setRawHeader("Accept",          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, "quote"};
    Logger::instance().append(QString("YahooPage [%1] GET %2").arg(symbol, url.toString()));
}

// ── Reply handler ─────────────────────────────────────────────────────────────

void YahooPageProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    const QString symbol = m_pending.take(reply).first;

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(QString("YahooPage [%1] HTTP %2").arg(symbol).arg(httpStatus));

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(symbol, "Yahoo page: " + reply->errorString());
        return;
    }

    const QByteArray rawBody = reply->readAll();
    // YahooPage returns HTML for both fetchData and fetchLatestQuote — store for both slots
    m_lastHistoryJson = rawBody;
    m_lastQuoteJson   = rawBody;
    emit historyResponseStored();
    emit quoteResponseStored();
    const QString html = QString::fromUtf8(rawBody);
    double  price     = 0.0;
    qint64  epoch     = 0;
    QString quoteType;

    if (price <= 0.0) {
        // This targets the "raw" value of the post-market price in the JSON block
        QRegularExpression postPriceRx(R"rx("postMarketPrice\\":\{\\"raw\\":([\d,]+\.?\d*))rx");

        const auto match = postPriceRx.match(html);
        if (match.hasMatch()) {
            QString postPriceStr = match.captured(1);
            postPriceStr.remove(',');
            price = postPriceStr.toDouble();
        }

        if (price > 0) {
            QRegularExpression timeRx(R"rx("postMarketTime\\":\{\\"raw\\":(\d+))rx");
            auto timeMatch = timeRx.match(html);
            if (timeMatch.hasMatch()) {
                epoch = timeMatch.captured(1).toLongLong();
            }
        }
    }

    // ── Method 1: data-testid="qsp-price" span (stocks, ETFs, indices) ────────
    // <span ... data-testid="qsp-price">248.16 </span>
    // <span ... data-testid="qsp-price">6,582.69 </span>   ← commas for large prices
    if (price <= 0.0) {
        QRegularExpression priceRx(R"rx(data-testid="qsp-price"[^>]*>([\d,]+\.?\d*))rx");
        const auto pm = priceRx.match(html);
        if (pm.hasMatch()) {
            QString priceStr = pm.captured(1);
            priceStr.remove(',');
            price = priceStr.toDouble();
        }
    }

    // ── Method 2: fin-streamer element (fallback) ─────────────────────────────
    // <fin-streamer data-symbol="^GSPC" data-field="regularMarketPrice" data-value="6582.69">
    if (price <= 0.0) {
        const QString esc = QRegularExpression::escape(symbol);
        QRegularExpression rx1(
            "data-symbol=\"" + esc + "\"[^>]*"
            "data-field=\"regularMarketPrice\"[^>]*"
            "data-value=\"(\\d+\\.?\\d*)\"");
        QRegularExpression rx2(
            "data-field=\"regularMarketPrice\"[^>]*"
            "data-symbol=\"" + esc + "\"[^>]*"
            "data-value=\"(\\d+\\.?\\d*)\"");

        auto m1 = rx1.match(html);
        auto m2 = rx2.match(html);
        if      (m1.hasMatch()) price = m1.captured(1).toDouble();
        else if (m2.hasMatch()) price = m2.captured(1).toDouble();
    }

    // data-testid="qsp-price">10.16
    // data-testid="qsp-overnight-price">103.16

    if (price <= 0.0) {
        // ue,\"regularMarketPrice\":{\"raw\":27.35,\"fmt\":\"27.35\"}
        QRegularExpression priceRx(R"rx("regularMarketPrice\\":\{\\"raw\\":([\d,]+\.?\d*))rx");
        const auto pm = priceRx.match(html);
        if (pm.hasMatch()) {
            QString priceStr = pm.captured(1);
            priceStr.remove(',');
            price = priceStr.toDouble();
        }
    }

    // \"navPrice\":{\"raw\":18.9491,\"fmt\":\"18.95\"},
    if (price <= 0.0) {
        QRegularExpression priceRx(R"rx(navPrice\\":\s*\{\\"raw\\":\s*([\d,]+\.?\d*))rx");
        const auto pm = priceRx.match(html);
        if (pm.hasMatch()) {
            QString priceStr = pm.captured(1);
            priceStr.remove(',');
            price = priceStr.toDouble();
        }
        // QRegularExpression priceRx1(R"rx(navPrice)rx");
        // QRegularExpression priceRx2(R"rx(navPrice.":)rx");
        // QRegularExpression priceRx3(R"rx(navPrice.":\s*\{."raw")rx");
        // bool b1 = priceRx1.match(html).hasMatch();
        // bool b2 = priceRx2.match(html).hasMatch();
        // bool b3 = priceRx3.match(html).hasMatch();
    }

    if (price <= 0.0) {
        emit errorOccurred(symbol,
            "Yahoo page: price not found for " + symbol +
            " — page structure may have changed");
        return;
    }

    // ── Timestamp + quoteType from escaped JSON blobs ─────────────────────────
    // Yahoo Finance now pre-fetches API data and embeds it in
    // <script type="application/json"> tags as escaped JSON strings, e.g.:
    //   "body":"{\"symbol\":\"IBM\",\"regularMarketTime\":1775160002,...}"
    // Search each symbol occurrence until both pieces of data are found.
    {
        // HTML contains literal backslash-quotes: \"symbol\":\"IBM\"
        const QString escapedSymKey = "\\\"symbol\\\":\\\"" + symbol + "\\\"";
        int esIdx = html.indexOf(escapedSymKey);
        while (esIdx >= 0 && (epoch == 0 || quoteType.isEmpty())) {
            const QString chunk = html.mid(esIdx, 3000);

            // Timestamp – spark format: \"regularMarketTime\":1775160002
            if (epoch == 0) {
                QRegularExpression timeRx1(R"rx(\\"regularMarketTime\\":(\d{10,}))rx");
                const auto tm = timeRx1.match(chunk);
                if (tm.hasMatch()) epoch = tm.captured(1).toLongLong();
            }
            // Timestamp – summary format: \"regularMarketTime\":{\"raw\":N}
            if (epoch == 0) {
                QRegularExpression timeRx2(R"rx(\\"regularMarketTime\\":\{\\"raw\\":(\d+))rx");
                const auto tm = timeRx2.match(chunk);
                if (tm.hasMatch()) epoch = tm.captured(1).toLongLong();
            }
            // QuoteType field
            if (quoteType.isEmpty()) {
                QRegularExpression typeRx(R"rx(\\"quoteType\\":\\"([A-Z]+)\\")rx");
                const auto typeM = typeRx.match(chunk);
                if (typeM.hasMatch()) quoteType = typeM.captured(1);
            }
            // instrumentType field (spark data uses this instead of quoteType)
            if (quoteType.isEmpty()) {
                QRegularExpression instrRx(R"rx(\\"instrumentType\\":\\"([A-Z]+)\\")rx");
                const auto instrM = instrRx.match(chunk);
                if (instrM.hasMatch()) quoteType = instrM.captured(1);
            }

            esIdx = html.indexOf(escapedSymKey, esIdx + 1);
        }
    }

    // Emit symbol type if we extracted it
    if (!quoteType.isEmpty()) {
        SymbolType sType = SymbolType::Unknown;
        if      (quoteType == "EQUITY")         sType = SymbolType::Stock;
        else if (quoteType == "ETF")            sType = SymbolType::ETF;
        else if (quoteType == "MUTUALFUND")     sType = SymbolType::MutualFund;
        else if (quoteType == "CRYPTOCURRENCY") sType = SymbolType::Crypto;
        else if (quoteType == "INDEX")          sType = SymbolType::Index;
        if (sType != SymbolType::Unknown)
            emit symbolTypeReady(symbol, sType);
    }

    static const QTimeZone kET("America/New_York");
    const QDateTime ts = (epoch > 0)
        ? QDateTime::fromSecsSinceEpoch(epoch)
        : QDateTime(QDate::currentDate(), QTime(16, 0), kET);

    emit dataReady(symbol, {{ts, price}});
}
