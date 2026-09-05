#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// How messages, badges and the overlay are drawn.
class AppearancePage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    AppearancePage();

private:
    void initLayout(GeneralPageView &layout) override;
};

}  // namespace chatterino
