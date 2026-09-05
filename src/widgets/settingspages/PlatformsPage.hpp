#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// Per-platform integration settings for Twitch and Kick.
class PlatformsPage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    PlatformsPage();

private:
    void initLayout(GeneralPageView &layout) override;
};

}  // namespace chatterino
