#include "SettingsDialog.h"
#include "ProviderRegistry.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QLabel>
#include <QDialogButtonBox>

SettingsDialog::SettingsDialog(const QList<StockDataProvider*> &providers,
                               const QString &activeProviderId,
                               QWidget *parent)
    : QDialog(parent)
    , m_providers(providers)
{
    setWindowTitle("Configure API Keys");
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);

    // ── Active provider selection ─────────────────────────────────────────────
    auto *providerBox    = new QGroupBox("Active Provider", this);
    auto *providerLayout = new QHBoxLayout(providerBox);
    m_providerGroup = new QButtonGroup(this);

    for (int i = 0; i < providers.size(); ++i) {
        const QString lbl = ProviderRegistry::instance().label(providers[i]->id());
        auto *radio = new QRadioButton(lbl, providerBox);
        radio->setToolTip(ProviderRegistry::instance().comment(providers[i]->id()));
        m_providerGroup->addButton(radio, i);
        providerLayout->addWidget(radio);
        if (providers[i]->id() == activeProviderId)
            radio->setChecked(true);
    }

    layout->addWidget(providerBox);

    // ── Credential tabs (one per provider) ───────────────────────────────────
    m_tabs = new QTabWidget(this);

    for (int i = 0; i < providers.size(); ++i) {
        StockDataProvider *p = providers[i];
        const ProviderInfo pi = ProviderRegistry::instance().info(p->id());

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

            // Sign-up link (from registry)
            if (!pi.url.isEmpty()) {
                auto *link = new QLabel(
                    QString("<a href='%1'>Get a free key at %2</a>").arg(pi.url, pi.url), page);
                link->setOpenExternalLinks(true);
                form->addRow(link);
            }

            // Account / plan link (from registry, shown only when distinct from signup url)
            if (!pi.accountUrl.isEmpty() && pi.accountUrl != pi.url) {
                auto *aLink = new QLabel(
                    QString("<a href='%1'>Account / Plan info</a>").arg(pi.accountUrl), page);
                aLink->setOpenExternalLinks(true);
                form->addRow(aLink);
            }

            // Limited / trial account checkbox
            auto *limitedCheck = new QCheckBox("Trial / free-tier account (limited API calls)", page);
            limitedCheck->setChecked(AppSettings::instance().providerLimited(p->id()));
            limitedCheck->setToolTip(
                "Check this if you are on a free or trial plan. "
                "The app can use this to warn when approaching rate limits.");
            form->addRow(limitedCheck);
            m_limitedChecks[p->id()] = limitedCheck;
        }

        m_fields[p->id()] = fieldMap;
        m_tabs->addTab(page, pi.label);
    }

    layout->addWidget(m_tabs);

    // ── Buttons ───────────────────────────────────────────────────────────────
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

QMap<QString, bool> SettingsDialog::limitedFlags() const
{
    QMap<QString, bool> result;
    for (auto it = m_limitedChecks.cbegin(); it != m_limitedChecks.cend(); ++it)
        result[it.key()] = it.value()->isChecked();
    return result;
}
