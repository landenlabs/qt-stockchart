#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QLabel>
#include <QDialogButtonBox>

static QString signupUrl(const QString &id)
{
    if (id == "alphavantage") return "https://www.alphavantage.co/support/#api-key";
    if (id == "finnhub")      return "https://finnhub.io/register";
    if (id == "polygon")      return "https://polygon.io/dashboard/signup";
    if (id == "twelvedata")   return "https://twelvedata.com/";
    if (id == "fmp")          return "https://financialmodelingprep.com/developer/docs/";
    return {};
}

static QString accountUrl(const QString &id)
{
    if (id == "alphavantage") return "https://www.alphavantage.co/premium/";
    if (id == "finnhub")      return "https://finnhub.io/dashboard";
    if (id == "polygon")      return "https://polygon.io/dashboard/billing";
    if (id == "twelvedata")   return "https://twelvedata.com/account";
    if (id == "fmp")          return "https://financialmodelingprep.com/developer/docs/";
    return {};
}

SettingsDialog::SettingsDialog(const QList<StockDataProvider*> &providers,
                               const QString &activeProviderId,
                               QWidget *parent)
    : QDialog(parent)
    , m_providers(providers)
{
    setWindowTitle("Configure API Keys");
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);

    // --- Provider selection ---
    auto *providerBox    = new QGroupBox("Active Provider", this);
    auto *providerLayout = new QHBoxLayout(providerBox);
    m_providerGroup = new QButtonGroup(this);

    for (int i = 0; i < providers.size(); ++i) {
        auto *radio = new QRadioButton(providers[i]->displayName(), providerBox);
        m_providerGroup->addButton(radio, i);   // button id == tab index
        providerLayout->addWidget(radio);
        if (providers[i]->id() == activeProviderId)
            radio->setChecked(true);
    }

    layout->addWidget(providerBox);

    // --- Credential tabs (one per provider) ---
    m_tabs = new QTabWidget(this);

    for (int i = 0; i < providers.size(); ++i) {
        StockDataProvider *p = providers[i];

        auto *page = new QWidget();
        auto *form = new QFormLayout(page);
        form->setContentsMargins(12, 12, 12, 12);
        form->setSpacing(8);

        QMap<QString, QLineEdit*> fieldMap;
        if (p->credentialFields().isEmpty()) {
            auto *note = new QLabel(
                "<i>No API key required — works out of the box.</i><br>"
                "<small>Note: this is an unofficial API and may break without warning.</small>",
                page);
            note->setWordWrap(true);
            form->addRow(note);
        } else {
            for (const auto &field : p->credentialFields()) {
                auto *edit = new QLineEdit(page);
                edit->setText(p->credentials().value(field.first));
                edit->setEchoMode(QLineEdit::Password);
                edit->setPlaceholderText("Paste your " + field.second.toLower() + " here");
                edit->setMinimumWidth(280);
                form->addRow(field.second + ":", edit);
                fieldMap[field.first] = edit;
            }

            // Sign-up link
            QString url = signupUrl(p->id());
            if (!url.isEmpty()) {
                auto *link = new QLabel(
                    QString("<a href='%1'>Get a free key at %2</a>").arg(url, url), page);
                link->setOpenExternalLinks(true);
                form->addRow(link);
            }

            // Account / plan link
            QString aUrl = accountUrl(p->id());
            if (!aUrl.isEmpty()) {
                auto *aLink = new QLabel(
                    QString("<a href='%1'>Account / Plan info</a>").arg(aUrl), page);
                aLink->setOpenExternalLinks(true);
                form->addRow(aLink);
            }
        }

        m_fields[p->id()] = fieldMap;
        m_tabs->addTab(page, p->displayName());
    }

    layout->addWidget(m_tabs);

    // --- Buttons ---
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Sync radio selection -> tab
    connect(m_providerGroup, &QButtonGroup::idClicked, m_tabs, &QTabWidget::setCurrentIndex);

    // Set initial tab to match active provider
    for (int i = 0; i < providers.size(); ++i) {
        if (providers[i]->id() == activeProviderId) {
            m_tabs->setCurrentIndex(i);
            break;
        }
    }
}

QString SettingsDialog::selectedProviderId() const
{
    int idx = m_providerGroup->checkedId();
    if (idx >= 0 && idx < m_providers.size())
        return m_providers[idx]->id();
    return m_providers.isEmpty() ? QString() : m_providers.first()->id();
}

QMap<QString, QMap<QString,QString>> SettingsDialog::allCredentials() const
{
    QMap<QString, QMap<QString,QString>> result;
    for (auto it = m_fields.cbegin(); it != m_fields.cend(); ++it) {
        QMap<QString,QString> creds;
        for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt)
            creds[jt.key()] = jt.value()->text().trimmed();
        result[it.key()] = creds;
    }
    return result;
}
