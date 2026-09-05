#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;
class DescriptionLabel;

/// Power-user settings: experimental features, browser integration, cache
/// locations and everything that does not belong on a day-to-day page.
class AdvancedPage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    AdvancedPage();

private:
    void initLayout(GeneralPageView &layout) override;
    void initExtra();

    DescriptionLabel *cachePath_{};
};

}  // namespace chatterino
