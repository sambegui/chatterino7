#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// Look-and-feel basics: theme, font, zoom, window and tab behaviour.
class GeneralPage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    GeneralPage();

private:
    void initLayout(GeneralPageView &layout) override;
};

}  // namespace chatterino
