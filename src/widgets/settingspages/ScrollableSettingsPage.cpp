#include "widgets/settingspages/ScrollableSettingsPage.hpp"

#include "widgets/settingspages/GeneralPageView.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace chatterino {

void ScrollableSettingsPage::buildLayout()
{
    auto *y = new QVBoxLayout;
    auto *x = new QHBoxLayout;
    auto *view = GeneralPageView::withNavigation(this);
    this->view_ = view;
    x->addWidget(view);
    auto *z = new QFrame;
    z->setLayout(x);
    y->addWidget(z);
    this->setLayout(y);

    this->initLayout(*view);
}

bool ScrollableSettingsPage::filterElements(const QString &query)
{
    if (this->view_)
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }

    return false;
}

}  // namespace chatterino
