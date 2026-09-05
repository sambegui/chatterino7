#pragma once

#include "widgets/settingspages/ScrollableSettingsPage.hpp"

namespace chatterino {

class GeneralPageView;

/// Emote providers, sizing and autocompletion.
class EmotesPage : public ScrollableSettingsPage
{
    Q_OBJECT

public:
    EmotesPage();

private:
    void initLayout(GeneralPageView &layout) override;
};

}  // namespace chatterino
