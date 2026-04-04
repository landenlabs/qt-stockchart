#include "AdBlockDialog.h"
#include "RequestInterceptor.h"
#include "AppSettings.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QHeaderView>
#include <QFont>

AdBlockDialog::AdBlockDialog(RequestInterceptor *interceptor, QWidget *parent)
    : QDialog(parent, Qt::Tool)
    , m_interceptor(interceptor)
{
    setWindowTitle("Domain Filter / Ad Blocker");
    setMinimumSize(600, 460);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);

    // ── Column headers ────────────────────────────────────────────────────────
    auto *headerRow = new QHBoxLayout();
    auto *leftHeader = new QLabel("<b>Active Domains</b>  (this page load)");
    auto *rightHeader = new QLabel("<b>Blocked Domains</b>");
    headerRow->addWidget(leftHeader, 1);
    headerRow->addSpacing(52);
    headerRow->addWidget(rightHeader, 1);
    mainLayout->addLayout(headerRow);

    // ── Lists + buttons ───────────────────────────────────────────────────────
    auto *listsRow = new QHBoxLayout();
    listsRow->setSpacing(6);

    // Left: active domains list + clear/reload buttons below it
    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(3);

    m_activeList = new QListWidget(this);
    m_activeList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_activeList->setSortingEnabled(false); // managed manually so ~ items stay at bottom
    leftCol->addWidget(m_activeList, 1);

    auto *leftBtnRow = new QHBoxLayout();
    leftBtnRow->setSpacing(4);

    auto *clearBtn = new QToolButton(this);
    clearBtn->setText("\xF0\x9F\x97\x91");  // 🗑 wastebasket
    clearBtn->setToolTip("Clear the active domain list");
    clearBtn->setAutoRaise(true);
    leftBtnRow->addWidget(clearBtn);

    auto *reloadBtn = new QToolButton(this);
    reloadBtn->setText("\xF0\x9F\x94\x84"); // 🔄 reload arrows
    reloadBtn->setToolTip("Reload the current web page");
    reloadBtn->setAutoRaise(true);
    leftBtnRow->addWidget(reloadBtn);

    leftBtnRow->addStretch();
    leftCol->addLayout(leftBtnRow);

    listsRow->addLayout(leftCol, 1);

    // Centre: + / - buttons
    auto *btnCol = new QVBoxLayout();
    btnCol->setSpacing(6);
    btnCol->addStretch();

    m_addBtn = new QPushButton("+", this);
    m_addBtn->setFixedWidth(36);
    m_addBtn->setToolTip("Block selected active domains");
    btnCol->addWidget(m_addBtn);

    m_removeBtn = new QPushButton("\xe2\x88\x92", this); // − (minus sign)
    m_removeBtn->setFixedWidth(36);
    m_removeBtn->setToolTip("Unblock selected domains");
    btnCol->addWidget(m_removeBtn);

    btnCol->addStretch();
    listsRow->addLayout(btnCol);

    // Right: blacklist (domain + hit count)
    m_blackList = new QTreeWidget(this);
    m_blackList->setColumnCount(2);
    m_blackList->setHeaderLabels({ "Domain", "Hits" });
    m_blackList->header()->setStretchLastSection(false);
    m_blackList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_blackList->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_blackList->header()->setMinimumSectionSize(5);
    m_blackList->header()->resizeSection(1, 28);
    m_blackList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_blackList->setRootIsDecorated(false);
    m_blackList->setSortingEnabled(true);
    m_blackList->sortByColumn(0, Qt::AscendingOrder);
    listsRow->addWidget(m_blackList, 1);

    mainLayout->addLayout(listsRow, 1);

    // ── Hint label ────────────────────────────────────────────────────────────
    auto *hint = new QLabel(
        "Select domains in the left list and press <b>+</b> to block them.  "
        "Select rows in the right list and press <b>\xe2\x88\x92</b> to unblock them.  "
        "<b>~</b> prefix = already blocked.", this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    // ── Ad pattern filter ─────────────────────────────────────────────────────
    auto *regexRow = new QHBoxLayout();
    regexRow->addWidget(new QLabel("Ad pattern:", this));
    m_adRegexEdit = new QLineEdit(this);
    m_adRegexEdit->setToolTip(
        "Regular expression used to bold likely ad domains in the left list.\n"
        "Case-insensitive. Changes are saved automatically.");
    m_adRegexEdit->setText(AppSettings::instance().adBlockAdRegex());
    regexRow->addWidget(m_adRegexEdit, 1);
    mainLayout->addLayout(regexRow);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_addBtn,     &QPushButton::clicked,   this, &AdBlockDialog::onAdd);
    connect(m_removeBtn,  &QPushButton::clicked,   this, &AdBlockDialog::onRemove);
    connect(clearBtn,     &QToolButton::clicked,   this, &AdBlockDialog::onClearActive);
    connect(reloadBtn,    &QToolButton::clicked,   this, &AdBlockDialog::onReload);
    connect(m_adRegexEdit, &QLineEdit::textChanged, this, &AdBlockDialog::onRegexChanged);

    // Initialise regex from the edit field content and populate lists once.
    onRegexChanged(m_adRegexEdit->text());
    refreshActiveList();
    refreshBlacklist();
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void AdBlockDialog::onAdd()
{
    const auto selected = m_activeList->selectedItems();
    if (selected.isEmpty()) return;

    QStringList domains;
    domains.reserve(selected.size());
    for (const auto *item : selected) {
        const QString text = item->text();
        if (text.startsWith('~')) continue; // already blocked — skip
        domains << text;
    }
    if (domains.isEmpty()) return;

    m_interceptor->addDomains(domains);
    refreshBlacklist();
}

void AdBlockDialog::onRemove()
{
    const auto selected = m_blackList->selectedItems();
    if (selected.isEmpty()) return;

    QStringList domains;
    domains.reserve(selected.size());
    for (const auto *item : selected)
        domains << item->text(0);

    m_interceptor->removeDomains(domains);
    refreshBlacklist();
}

void AdBlockDialog::onClearActive()
{
    m_interceptor->clearTracking();
    m_activeList->clear();
}

void AdBlockDialog::onReload()
{
    // Clear tracking and the list so the page's fresh requests repopulate it.
    m_interceptor->clearTracking();
    m_activeList->clear();
    emit reloadRequested();
    // refreshActiveList() will be called by WebBrowserWidget once loadFinished fires.
}

void AdBlockDialog::onRegexChanged(const QString &text)
{
    m_adRx = QRegularExpression(text.trimmed(),
                                QRegularExpression::CaseInsensitiveOption);
    AppSettings::instance().setAdBlockAdRegex(text);
    refreshActiveList();
}

// ── Private helpers ────────────────────────────────────────────────────────────

void AdBlockDialog::refreshActiveList()
{
    const QStringList current   = m_interceptor->accessedDomains();
    const QSet<QString> blocked = m_interceptor->blacklist();

    if (current.isEmpty()) {
        m_activeList->clear();
        return;
    }

    // Remember which raw domain names are currently selected (strip ~ prefix)
    QSet<QString> selectedDomains;
    for (const auto *item : m_activeList->selectedItems()) {
        QString t = item->text();
        if (t.startsWith('~')) t = t.mid(1);
        selectedDomains.insert(t);
    }

    // Split into two groups: normal (not blocked) and already-blocked
    QStringList normal, alreadyBlocked;
    for (const QString &d : current) {
        if (blocked.contains(d)) alreadyBlocked << d;
        else                     normal          << d;
    }
    normal.sort(Qt::CaseInsensitive);
    alreadyBlocked.sort(Qt::CaseInsensitive);

    m_activeList->clear();

    // Normal domains first — bold those matching the ad regex
    for (const QString &d : normal) {
        auto *item = new QListWidgetItem(d);
        if (m_adRx.isValid() && !m_adRx.pattern().isEmpty()
                && m_adRx.match(d).hasMatch()) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        if (selectedDomains.contains(d))
            item->setSelected(true);
        m_activeList->addItem(item);
    }

    // Already-blocked domains at the bottom, prefixed with ~, grayed out
    for (const QString &d : alreadyBlocked) {
        auto *item = new QListWidgetItem("~" + d);
        item->setForeground(Qt::gray);
        m_activeList->addItem(item);
    }
}

void AdBlockDialog::refreshBlacklist()
{
    const QSet<QString>    blacklist = m_interceptor->blacklist();
    const QMap<QString,int> hits     = m_interceptor->blockedHits();

    // Build map of existing items for fast lookup.
    QMap<QString, QTreeWidgetItem *> existing;
    for (int i = 0; i < m_blackList->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_blackList->topLevelItem(i);
        existing[it->text(0)] = it;
    }

    // Add new / update existing.
    for (const QString &domain : blacklist) {
        const int h = hits.value(domain, 0);
        const QString hStr = h > 0 ? QString::number(h) : QString();

        if (existing.contains(domain)) {
            existing[domain]->setText(1, hStr);
            existing.remove(domain);
        } else {
            auto *item = new QTreeWidgetItem({ domain, hStr });
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            m_blackList->addTopLevelItem(item);
        }
    }

    // Remove entries that were deleted from the blacklist.
    for (QTreeWidgetItem *stale : std::as_const(existing))
        delete stale;
}
