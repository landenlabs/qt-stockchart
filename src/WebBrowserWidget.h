#pragma once
#include <QWidget>
#include <QTabBar>
#include <QLineEdit>
#include <QToolButton>
#include <QPointer>
#include <QString>
#include <QList>
#include "AdBlockDialog.h"

class QWebEngineView;
class QWebEngineProfile;
class RequestInterceptor;

class WebBrowserWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WebBrowserWidget(QWidget *parent = nullptr);

    void setSymbol(const QString &symbol);
    void openAdBlockDialog();

    void saveBlacklist() const;
    void loadBlacklist();

private slots:
    void onTabChanged(int index);
    void onAddTab();
    void onUrlBarReturnPressed();

private:
    void loadTabDefinitions();
    void loadCurrentTab();
    QString buildUrl(int tabIndex) const;

    QTabBar            *m_tabBar      = nullptr;
    QToolButton        *m_addTabBtn   = nullptr;
    QLineEdit          *m_urlBar      = nullptr;
    QWebEngineView     *m_webView     = nullptr;
    QWebEngineProfile  *m_profile     = nullptr;
    RequestInterceptor *m_interceptor = nullptr;
    QString             m_symbol;

    QPointer<AdBlockDialog> m_adBlockDialog;

    struct TabInfo {
        QString name;
        QString urlPattern;   // empty for generic tabs
        QString comment;
        bool    fixed = true; // false = user-added generic tab
        QString lastUrl;      // restored when switching back to a generic tab
    };
    QList<TabInfo> m_tabs;
};
